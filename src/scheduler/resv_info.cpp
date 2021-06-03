/*
 * Copyright (C) 1994-2021 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of both the OpenPBS software ("OpenPBS")
 * and the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * OpenPBS is free software. You can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * OpenPBS is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
 * License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * PBS Pro is commercially licensed software that shares a common core with
 * the OpenPBS software.  For a copy of the commercial license terms and
 * conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
 * Altair Legal Department.
 *
 * Altair's dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of OpenPBS and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair's trademarks, including but not limited to "PBS™",
 * "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
 * subject to Altair's trademark licensing policies.
 */



/**
 * @file    resv_info.c
 *
 * @brief
 * 	resv_info.c - This file contains functions related to advance reservations.
 *
 * Functions included are:
 *	stat_resvs()
 *	query_reservations()
 *	query_resv()
 *	new_resv_info()
 *	free_resv_info()
 *	dup_resv_info()
 *	check_new_reservations()
 *	disable_reservation_occurrence()
 *	confirm_reservation()
 *	check_vnodes_unavailable()
 *	release_nodes()
 *	create_resv_nodes()
 *
 */
#include <pbs_config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pbs_ifl.h>
#include <log.h>
#include <libutil.h>

#include "data_types.h"
#include "resv_info.h"
#include "job_info.h"
#include "queue_info.h"
#include "server_info.h"
#include "misc.h"
#include "sort.h"
#include "globals.h"
#include "node_info.h"
#include "resource_resv.h"
#include "resource.h"
#include "fifo.h"
#include "check.h"
#include "simulate.h"
#include "constant.h"
#include "node_partition.h"
#include "pbs_internal.h"
#include "libpbs.h"

/**
 * @brief
 * 		Statuses reservations from the server in batch status form.
 *
 * @param[in]	pbs_sd	-	The socket descriptor to the server's connection
 *
 * @return	batch status form of the reservations
 */
struct batch_status *
stat_resvs(int pbs_sd)
{
	struct batch_status *resvs;
	/* used for pbs_geterrmsg() */
	const char *errmsg;

	/* get the reservation info from the PBS server */
	if ((resvs = pbs_statresv(pbs_sd, NULL, NULL, NULL)) == NULL) {
		if (pbs_errno) {
			errmsg = pbs_geterrmsg(pbs_sd);
			if (errmsg == NULL)
				errmsg = "";
			log_eventf(PBSEVENT_SCHED, PBS_EVENTCLASS_RESV, LOG_NOTICE, "resv_info",
					"pbs_statresv failed: %s (%d)", errmsg, pbs_errno);
		}
		return NULL;
	}
	return resvs;
}

/**
 *
 * @brief
 *	query_reservations - query the reservations from the server.
 *
 *  Each reservation, is created to reflect its current state in the server.
 *  For a standing reservation, the parent reservation represents the soonest
 *  occurrence known to the server; each remaining occurrence is unrolled to
 *  account for the resources consumed by the standing reservation as a whole.
 *
 *  A degraded reservation, is handled in a manner similar to a confirmed
 *  reservation. Even though resources of a degraded reservation may change in
 *  this scheduling cycle, in case no alternate resources are found, the
 *  reservation retains its currently allocated resources, such that no other
 *  requests make use of the same resources.
 *
 * @param[in] pbs_sd - connection to the pbs server
 * @param[in] sinfo  - the server to query from
 * @param[in] resvs  - batch status of the stat'ed reservations
 *
 * @return    An array of reservations
 *
 */
resource_resv **
query_reservations(int pbs_sd, server_info *sinfo, struct batch_status *resvs)
{
	static struct batch_status *prev_resvs;
	struct batch_status *diff_resvs;
	/* the current reservation in the list */
	struct batch_status *cur_resv;

	/* the array of pointers to internal scheduling structure for reservations */
	resource_resv **resresv_arr;

	/* the current resv */
	resource_resv *resresv;;

	int new_resvs = 0;

	schd_error *err;

	if (resvs == NULL) {
		sinfo->num_resvs = 0;
		free_resource_resv_array(sinfo->resvs);
		return NULL;
	}

	err = new_schd_error();
	if (err == NULL) {
		free_resource_resv_array(sinfo->resvs);
		return NULL;
	}
	
	if (sinfo->resvs == NULL && prev_resvs != NULL) {
		pbs_statfree(prev_resvs);
		prev_resvs = NULL;
	}

	if (prev_resvs != NULL)
		diff_resvs = diff_batch_status_list(prev_resvs, resvs);
	else
		diff_resvs = resvs;

	cur_resv = diff_resvs;

	if (sinfo->resvs == NULL) {
		if ((resresv_arr = static_cast<resource_resv **> (malloc(sizeof(resource_resv *)))) == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			free_schd_error(err);
			return NULL;
		}
		resresv_arr[0] = NULL;
	} else
		resresv_arr = sinfo->resvs;

	for (cur_resv = diff_resvs; cur_resv != NULL; cur_resv = cur_resv->next) {
		int ignore_resv = 0;
		struct attrl *attrp = NULL;
		resource_resv *presresv = NULL;
		int old_is_running = 0;

		clear_schd_error(err);

		/* Check if this reservation belongs to this scheduler */
		for (attrp = cur_resv->attribs; attrp != NULL; attrp = attrp->next) {
			if (strcmp(attrp->name, ATTR_partition) == 0) {
				if (sc_attrs.partition != NULL && (strcmp(attrp->value, sc_attrs.partition) != 0))
					ignore_resv = 1;
				break;
			}
		}
		
		presresv = find_resource_resv(resresv_arr, cur_resv->name);
		if (presresv != NULL) {
			if (ignore_resv) { /* resv confirmed on another partition */
				remove_ptr_from_array(resresv_arr, presresv);
				sinfo->num_resvs--;
				delete presresv;
				presresv = NULL;
			} else if (presresv->resv->is_standing) {
				while (presresv != NULL && presresv->end != UNSPECIFIED && presresv->end < sinfo->server_time) {
					resource_resv *tmp_resresv;
					presresv->resv->resv_queue->resv = NULL;
					remove_ptr_from_array(resresv_arr, presresv);
					tmp_resresv = find_resource_resv(resresv_arr, cur_resv->name);
					if (presresv->resv->resv_queue->jobs != NULL)
						for (int i = 0; presresv->resv->resv_queue->jobs[i] != NULL; i++)
							presresv->resv->resv_queue->jobs[i]->job->resv = tmp_resresv;
					delete presresv;
					sinfo->num_resvs--;
					presresv = tmp_resresv;
				}
				if (presresv == NULL)
					ignore_resv = 1;
			}
		}

		if (cur_resv->attribs == NULL && presresv != NULL) { /* Reservation removed */
			sinfo->num_resvs -= remove_resv_from_server(resresv_arr, presresv);
			continue;
		}

		if (ignore_resv) {
			requery_universe = true;
			continue;
		}

		if (presresv != NULL)
			old_is_running = is_resresv_running(presresv);

		if ((resresv = query_resv(cur_resv, sinfo, presresv)) == NULL) {
			free_resource_resv_array(resresv_arr);
			free_schd_error(err);
			requery_universe = true;
			return NULL;
		}
#ifdef NAS /* localmod 047 */
		if (resresv->place_spec == NULL) {
			resresv->place_spec = parse_placespec("scatter");
		}
#endif /* localmod 047 */

		/* We continue adding valid resvs to our array.  We're
		 * freeing what we allocated and ignoring this resv completely.
		 */
		if (!is_resource_resv_valid(resresv, err) || resresv->is_invalid) {
			schdlogerr(PBSEVENT_DEBUG, PBS_EVENTCLASS_RESV, LOG_DEBUG, resresv->name,
				"Reservation is invalid - ignoring for this cycle", err);
			ignore_resv = 1;
		}
		/* Make sure it is not a future reservation that is being deleted, if so ignore it */
		else if ((resresv->resv->resv_state == RESV_BEING_DELETED) && (resresv->start > sinfo->server_time)) {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_RESV, LOG_DEBUG, resresv->name, 
				"Future reservation is being deleted, ignoring this reservation");
			ignore_resv = 1;
		}
		else if ((resresv->resv->resv_state == RESV_BEING_DELETED) && (resresv->resv->resv_nodes != NULL) &&
			(!is_string_in_arr(resresv->resv->resv_nodes[0]->resvs, resresv->name.c_str()))) {
			log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_RESV, LOG_DEBUG, resresv->name, 
				"Reservation is being deleted and not present on node, ignoring this reservation");
			ignore_resv = 1;
		}

		if (ignore_resv) {
			/* mark all the jobs of the associated queue as can never run */
			if (resresv->resv->resv_queue != NULL) {
				clear_schd_error(err);
				set_schd_error_arg(err, SPECMSG, "Reservation is in an invalid state");
				set_schd_error_codes(err, NEVER_RUN, ERR_SPECIAL);
				update_jobs_cant_run(pbs_sd, resresv->resv->resv_queue->jobs, NULL, err, START_WITH_JOB);
				resresv->resv->resv_queue->resv = NULL;
			}
			if (presresv == resresv)
				sinfo->num_resvs -= remove_resv_from_server(resresv_arr, presresv);
			else
				delete resresv;

			requery_universe = true;
			continue;
		}

		resresv->aoename = getaoename(resresv->select);
		resresv->eoename = geteoename(resresv->select);

		/* reservations requesting AOE mark nodes as exclusive */
		if (resresv->aoename) {
			resresv->place_spec->share = 0;
			resresv->place_spec->excl = 1;
		}

		/*
		 * Check to see if we can attempt to confirm this reservation.
		 * If we can, then then all we will do in this cycle is attempt
		 * to confirm reservations.  In that case, build the calendar
		 * using the hard durations of jobs.
		 */
		if (will_confirm(resresv, sinfo->server_time))
			sinfo->use_hard_duration = 1;

		resresv->duration = resresv->resv->req_duration;
		resresv->hard_duration = resresv->duration;
		if (resresv->resv->resv_state != RESV_UNCONFIRMED) {
			resresv->start = resresv->resv->req_start;
			if(resresv->resv->resv_state == RESV_BEING_DELETED ||
			   resresv->start + resresv->duration <= sinfo->server_time) {
				resresv->end = sinfo->server_time + EXITING_TIME;
			} else
				resresv->end = resresv->resv->req_end;
		}

		if (resresv->node_set_str != NULL) {
			if (resresv->node_set != NULL)
				free(resresv->node_set);
			resresv->node_set = create_node_array_from_str(
				resresv->server->unassoc_nodes, resresv->node_set_str);
		}
		if (is_resresv_running(resresv) && !old_is_running) {
			for (int i = 0; resresv->ninfo_arr[i] != NULL; i++)
				resresv->ninfo_arr[i]->num_run_resv++;
		}

		if (resresv->resv->resv_queue != NULL)
			resresv->resv->resv_queue->resv = resresv;
		/* The server's info only gives information about a single reservation
		 * object. In the case of a standing reservation, it is up to the
		 * scheduler to account for each occurrence and attempt to confirm the
		 * reservation.
		 *
		 * In such a case, each occurrence has to be 'cloned'
		 * by duplicating the parent reservation and setting the specific start
		 * and end times and unique execvnodes for each occurrence.
		 *
		 * Note that because the first occurrence may reschedule the start time
		 * from the one submitted by reserve_start (see -R of pbs_rsub), the
		 *  initial start time has to be reconfirmed. An example of such
		 * rescheduling is:
		 *   pbs_rsub -R 2000 -E 2100 -r "FREQ=DAILY;BYHOUR=9,20;COUNT=2"
		 * for which the first occurrence is 9am and then 8pm.
		 * BYHOUR takes priority over the start time specified by -R of pbs_rsub.
		 *
		 * Only unroll the occurrences if the parent reservation has been
		 * confirmed.
		 */
		if (resresv->resv->is_standing &&
			(resresv->resv->resv_state != RESV_UNCONFIRMED)) {
			resource_resv *resresv_ocr = NULL; /* the occurrence's resource_resv */
			char *execvnodes_seq = NULL; /* confirmed execvnodes sequence string */
			char **execvnode_ptr = NULL;
			char **tofree = NULL;
			time_t dtstart;
			time_t next;
			char *rrule = NULL;
			char *tz = NULL;
			struct tm* loc_time;
			char start_time[128];
			int count = 0;
			int occr_count; /* occurrences count as reported by execvnodes_seq */
			int occr_idx = 1; /* the occurrence index of a standing reservation */
			int degraded_idx; /* index corrected to account for reconfirmation */

			/* occr_idx refers to the soonest occurrence to run or currently running
			 * Note that resv_idx starts at 1 on the first occurrence and not 0.
			 */
			occr_idx = resresv->resv->resv_idx;
			execvnodes_seq = string_dup(resresv->resv->execvnodes_seq);
			/* the error handling for the string duplication returning NULL is
			 * combined with the following assignment, because get_execvnodes_count
			 * returns 0 if passed a NULL argument
			 */
			occr_count = get_execvnodes_count(execvnodes_seq);
			/* this should happen only if the execvnodes_seq are corrupted. In such
			 * case, we ignore the reservation and move on to the next one
			 */
			if (occr_count == 0) {
				log_event(PBSEVENT_SCHED, PBS_EVENTCLASS_RESV, LOG_DEBUG,
					resresv->name, "Error processing standing reservation");
				free(execvnodes_seq);
				if (presresv == NULL)
					delete resresv;
				continue;
			} 
			/* unroll_execvnode_seq will destroy the first argument that is passed
			 * to it by calling tokenizing functions, hence, it has to be duplicated
			 */
			execvnode_ptr = unroll_execvnode_seq(execvnodes_seq, &tofree);
			count = resresv->resv->count;

			/* 'count' and 'occr_idx' attributes persist through the life of the
			 * standing reservation. After a reconfirmation, the new execvnodes
			 * sequence may be shortened, therefore the occurrence index used to
			 * identify which execvnode is associated to which occurrence needs to
			 * be adjusted to take into account the elapsed occurrences
			 */
			degraded_idx = occr_idx - (count - occr_count);

			rrule = resresv->resv->rrule;
			dtstart = resresv->resv->req_start;
			tz = resresv->resv->timezone;

			/* Add each occurrence to the universe's view by duplicating the
			 * parent reservation and resetting start and end times and the
			 * execvnode on which the occurrence is confirmed to run.
			 */
			for (int i = 0; occr_idx <= count; occr_idx++, i++, degraded_idx++) {
				/* If it is not the first occurrence then update the start time as
				 * req_start_standing (if set). This is to ensure that if the first
				 * occurrence has been changed, other future occurrences are not
				 * affected.
				 */
				if (i == 1 && resresv_ocr->resv->req_start_standing != UNSPECIFIED)
					dtstart = resresv_ocr->resv->req_start_standing;
				/* Get the start time of the next occurrence computed from dtstart.
				 * The server maintains state of a single reservation object for
				 * which in the case of a standing reservation, it updates start
				 * and end times and execvnodes.
				 * The last argument (i+1) indicates the occurrence index from dtstart
				 * starting at 1. Returns dtstart if it's an advance reservation.
				 */
				next = get_occurrence(rrule, dtstart, tz, i + 1);

				/* Duplicate the "master" resv only for subsequent occurrences */
				if (i == 0)
					resresv_ocr = resresv;
				else {
					resresv_ocr = dup_resource_resv(resresv, sinfo, NULL);
					if (resresv_ocr == NULL) {
						free_resource_resv_array(resresv_arr);
						if (presresv == NULL)
							delete resresv;
						free_execvnode_seq(tofree);
						free(execvnodes_seq);
						free(execvnode_ptr);
						free_schd_error(err);
						return NULL;
					}
					if (resresv->resv->resv_state == RESV_RUNNING ||
						resresv->resv->resv_state == RESV_BEING_ALTERED ||
						resresv->resv->resv_state == RESV_DELETING_JOBS) {
						/* Each occurrence will be added to the simulation framework and
						 * should not be in running state. Their state should be
						 * Confirmed instead of possibly inheriting the Running state
						 * from the parent reservation.
						 */
						resresv_ocr->resv->resv_state = RESV_CONFIRMED;
						resresv_ocr->resv->is_running = 0;
					}
					/* Duplication deep-copies node info array. This array gets
					 * overwritten and needs to be freed. This is an alternative
					 * to creating another duplication function that only duplicates
					 * the required fields.
					 */
					release_nodes(resresv_ocr);

					if (resresv_ocr->resv->select_standing != NULL) {
						delete resresv_ocr->select;
						resresv_ocr->select = new selspec(*resresv_ocr->resv->select_standing);
					}

					resresv_ocr->resv->orig_nspec_arr = parse_execvnode(
						execvnode_ptr[degraded_idx - 1], sinfo, resresv_ocr->select);
					resresv_ocr->nspec_arr = combine_nspec_array(resresv_ocr->resv->orig_nspec_arr);
					resresv_ocr->ninfo_arr = create_node_array_from_nspec(resresv_ocr->nspec_arr);
					resresv_ocr->resv->resv_nodes = create_resv_nodes(
						resresv_ocr->nspec_arr, sinfo);
				}

				/* Set occurrence start and end time and nodes information. On the
				 * first occurrence the start time may need to be reset to the time
				 * specified by the recurrence rule. See description at the head of
				 * this block.
				 */
				resresv_ocr->resv->req_start = next;
				/* If it is not the first occurrence then update the duration as
				 * req_duration_standing (if set). This is to ensure that if the first
				 * occurrence has been changed, other future occurrences are not
				 * affected.
				 */
				if (i != 0 && resresv->resv->req_duration_standing != UNSPECIFIED)
					resresv_ocr->hard_duration = resresv_ocr->duration = resresv->resv->req_duration_standing;
				resresv_ocr->resv->req_end = next + resresv->duration;
				resresv_ocr->start = resresv_ocr->resv->req_start;
				resresv_ocr->end = resresv_ocr->resv->req_end;
				resresv_ocr->resv->resv_idx = occr_idx;

				resresv_ocr->rank = get_sched_rank();

				if (presresv == NULL) {
					resource_resv **tmp;
					tmp = add_resresv_to_array(resresv_arr, resresv_ocr, NO_FLAGS);
					if (tmp == NULL) {
						free_resource_resv_array(resresv_arr);
						if (presresv == NULL)
							delete resresv;
						free_execvnode_seq(tofree);
						free(execvnodes_seq);
						free(execvnode_ptr);
						free_schd_error(err);
						return NULL;
					}
					resresv_arr = tmp;
					new_resvs++;
				} else if (i > 0) {
					int j;
					for (j = 0; resresv_arr[j] != NULL && !(resresv_arr[j]->name == resresv_ocr->name &&
										resresv_arr[j]->start == resresv_ocr->start); j++)
						;
					/* The cycle after the reservation gets confirmed, we'll have only one occurrence of the reservation
					 * So presresv != NULL, but we still need to add the other occurrences
					 */
					if (resresv_arr[j] == NULL) {
						resource_resv **tmp;
						tmp = add_resresv_to_array(resresv_arr, resresv_ocr, NO_FLAGS);
						if (tmp == NULL) {
							free_resource_resv_array(resresv_arr);
							delete resresv_ocr;
							free_schd_error(err);
							return NULL;
						}
						resresv_arr = tmp;
						new_resvs++;
					} else {
						delete resresv_arr[j];
						resresv_arr[j] = resresv_ocr;
					}
				}

				loc_time = localtime(&resresv_ocr->start);
				strftime(start_time, sizeof(start_time), "%Y%m%d-%H:%M:%S", loc_time);

				log_eventf(PBSEVENT_DEBUG2, PBS_EVENTCLASS_RESV, LOG_DEBUG, resresv->name,
					"Occurrence %d/%d,%s", occr_idx, count, start_time);
			}
			/* The parent reservation has already been added so move on to handling
			 * the next reservation
			 */

			free_execvnode_seq(tofree);
			free(execvnodes_seq);
			free(execvnode_ptr);
		} else if (presresv == NULL) {
			resource_resv **tmp;
			tmp = add_resresv_to_array(resresv_arr, resresv, NO_FLAGS);
			if (tmp == NULL) {
				free_resource_resv_array(resresv_arr);
				delete resresv;
				free_schd_error(err);
				return NULL;
			}
			resresv_arr = tmp;
			new_resvs++;
		}
	}
	for (int i = 0; resresv_arr[i] != NULL; i++) {
		if (resresv_arr[i]->resv->rjob_state_change) {
			/* pick up any new running jobs on the resv_nodes */
			for (int j = 0; resresv_arr[i]->resv->resv_nodes[j] != NULL; j++) {
				node_info *resv_node = resresv_arr[i]->resv->resv_nodes[j];

				if (resv_node->jobs != NULL)
					free_string_array(resv_node->jobs);
				resv_node->jobs = dup_string_arr(resv_node->svr_node->jobs);
			}

			if (resresv_arr[i]->resv->resv_queue->jobs != NULL) {
				std::unordered_map<std::string, resource_resv *> jobs_in_reservations;
				for (int j = 0; resresv_arr[i]->resv->resv_queue->jobs[j] != NULL; j++) {
					auto job = resresv_arr[i]->resv->resv_queue->jobs[j];

					if (check_running_job_in_reservation(job, NULL))
						jobs_in_reservations[job->name] = job;
				}

				collect_jobs_on_nodes(resresv_arr[i]->resv->resv_nodes, jobs_in_reservations, NO_FLAGS);

				for (int j = 0; resresv_arr[i]->resv->resv_nodes[j] != NULL; j++)
					create_resource_assn_for_node(resresv_arr[i]->resv->resv_nodes[j], 1);

				/* Sort the nodes to ensure correct job placement. */
				qsort(resresv_arr[i]->resv->resv_nodes,
				      count_array(resresv_arr[i]->resv->resv_nodes),
				      sizeof(node_info *), multi_node_sort);
			}
			resresv_arr[i]->resv->rjob_state_change = 0;
		}
	}

	free_schd_error(err);
	sinfo->num_resvs += new_resvs;

	if (prev_resvs != NULL)
		pbs_statfree(prev_resvs);
	
	if (diff_resvs != resvs)
		pbs_statfree(diff_resvs);

	prev_resvs = resvs;

	return resresv_arr;
}

void modify_job_for_resv(resource_resv *rjob, resource_resv *resv, time_t server_time)
{
	int i;

	if (rjob == NULL || resv == NULL)
		return;
	
	rjob->job->resv = resv;
	rjob->job->can_not_preempt = 1;
	if (rjob->node_set_str != NULL) {
		if (rjob->node_set != NULL)
			free(rjob->node_set);
		rjob->node_set = create_node_array_from_str(resv->resv->resv_nodes, rjob->node_set_str);
	}

	/* if a job will exceed the end time of a duration, it will be
	 * killed by the server. We set the job's end time to the resv's
	 * end time for better estimation.
	 */
	if (server_time + rjob->duration > resv->end) {
		rjob->duration = resv->end - server_time;
		rjob->hard_duration = rjob->duration;
		if (rjob->end != UNSPECIFIED)
			rjob->end = resv->end;
	}

	if (rjob->job->is_running) {
		/* the reservations resv_nodes is pointing to
		 * a node_info array with just the reservations part of the node
		 * i.e. the universe of the reservation
		 */
		for (i = 0; rjob->nspec_arr[i] != NULL; i++) {
			nspec *ns;
			node_info *resvnode;
			ns = rjob->nspec_arr[i];
			resvnode = find_node_by_indrank(resv->resv->resv_nodes, -1, ns->rank);

			if (resvnode != NULL) {
				/* update the ninfo to point to the ninfo in our universe */
				ns->ninfo = resvnode;
				rjob->ninfo_arr[i] = resvnode;
			} else {
#ifdef NAS /* localmod 031 */
				log_eventf(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, rjob->name,
					   "Job has been assigned a node that doesn't exist in its reservation: %s", ns->ninfo->name);
#else
				log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, rjob->name,
					  "Job has been assigned a node which doesn't exist in its reservation");
#endif /* localmod 031 */
			}
		}
		if (rjob->ninfo_arr[i] != NULL) {
			log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, rjob->name,
				  "Job's node array has different length than nspec_arr in query_reservations()");
		}
	} else if (rjob->nspec_arr != NULL) {
		free_nspecs(rjob->nspec_arr);
		rjob->nspec_arr = NULL;
		free(rjob->ninfo_arr);
		rjob->ninfo_arr = NULL;
	}
}

/**
 * @brief
 *		query_resv - convert the servers batch_status structure into a
 *			 resource_resv/resv_info structs for easier access
 *
 * @param[in]	resv	-	a single reservation in batch_status form
 * @param[in]	sinfo 	- 	the server
 *
 * @return	the converted resource_resv struct
 *
 */
resource_resv *
query_resv(struct batch_status *resv, server_info *sinfo, resource_resv *presresv)
{
	struct attrl *attrp = NULL;	/* linked list of attributes from server */
	resource_resv *advresv = NULL;	/* resv_info to be created */
	resource_req *resreq = NULL;	/* used for the ATTR_l resources */
	char *endp = NULL;		/* used with strtol() */
	long count = 0; 		/* used to convert string -> num */
	char *resv_nodes = NULL;	/* used to hold the resv_nodes for later processing */
	int occr_changed = 0;

	if (resv == NULL)
		return NULL;

	if (presresv == NULL) {
		advresv = new resource_resv(resv->name);
		if ((advresv->resv = new_resv_info()) == NULL) {
			delete advresv;
			return NULL;
		}
		advresv->server = sinfo;
		advresv->is_resv = 1;

	} else
		advresv = presresv;

	attrp = resv->attribs;

	while (attrp != NULL) {
		bool unset = false;
		if (attrp->value[0] == '\0') {
			unset = true;
			endp = NULL;
		} else {
			count = strtol(attrp->value, &endp, 10);
			if (*endp != '\0')
				count = UNSPECIFIED;
		}

		if (!strcmp(attrp->name, ATTR_resv_owner)) {
			if (advresv->user != NULL)
				free(advresv->user);
			advresv->user = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_egroup)) {
			if (advresv->group != NULL)
				free(advresv->group);
			advresv->group = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_queue)) {
			if (advresv->resv->queuename != NULL)
				free(advresv->resv->queuename);
			advresv->resv->queuename = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_SchedSelect)) {
			if (advresv->select != NULL)
				delete advresv->select;
			advresv->select = parse_selspec(attrp->value);
			if (advresv->select != NULL && advresv->select->chunks != NULL) {
				/* Ignore resv if any of the chunks has no resource req. */
				int i;
				for (i = 0; advresv->select->chunks[i] != NULL; i++)
					if (advresv->select->chunks[i]->req == NULL)
						advresv->is_invalid = true;
			} else
				advresv->is_invalid = true;
		} else if (!strcmp(attrp->name, ATTR_resv_start))
			advresv->resv->req_start = count;
		else if (!strcmp(attrp->name, ATTR_resv_end))
			advresv->resv->req_end = count;
		else if (!strcmp(attrp->name, ATTR_resv_duration))
			advresv->resv->req_duration = count;
		else if (!strcmp(attrp->name, ATTR_resv_alter_revert)) {
			if (!strcmp(attrp->resource, "start_time")) {
				if (unset)
					advresv->resv->req_start_orig = UNSPECIFIED;
				else 
					advresv->resv->req_start_orig = count;
			} else if (!strcmp(attrp->resource, "walltime")) {
				if (unset)
					advresv->resv->req_duration_orig = UNSPECIFIED;
				else
					advresv->resv->req_duration_orig = (time_t) res_to_num(attrp->value, NULL);
			}
		} else if (!strcmp(attrp->name, ATTR_resv_standing_revert)) {
			if (!strcmp(attrp->resource, "start_time")) {
				if (unset)
					advresv->resv->req_start_standing = UNSPECIFIED;
				else
					advresv->resv->req_start_standing = count;
			} else if (!strcmp(attrp->resource, "walltime")) {
				if (unset)
					advresv->resv->req_duration_standing = UNSPECIFIED;
				else
					advresv->resv->req_duration_standing = (time_t) res_to_num(attrp->value, NULL);
			} else if (!strcmp(attrp->resource, "select")) { 
				if (unset) {
					delete advresv->resv->select_standing;
					advresv->resv->select_standing = NULL;
				} else
					advresv->resv->select_standing = parse_selspec(attrp->value);
			}
		} else if (!strcmp(attrp->name, ATTR_resv_retry)) {
			if (unset)
				advresv->resv->retry_time = UNSPECIFIED;
			else
				advresv->resv->retry_time = count;
		} else if (!strcmp(attrp->name, ATTR_resv_state))
			advresv->resv->resv_state = (enum resv_states) count;
		else if (!strcmp(attrp->name, ATTR_resv_substate))
			advresv->resv->resv_substate = (enum resv_states) count;
		else if (!strcmp(attrp->name, ATTR_l)) { /* resources requested */
			resreq = find_alloc_resource_req_by_str(advresv->resreq, attrp->resource);
			if (resreq == NULL) {
				delete advresv;
				return NULL;
			}

			if (set_resource_req(resreq, attrp->value) != 1)
				advresv->is_invalid = 1;
			else {
				if (advresv->resreq == NULL)
					advresv->resreq = resreq;
				if (!strcmp(attrp->resource, "place")) {
					if (advresv->place_spec != NULL)
						free_place(advresv->place_spec);
					advresv->place_spec = parse_placespec(attrp->value);
					if (advresv->place_spec == NULL)
						advresv->is_invalid = 1;
				}
			}
		} else if (!strcmp(attrp->name, ATTR_resv_nodes)) {
			resv_nodes = attrp->value;
			if (advresv->resv->resv_nodes_str != NULL)
				free(advresv->resv->resv_nodes_str);
			advresv->resv->resv_nodes_str = string_dup(resv_nodes);
		} else if (!strcmp(attrp->name, ATTR_node_set)) {
			if (advresv->node_set_str != NULL)
				free_string_array(advresv->node_set_str);
			if (unset)
				advresv->node_set_str = NULL;
			else
				advresv->node_set_str = break_comma_list(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_resv_timezone)) {
			if (advresv->resv->timezone != NULL)
				free(advresv->resv->timezone);
			advresv->resv->timezone = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_resv_rrule)) {
			if (advresv->resv->rrule != NULL )
				free(advresv->resv->rrule);
			advresv->resv->rrule = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_resv_execvnodes)) {
			if (advresv->resv->execvnodes_seq != NULL)
				free(advresv->resv->execvnodes_seq);
			advresv->resv->execvnodes_seq = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_resv_idx)) {
			advresv->resv->resv_idx = count;
			occr_changed = 1;
		} else if (!strcmp(attrp->name, ATTR_resv_standing))
			advresv->resv->is_standing = count;
		else if (!strcmp(attrp->name, ATTR_resv_count))
			advresv->resv->count = count;
		else if (!strcmp(attrp->name, ATTR_partition)) {
			if (advresv->resv->partition != NULL)
				free(advresv->resv->partition);
			advresv->resv->partition = string_dup(attrp->value);
		} else if (!strcmp(attrp->name, ATTR_SchedSelect_orig)) {
			if (advresv->resv->select_orig != NULL)
				delete advresv->resv->select_orig;
			if (unset)
				advresv->resv->select_orig = NULL;
			else
				advresv->resv->select_orig = parse_selspec(attrp->value);
			if (resv_nodes == NULL)
				resv_nodes = advresv->resv->resv_nodes_str;
		} else if (!strcmp(attrp->name, ATTR_server_inst_id)) {
			advresv->svr_inst_id = string_dup(attrp->value);
			if (advresv->svr_inst_id == NULL) {
				delete advresv;
				return NULL;
			}
		}
		attrp = attrp->next;
	}

	/* If we have a select_orig, this means we're doing an ralter and reducing the size of our reservation
	 * We need to map the orig chunks to chunks from the smaller select to make sure we keep them.
	 * To do this, we set the seq_num of the select chunk to the same seq_num of the select_orig chunk
	 *
	 */
	if (advresv->resv->select_orig != NULL) {
		int i;
		int j = 0;
		int sel_orig_num, sel_num;
		char *sel_orig_str, *sel_str;
		sel_num = strtol(advresv->select->chunks[j]->str_chunk, &sel_str, 10);

		for (i = 0; advresv->resv->select_orig->chunks[i] != NULL; i++) {
			chunk *chk = advresv->resv->select_orig->chunks[i];
			sel_orig_num = strtol(chk->str_chunk, &sel_orig_str, 10);
			if (strcmp(sel_orig_str, sel_str) == 0 && sel_num <= sel_orig_num) {
				advresv->select->chunks[j++]->seq_num = chk->seq_num;
				if (advresv->select->chunks[j] != NULL)
					sel_num = strtol(advresv->select->chunks[j]->str_chunk, &sel_str, 10);
				else
					break;
			}
		}
	}

	if (resv_nodes != NULL) {
		selspec *sel;
		std::string selectspec;
		node_info **new_resv_nodes;
		/* parse the execvnode and create an nspec array with ninfo ptrs pointing
		 * to nodes in the real server
		 */
		if (advresv->resv->select_orig != NULL)
			sel = advresv->resv->select_orig;
		else
			sel = advresv->select;
		if (advresv->resv->orig_nspec_arr != NULL) {
			free_nspecs(advresv->resv->orig_nspec_arr);
			delete advresv->execselect;
		}
		advresv->resv->orig_nspec_arr = parse_execvnode(resv_nodes, sinfo, sel);
		selectspec = create_select_from_nspec(advresv->resv->orig_nspec_arr);
		advresv->execselect = parse_selspec(selectspec);

		if (advresv->nspec_arr != NULL)
			free_nspecs(advresv->nspec_arr);
		advresv->nspec_arr = combine_nspec_array(advresv->resv->orig_nspec_arr);
		if (advresv->ninfo_arr != NULL)
			free(advresv->ninfo_arr);
		advresv->ninfo_arr = create_node_array_from_nspec(advresv->nspec_arr);

		/* create a node info array by copying the nodes and setting
		 * available resources to only the ones assigned to the reservation
		 */
		new_resv_nodes = create_resv_nodes(advresv->nspec_arr, sinfo);
		if (advresv->resv->resv_nodes != NULL)
			free_nodes(advresv->resv->resv_nodes);
		advresv->resv->resv_nodes = new_resv_nodes;

	}

	/* The first time we see the reservation, the partition won't be set on its queue, so it won't be queried.
	 * We won't associate the queue with the reservation until the second cycle
	 */
	if (advresv->resv->queuename != NULL && advresv->resv->resv_queue == NULL)
		advresv->resv->resv_queue = find_queue_info(sinfo->queues, advresv->resv->queuename);

	if ((resv_nodes != NULL || occr_changed) && advresv->resv->resv_queue != NULL && advresv->resv->resv_queue->jobs != NULL) {
			int i;
			for (i = 0; advresv->resv->resv_queue->jobs[i] != NULL; i++)
				modify_job_for_resv(advresv->resv->resv_queue->jobs[i], advresv, sinfo->server_time);
	}

	/* If reservation is unconfirmed and the number of occurrences is 0 then flag
	 * the reservation as invalid. This is an extra check but isn't supposed to
	 * happen because the server will purge such reservations.
	 */
	if (advresv->resv->resv_state == RESV_UNCONFIRMED && get_num_occurrences(advresv->resv->rrule,
		advresv->resv->req_start, advresv->resv->timezone) == 0) {
		advresv->is_invalid = 1;
	}

	/* When a reservation is recognized as DEGRADED, it is converted into
	 * state = CONFIRMED; substate = DEGRADED
	 * From the scheduler's perspective, the reservation is CONFIRMED in that its
	 * allocated resources remain scheduled in the calendar, but it is
	 * handled as an UNCONFIRMED reservation when its resources are to be
	 * replaced.
	 */
	if (advresv->resv->resv_state == RESV_DEGRADED) {
		advresv->resv->resv_state = RESV_CONFIRMED;
		if (advresv->resv->resv_substate != RESV_IN_CONFLICT)
			advresv->resv->resv_substate = RESV_DEGRADED;
	}

	advresv->resv->is_running = 0;
	if (advresv->resv->resv_state == RESV_BEING_ALTERED) {
		time_t alter_end = advresv->resv->req_start_orig + advresv->resv->req_duration_orig;
		if (advresv->resv->req_start_orig <= sinfo->server_time && alter_end >= sinfo->server_time)
			advresv->resv->is_running = 1;
	} else if (advresv->resv->req_start <= sinfo->server_time && advresv->resv->req_end >= sinfo->server_time && advresv->resv->resv_state != RESV_UNCONFIRMED)
		advresv->resv->is_running = 1;

	return advresv;
}

/**
 * @brief
 *		new_resv_info - allocate and initialize new resv_info structure
 *
 * @return	the new structure
 *
 */
resv_info *
new_resv_info()
{
	resv_info *rinfo;

	if ((rinfo = static_cast<resv_info *>(malloc(sizeof(resv_info)))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return NULL;
	}

	rinfo->queuename = NULL;
	rinfo->req_start = UNSPECIFIED;
	rinfo->req_start_orig = UNSPECIFIED;
	rinfo->req_start_standing = UNSPECIFIED;
	rinfo->req_end = UNSPECIFIED;
	rinfo->req_duration = UNSPECIFIED;
	rinfo->req_duration_orig = UNSPECIFIED;
	rinfo->req_duration_standing = UNSPECIFIED;
	rinfo->retry_time = UNSPECIFIED;
	rinfo->resv_state = RESV_NONE;
	rinfo->resv_substate = RESV_NONE;
	rinfo->resv_queue = NULL;
	rinfo->resv_nodes = NULL;
	rinfo->timezone = NULL;
	rinfo->rrule = NULL;
	rinfo->resv_idx = 1;
	rinfo->execvnodes_seq = NULL;
	rinfo->count = 0;
	rinfo->is_standing = 0;
	rinfo->is_running = 0;
	rinfo->rjob_state_change = 0;
	rinfo->occr_start_arr = NULL;
	rinfo->partition = NULL;
	rinfo->select_orig = NULL;
	rinfo->select_standing = NULL;
	rinfo->orig_nspec_arr = NULL;
	rinfo->resv_nodes_str = NULL;

	return rinfo;
}

/**
 * @brief
 *		free_resv_info - free all the memory used by a rev_info structure
 *
 * @param[in]	rinfo	-	the resv_info to free
 *
 * @return	nothing
 *
 */
void
free_resv_info(resv_info *rinfo)
{
	if (rinfo == NULL)
		return;
	
	if (rinfo->queuename != NULL)
		free(rinfo->queuename);

	if (rinfo->resv_nodes != NULL)
		free_nodes(rinfo->resv_nodes);

	if (rinfo->timezone != NULL)
		free(rinfo->timezone);

	if (rinfo->rrule != NULL)
		free(rinfo->rrule);

	if (rinfo->execvnodes_seq != NULL)
		free(rinfo->execvnodes_seq);

	if (rinfo->occr_start_arr != NULL)
		free(rinfo->occr_start_arr);

	if (rinfo->partition != NULL)
		free(rinfo->partition);

	if (rinfo->select_orig != NULL)
		delete rinfo->select_orig;

	if (rinfo->select_standing != NULL)
		delete rinfo->select_standing;

	if (rinfo->orig_nspec_arr != NULL)
		free_nspecs(rinfo->orig_nspec_arr);
	
	if (rinfo->resv_nodes_str != NULL);
	free(rinfo->resv_nodes_str);

	free(rinfo);

}

/**
 * @brief
 *		dup_resv_info - duplicate a reservation
 *
 * @param[in]	rinfo	-	the reservation to duplicate
 * @param[in]	sinfo 	-	the server the NEW reservation belongs to
 *
 * @return	duplicated reservation
 *
 */
resv_info *
dup_resv_info(resv_info *rinfo, server_info *sinfo)
{
	resv_info *nrinfo;

	if (rinfo == NULL)
		return NULL;

	if ((nrinfo = new_resv_info()) == NULL)
		return NULL;

	if (rinfo->queuename != NULL)
		nrinfo->queuename = string_dup(rinfo->queuename);

	nrinfo->req_start = rinfo->req_start;
	nrinfo->req_start_orig = rinfo->req_start_orig;
	nrinfo->req_start_standing = rinfo->req_start_standing;
	nrinfo->req_end = rinfo->req_end;
	nrinfo->req_duration = rinfo->req_duration;
	nrinfo->req_duration_orig = rinfo->req_duration_orig;
	nrinfo->req_duration_standing = rinfo->req_duration_standing;
	nrinfo->retry_time = rinfo->retry_time;
	nrinfo->resv_state = rinfo->resv_state;
	nrinfo->resv_substate = rinfo->resv_substate;
	nrinfo->is_standing = rinfo->is_standing;
	nrinfo->is_running = rinfo->is_running;
	nrinfo->timezone = string_dup(rinfo->timezone);
	nrinfo->rrule = string_dup(rinfo->rrule);
	nrinfo->resv_idx = rinfo->resv_idx;
	nrinfo->execvnodes_seq = string_dup(rinfo->execvnodes_seq);
	nrinfo->count = rinfo->count;
	if (rinfo->partition != NULL)
		nrinfo->partition = string_dup(rinfo->partition);
	if (rinfo->select_orig != NULL)
		nrinfo->select_orig = new selspec(*rinfo->select_orig);
	if (rinfo->select_standing != NULL)
		nrinfo->select_standing = new selspec(*rinfo->select_standing);

	/* the queues may not be available right now.  If they aren't, we'll
	 * catch this when we duplicate the queues
	 */
	if (rinfo->resv_queue != NULL)
		nrinfo->resv_queue = find_queue_info(sinfo->queues, rinfo->resv_queue->name);

	nrinfo->resv_nodes = dup_nodes(rinfo->resv_nodes, sinfo, NO_FLAGS);
	nrinfo->resv_nodes_str = string_dup(rinfo->resv_nodes_str);

	return nrinfo;
}

/**
 * @brief
 * 		check for new reservations and handle them
 *		if we can serve the reservation, we reserve it
 *		and if we can't, we delete the reservation
 * @par
 *  	For a standing reservation, each occurrence is unrolled and attempted
 * 		to be confirmed. If a single occurrence fails to be confirmed, then the
 * 		standing reservation is rejected.
 * @par
 *  	For a degraded reservation, the resources allocated to the reservation
 * 		are free'd in the simulated universe, and we attempt to reconfirm resources
 * 		for it. If it fails then we inform the server that the reconfirmation has
 * 		failed. If it succeeds, then the previously allocated resources are freed
 * 		from the real universe and replaced by the newly allocated resources.
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	communication descriptor to PBS server
 * @param[in]	resvs	-	list of reservations
 * @param[in]	sinfo	-	the server who owns the reservations
 *
 * @return	int
 * @retval	number of reservations confirmed
 * @retval	-1	: something went wrong with confirmation, retry later
 *
 */
int
check_new_reservations(status *policy, int pbs_sd, resource_resv **resvs, server_info *sinfo)
{
	int count = 0;	/* new reservation count */
	int pbsrc = 0;	/* return code from pbs_confirmresv() */

	server_info *nsinfo = NULL;
	resource_resv *nresv = NULL;
	resource_resv *nresv_copy = NULL;
	resource_resv **tmp_resresv = NULL;

	char **occr_execvnodes_arr = NULL;
	char **tofree = NULL;
	int occr_count =1;
	int have_alter_request = 0;
	int i;
	int j;
	schd_error *err;

	if (sinfo == NULL)
		return -1;

	/* If no reservations to check then return, this is not an error */
	if (resvs == NULL || resvs[0] == NULL)
		return 0;

	err = new_schd_error();
	if (err == NULL)
		return -1;

	qsort(sinfo->resvs, sinfo->num_resvs, sizeof(resource_resv*), cmp_resv_state);

	for (i = 0; sinfo->resvs[i] != NULL; i++) {
		if (sinfo->resvs[i]->resv == NULL) {
			log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, sinfo->resvs[i]->name,
				"Error determining if reservation can be confirmed: "
				"Could not find the reservation.");
			continue;
		}

		/* If the reservation is unconfirmed OR is degraded and not running, with a
		 * retry time that is in the past, then the reservation has to be
		 * respectively confirmed and reconfirmed.
		 */
		if (will_confirm(sinfo->resvs[i], sinfo->server_time)) {
			/* Clone the real universe for simulation scratch work. This universe
			 * will be garbage collected after simulation completes.
			 */
			nsinfo = dup_server_info(sinfo);
			if (nsinfo == NULL)
				return -1;

			/* Resource reservations are ordered by event time, in the case of a
			 * standing reservation, the first to be found will be the "parent"
			 * reservation
			 */
			nresv = find_resource_resv_by_indrank(nsinfo->resvs, sinfo->resvs[i]->resresv_ind, sinfo->resvs[i]->rank);
			if (nresv == NULL) {
				log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, sinfo->resvs[i]->name,
					"Error determining if reservation can be confirmed: Resource not found.");
				free_server(nsinfo);
				return -1;
			}

			release_running_resv_nodes(nresv, nsinfo);

			/* Attempt to confirm the reservation. For a standing reservation,
			 * each occurrence is unrolled and attempted to be confirmed within the
			 * function.
			 */
			pbsrc = confirm_reservation(policy, pbs_sd, nresv , nsinfo);

			/* confirm_reservation only returns success if all occurrences were
			 * confirmed and the communication with the server returned no error
			 */
			if (pbsrc == RESV_CONFIRM_SUCCESS) {
				/* If a degraded reservation, then we need to release the resources
				 * that were previously allocated to the reservation in the real
				 * universe. These resources will be replaced by the newly allocated
				 * ones from the simulated server universe.
				 */
				if (nresv->resv->resv_substate == RESV_DEGRADED || nresv->resv->resv_substate == RESV_IN_CONFLICT)
					release_nodes(sinfo->resvs[i]);

				/* the number of occurrences is set during the confirmation process */
				occr_count = nresv->resv->count;

				/* Now deal with updating the "real" server universe */

				/* If a standing reservation, unroll the string representation of the
				 * sequence of execvnodes into an array of pointer to execvnodes */
				if (nresv->resv->is_standing) {
					/* "tofree" is a pointer array to a list of unique execvnodes. It is
					 * safely freed exclusively by calling free_execvnode_seq
					 */
					occr_execvnodes_arr = unroll_execvnode_seq(
						nresv->resv->execvnodes_seq, &tofree);
					if (occr_execvnodes_arr == NULL) {
						log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, sinfo->resvs[i]->name,
							"Error unrolling standing reservation.");
						free_server(nsinfo);
						return -1;
					}
				}
				else {
					/* Since we will use occr_execvnodes_arr both for standing and advance
					 * reservations, we create an array with a single entry to hold the
					 * advance reservation's execvnode.
					 */
					occr_execvnodes_arr = static_cast<char **>(malloc(sizeof(char *)));
					if (occr_execvnodes_arr == NULL) {
						free_server(nsinfo);
						log_err(errno, __func__, MEM_ERR_MSG);
						return -1;
					}
					*occr_execvnodes_arr = nresv->resv->execvnodes_seq;
				}

				/* Iterate over all occurrences (would be 1 for advance reservations)
				 * and copy the information collected during simulation back into the
				 * real universe
				 */
				for (j = 0; j < occr_count; j++) {
					/* On first occurrence, the reservation is the "parent" reservation */
					if (j == 0) {
						nresv_copy = sinfo->resvs[i];
					}
					/* Subsequent occurrences need to be either modified or created
					 * depending on whether the reservation is to be reconfirmed or
					 * is getting confirmed for the first time.
					 */
					else {
						/* For a degraded reservation, it had already been confirmed in a
						 * previous scheduling cycle. We retrieve the existing object from
						 * the all_resresv list
						 */
						if (nresv->resv->resv_substate == RESV_DEGRADED || nresv->resv->resv_substate == RESV_IN_CONFLICT) {
							nresv_copy = find_resource_resv_by_time(sinfo->all_resresv,
								nresv_copy->name, nresv->resv->occr_start_arr[j]);
							if (nresv_copy == NULL) {
								log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name,
									"Error determining if reservation can be confirmed: "
									"Could not find reservation by time.");
								break;
							}
						}
						else {
							/* For a new, unconfirmed, reservation, we duplicate the parent
							 * reservation
							 */
							nresv_copy = dup_resource_resv(nresv_copy, sinfo, NULL);
							if (nresv_copy == NULL)
								break;
							if (nresv_copy->resv->select_standing != NULL) {
								delete nresv_copy->select;
								nresv_copy->select = new selspec(*nresv_copy->resv->select_standing);
							}
						}
						/* Duplication deep-copies node info array. This array gets
						 * overwritten and needs to be freed. This is an alternative
						 * to creating another duplication function that only duplicates
						 * the required fields.
						 */
						release_nodes(nresv_copy);

						nresv_copy->resv->orig_nspec_arr = parse_execvnode(occr_execvnodes_arr[j], sinfo, nresv_copy->select);
						nresv_copy->nspec_arr = combine_nspec_array(nresv_copy->resv->orig_nspec_arr);
						nresv_copy->ninfo_arr = create_node_array_from_nspec(nresv_copy->nspec_arr);
						nresv_copy->resv->resv_nodes = create_resv_nodes(nresv_copy->nspec_arr, sinfo);
					}

					/* Note that the sequence of occurrence dates and time are determined
					 * during confirm_reservation and set to the reservation in the
					 * simulated server
					 */
					nresv_copy->start = nresv->resv->occr_start_arr[j];

					/* update start time, duration, and execvnodes of the occurrence */
					nresv_copy->end = nresv_copy->start + nresv_copy->duration;

					/* Only add the occurrence to the real universe if we are not
					 * processing a degraded reservation as otherwise, the resources
					 * had already been added to the real universe in query_reservations
					 */
					if (nresv_copy->resv->resv_substate != RESV_DEGRADED && nresv_copy->resv->resv_substate != RESV_IN_CONFLICT) {
						timed_event *te_start;
						timed_event *te_end;
						te_start = create_event(TIMED_RUN_EVENT, nresv_copy->start,
							nresv_copy, NULL, NULL);
						if (te_start == NULL)
							break;
						te_end = create_event(TIMED_END_EVENT, nresv_copy->end,
							nresv_copy, NULL, NULL);
						if (te_end == NULL) {
							free_timed_event(te_start);
							break;
						}
						add_event(sinfo->calendar, te_start);
						add_event(sinfo->calendar, te_end);

						if (j > 0) {
							tmp_resresv = add_resresv_to_array(sinfo->all_resresv, nresv_copy, SET_RESRESV_INDEX);
							if (tmp_resresv == NULL)
								break;
							sinfo->all_resresv = tmp_resresv;
							tmp_resresv = add_resresv_to_array(sinfo->resvs, nresv_copy, NO_FLAGS);
							if (tmp_resresv == NULL)
								break;
							sinfo->resvs = tmp_resresv;
							sinfo->num_resvs++;
						}
					}

					/* Confirm the reservation such that it is not looked at again in the
					 * main loop of this function.
					 */
					nresv_copy->resv->resv_state = RESV_CONFIRMED;
					nresv_copy->resv->resv_substate = RESV_CONFIRMED;
				}
				/* increment the count if we successfully processed all occurrences */
				if (j == occr_count)
					count++;
			} else if (pbsrc == RESV_CONFIRM_FAIL) {
				/* For a degraded reservation, it had already been confirmed in a
				 * previous scheduling cycle. We retrieve the existing object from
				 * the all_resresv list and update the retry_time to break out of
				 * the main loop that checks for reservations that need confirmation
				 */
				if (nresv->resv->resv_substate == RESV_DEGRADED || nresv->resv->resv_substate == RESV_IN_CONFLICT) {
					for (j = 0; j < nresv->resv->count; j++) {
						nresv_copy = find_resource_resv_by_time(sinfo->all_resresv,
							nresv->name, nresv->resv->occr_start_arr[j]);
						if (nresv_copy == NULL) {
							log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name,
								"Error determining if reservation can be confirmed: "
								"Could not find reservation by time.");
							break;
						}
						/* Update the retry time such that occurrences of a standing
						 * reservation do not independently attempt to be reconfirmed
						 * This is meant to break out of the conditional that checks what
						 * will be considered "confirmable" by the scheduler. Either one
						 * of updating the substate to something else than RESV_DEGRADED or
						 * updating the reservation retry time to some time in the future
						 * invalidating the condition would work.
						 *
						 * We choose to update the retry_time for consistency with what
						 * the server actually does upon receiving the message informing
						 * it that the reservation could not be reconfirmed.
						 */
						nresv_copy->resv->retry_time = sinfo->server_time + 1;
					}
				}
			}
			/* clean up */
			free(nresv->resv->occr_start_arr);
			nresv->resv->occr_start_arr = NULL;
			free_execvnode_seq(tofree);
			tofree = NULL;
			free(occr_execvnodes_arr);
			occr_execvnodes_arr = NULL;

			/* Clean up simulated server info */
			free_server(nsinfo);
		}
		if (sinfo->resvs[i]->resv->resv_state == RESV_BEING_ALTERED)
			have_alter_request = 1;

		/* Something went wrong with reservation confirmation, retry later */
		if (pbsrc == RESV_CONFIRM_RETRY) {
			free_schd_error(err);
			return -1;
		}
	}
	free_schd_error(err);
	/* If a reservation is being altered, its attributes are the new altered attributes.
	 * If the alter fails, we can't continue with a cycle because the reservation
	 * reverted back to its pre-altered state, but the copy we have is as if the alter succeeded.
	 * If no reservations have been confirmed, we will run a normal cycle.
	*/
	if (have_alter_request && count == 0)
		return -1;
	return count;
}

/**
 * @brief
 * 		mark the timed event associated to a resource reservation at a given time as
 * 		disabled.
 *
 * @param[in]	events	-	the event which to be disabled in the calendar.
 * @param[in]	resv	-	the resource reservation being disabled
 *
 * @return	int
 * @retval	1	: on success
 * @retval	0	: on failure
 */
static int
disable_reservation_occurrence(timed_event *events,
	resource_resv *resv)
{
	if(resv == NULL)
		return 0;

	if (resv->run_event != NULL)
		set_timed_event_disabled(resv->run_event, 1);
	if (resv->end_event != NULL)
		set_timed_event_disabled(resv->end_event, 1);
	return 1;
}

/**
 * @brief
 * 		determines if a resource reservation can be satisfied
 *
 * @param[in]	policy	-	policy info
 * @param[in]	pbs_sd	-	connection to server
 * @param[in]	unconf_resv	-	the reservation to confirm
 * @param[in]	nsinfo	-	the simulated server info universe
 *
 * @return	int
 * @retval	RESV_CONFIRM_SUCCESS
 * @retval	RESV_CONFIRM_FAIL
 *
 * @note
 * 		This function modifies the resource reservation by adding the number of
 * 		occurrences and the sequence of occurrence times, which are then used when
 * 		checking for new and degraded reservations.
 */
int
confirm_reservation(status *policy, int pbs_sd, resource_resv *unconf_resv, server_info *nsinfo)
{
	time_t sim_time;			/* time in simulation */
	unsigned int simrc = TIMED_NOEVENT;	/* ret code from simulate_events() */
	schd_error *err = NULL;
	int pbsrc = 0;				/* return code from pbs_confirmresv() */
	enum resv_conf rconf = RESV_CONFIRM_SUCCESS; /* assume reconf success */
	char logmsg[MAX_LOG_SIZE];
	char logmsg2[MAX_LOG_SIZE];
	const char *errmsg;

	nspec **ns = NULL;
	resource_resv *nresv = unconf_resv;
	resource_resv **tmp_resresv = NULL;
	resource_resv *nresv_copy = NULL;

	resource_resv *nresv_parent = nresv;	/* the "original" / parent reservation */

	int confirmd_occr = 0;			/* the number of confirmed occurrence(s) */
	int j, cur_count;

	int tot_vnodes = 0;			/* total number of vnodes associated to the reservation */
	int vnodes_down = 0;			/* the number of vnodes that are down */
	char names_of_down_vnodes[MAXVNODELIST] = "";  /* the list of down vnodes */

	/* resv_start_time is used both for calculating the time of an ASAP
	 * reservation and to keep track of the start time of the first occurrence
	 * of a standing reservation.
	 */
	time_t resv_start_time = 0;		/* estimated start time for resv */
	time_t *occr_start_arr = NULL;		/* an array of occurrence start times */

	std::string execvnodes;
	char *short_xc = NULL;
	char *tmp = NULL;
	time_t next;

	char *rrule = nresv->resv->rrule; 	/* NULL for advance reservation */
	time_t dtstart = nresv->resv->req_start;
	char *tz = nresv->resv->timezone;
	int occr_count = nresv->resv->count;
	int ridx = nresv->resv->resv_idx - 1;

	logmsg[0] = logmsg2[0] = '\0';

	err = new_schd_error();
	if (err == NULL)
		return RESV_CONFIRM_FAIL;


	/* If the number of occurrences is not set, this is a first time confirmation
	 * otherwise it is a reconfirmation request
	 */
	if (occr_count == 0)
		occr_count = get_num_occurrences(rrule, dtstart, tz);
	else {
		/* If the number of occurrences (occr_count) was already set, then we are
		 * dealing with the reconfirmation of a reservation. We need to adjust the
		 * number of occurrences to account only for the remaining occurrences and
		 * not the original number at the time the reservation was first submitted
		 */
		if (nresv->resv->resv_state != RESV_BEING_ALTERED)
			occr_count -= ridx;
		else
			occr_count = 1;
	}

	if ((occr_start_arr = static_cast<time_t *>(calloc(sizeof(time_t), occr_count))) == NULL) {
		log_err(errno, __func__, MEM_ERR_MSG);
		return RESV_CONFIRM_FAIL;
	}


	/* Each reservation attempts to confirm a set of nodes on which to run for
	 * a given start and end time. When handling an advance reservation,
	 * the current reservation is considered. For a standing reservation,
	 * each occurrence is processed by duplicating the parent reservation
	 * and attempts to confirm it.
	 *
	 * All the scratch work attempting to confirm the reservation takes place
	 * in a deep copy of the server info,and is done by simulating events just
	 * as if the server were processing them.
	 *
	 * At the end of the simulation, this cloned server info is completely
	 * wiped and a fresh version is created from the recorded 'sinfo' state.
	 *
	 * It's critical that when handling a standing reservation, each occurrence
	 * be added to the server info such that the duplicated server info has up to
	 * date information.
	 */
	for (j = 0, cur_count = 0; j < occr_count && rconf == RESV_CONFIRM_SUCCESS;
		j++, cur_count = j) {
		/* Get the start time of the next occurrence.
		 * See call to same function in query_reservations for a more in-depth
		 * description.
		 */
		next = get_occurrence(rrule, dtstart, tz, j + 1);
		/* keep track of each occurrence's start time */
		occr_start_arr[j] = next;

		/* Processing occurrences of a standing reservation requires duplicating
		 * the "parent" reservation as template for each occurrence, modifying its
		 * start time and duration and running a simulation for that occurrence
		 *
		 * This duplication is only done for subsequent occurrences, not for the
		 * parent reservation.
		 */
		if (j > 0) {
			/* If the reservation is Degraded, then it has already been added to the
			 * real universe, so instead of duplicating the parent reservation, it
			 * is retrieved from the duplicated real server universe
			 */
			if (nresv->resv->resv_substate == RESV_DEGRADED || nresv->resv->resv_substate == RESV_IN_CONFLICT) {
				nresv_copy = find_resource_resv_by_time(nsinfo->all_resresv,
					nresv->name, next);
				if (nresv_copy == NULL) {
					log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name,
						"Error determining if reservation can be confirmed: "
						"Could not find reservation by time.");
					rconf = RESV_CONFIRM_FAIL;
					break;
				}
				nresv = nresv_copy;
			} else {
				nresv_copy = dup_resource_resv(nresv, nsinfo, NULL);

				if (nresv_copy == NULL) {
					rconf = RESV_CONFIRM_FAIL;
					break;
				}
				nresv = nresv_copy;

				/* add it to the simulated universe of reservations.
				 * Also add it to the reservation list (resvs) to be garbage collected
				 */
				tmp_resresv = add_resresv_to_array(nsinfo->resvs, nresv, NO_FLAGS);
				if (tmp_resresv == NULL) {
					delete nresv;
					rconf = RESV_CONFIRM_FAIL;
					break;
				}
				nsinfo->resvs = tmp_resresv;

				tmp_resresv = add_resresv_to_array(nsinfo->all_resresv, nresv, SET_RESRESV_INDEX);
				if (tmp_resresv == NULL) {
					delete nresv;
					rconf = RESV_CONFIRM_FAIL;
					break;
				}
				nsinfo->all_resresv = tmp_resresv;
				nsinfo->num_resvs++;
			}
			execvnodes += TOKEN_SEPARATOR;
		}

		/* If reservation is degraded, then verify that some node(s) associated to
		 * the reservation are down before attempting to reconfirm it. If some
		 * are, then resources allocated to this reservation are released and the
		 * reconfirmation proceeds.
		 */
		if (nresv->resv->resv_substate == RESV_DEGRADED || nresv->resv->resv_substate == RESV_IN_CONFLICT ||
			nresv->resv->resv_state == RESV_BEING_ALTERED) {
			/* determine the number of vnodes associated to the reservation that are
			 * unavailable. If none, then this reservation or occurrence does not
			 * require reconfirmation.
			 */
			if (nresv->resv->resv_state == RESV_BEING_ALTERED)
				vnodes_down = ralter_reduce_chunks(nresv);
			else
				vnodes_down = check_vnodes_unavailable(nresv);

			if (vnodes_down < 0 && nresv->resv->resv_substate != RESV_IN_CONFLICT) {
				if (vnodes_down == -1)
					snprintf(logmsg, sizeof(logmsg), "Reservation has running jobs in it");
				rconf = RESV_CONFIRM_FAIL;
				break;
			} else if (nresv->resv->is_standing && nresv->resv->resv_state == RESV_DELETING_JOBS) {
				snprintf(logmsg, sizeof(logmsg), "Occurrence is ending, will try later");
				rconf = RESV_CONFIRM_FAIL;
			} else if (vnodes_down > 0 || nresv->resv->resv_substate == RESV_IN_CONFLICT ||
				nresv->resv->resv_state == RESV_BEING_ALTERED) {
				if (nresv->resv->is_running) {
					std::string sel;
					int ind;
					delete nresv->execselect;
					/* Use resv->orig_nspec_arr over nspec_arr because
					 * A) we modified it above in check_vnodes_unavailable() for reconfirmation
					 * B) it will allow us to map the original select back to the new resv_nodes
					 */
					sel = create_select_from_nspec(nresv->resv->orig_nspec_arr);
					nresv->execselect = parse_selspec(sel);
					for (ind = 0; nresv->resv->orig_nspec_arr[ind] != NULL; ind++) {
					    nresv->execselect->chunks[ind]->seq_num = nresv->resv->orig_nspec_arr[ind]->seq_num;
					}

					release_running_resv_nodes(nresv, nsinfo);
				}
				release_nodes(nresv);
			} else if (vnodes_down == 0) {
				/* this occurrence doesn't require reconfirmation so skip it by
				 * incrementing the number of occurrences confirmed and appending
				 * this occurrence's execvnodes to the sequence of execvnodes
				 */
				confirmd_occr++;
				tmp = create_execvnode(nresv->resv->orig_nspec_arr);
				if (j == 0)
					execvnodes = tmp;
				else  { /* subsequent occurrences */
					execvnodes += tmp;
					execvnodes += TOKEN_SEPARATOR;
				}
				continue;
			}
			if (!nresv->resv->is_running) {
				if (disable_reservation_occurrence(nsinfo->calendar->events, nresv) != 1) {
					log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name,
						  "Error determining if reservation can be confirmed: "
						  "Could not mark occurrence disabled.");
					rconf = RESV_CONFIRM_FAIL;
					break;
				}
			}
		}

		if (nresv->resv->req_start == PBS_RESV_FUTURE_SCH) { /* ASAP Resv */
			resv_start_time = calc_run_time(nresv->name, nsinfo, NO_FLAGS);
			/* Update occr_start_arr used to update the real sinfo structure */
			occr_start_arr[j] = resv_start_time;
		} else {
			nresv->resv->req_start = next;
			nresv->start = nresv->resv->req_start;
			nresv->end = nresv->start + nresv->resv->req_duration;

			/* "next" is used in simulate_events to determine the time up to which
			 * to simulate the universe
			 */
			simrc = simulate_events(policy, nsinfo, SIM_TIME, (void *) &next, &sim_time);
		}
		if (!(simrc & TIMED_ERROR) && resv_start_time >= 0) {
			clear_schd_error(err);
			if ((ns = is_ok_to_run(nsinfo->policy, nsinfo, NULL, nresv, NO_ALLPART, err)) != NULL) {
				qsort(ns, count_array(ns), sizeof(nspec *), cmp_nspec);
				tmp = create_execvnode(ns);
				free_nspecs(ns);
				if (tmp == NULL) {
					log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name,
						"Error determining if reservation can be confirmed: Creation of execvnode failed.");
					rconf = RESV_CONFIRM_FAIL;
					break;
				}

				if (j == 0) { /* first occurrence keeps track of first execvnode */
					execvnodes = tmp;
					/* Update resv_start_time only if not an ASAP reservation to
					 * schedule the reservation on the first occurrence.
					 */
					if (resv_start_time == 0)
						resv_start_time = next;
				}
				else /* subsequent occurrences */
					execvnodes += tmp;

				confirmd_occr++;
			}
			/* Something went wrong trying to determine if it's "ok to run", which
			 * may be a problem checking for limits or checking for availability of
			 * resources.
			 */
			else {
				(void) translate_fail_code(err, NULL, logmsg);

				/* If the reservation is degraded, we log a message and continue */
				if (nresv->resv->resv_substate == RESV_DEGRADED || nresv->resv->resv_substate == RESV_IN_CONFLICT) {
					log_eventf(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name, 
						"Reservation Failed to Reconfirm: %s", logmsg);
				}
				/* failed to confirm so move on. This will throw flow out of the
				 * loop
				 */
				rconf = RESV_CONFIRM_FAIL;
			}
		} /* end of simulation */
		else {
			log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv->name,
				"Error determining if reservation can be confirmed: Simulation failed.");
			rconf = RESV_CONFIRM_FAIL;
		}
	}

	/* Finished simulating occurrences now time to confirm if ok. Currently
	 * the confirmation is an all or nothing process but may come to change. */
	if (confirmd_occr == occr_count) {
		char confirm_msg[LOG_BUF_SIZE] = {0};
		/* We either confirm a standing or advance reservation, the standing
		 * has a special sequence of execvnodes while the advance has a single
		 * execvnode. The sequence of execvnodes is created by concatenating
		 * each execvnode and condensing the concatenated string.
		 */
		if (nresv_parent->resv->is_standing)
			short_xc = condense_execvnode_seq(execvnodes.c_str());
		else
			short_xc = string_dup(execvnodes.c_str());
		
		if (short_xc == NULL || get_execvnodes_count(short_xc) != occr_count) {
			log_event(PBSEVENT_DEBUG, PBS_EVENTCLASS_RESV, LOG_DEBUG, nresv_parent->name, "Invalid execvnode_seq while confirming reservation");
			rconf = RESV_CONFIRM_RETRY;
		} else {

			log_eventf(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv_parent->name,
				"Confirming %d Occurrences", occr_count);

			/* Send a reservation confirm message, if anything goes wrong pbsrc
			* will return an error
			*/
			snprintf(confirm_msg, LOG_BUF_SIZE, "%s:partition=%s", PBS_RESV_CONFIRM_SUCCESS,
				sc_attrs.partition ? sc_attrs.partition : DEFAULT_PARTITION);

			pbsrc = send_confirmresv(pbs_sd, nresv_parent, short_xc, resv_start_time, confirm_msg);
		}
	}
	else {
		/* This message is sent to inform that we could not confirm the reservation.
		 * If the reservation was degraded then the retry time will be reset.
		 * "null" is used satisfy the API but any string would do because we've
		 * failed to confirm the reservation and no execvnodes were determined
		 */
		pbsrc = send_confirmresv(pbs_sd, nresv_parent, "null", resv_start_time, PBS_RESV_CONFIRM_FAIL);
	}
	/* Error handling first checks for the return code from the server and the
	 * confirmation flag. If either failed, we print the error message, otherwise
	 */
	if (pbsrc > 0 || rconf == RESV_CONFIRM_FAIL) {
		/* If the scheduler could not find a place for the reservation, use the
		 * translated error code. Otherwise, use the error message from the server.
		 */
		if (rconf == RESV_CONFIRM_FAIL)
			log_eventf(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv_parent->name,
				"PBS Failed to confirm resv: %s", logmsg);
		else {
			errmsg = pbs_geterrmsg(pbs_sd);
			if (errmsg == NULL)
				errmsg = "";
			log_eventf(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv_parent->name,
				"PBS Failed to confirm resv: %s (%d)", errmsg, pbs_errno);
			rconf = RESV_CONFIRM_RETRY;
		}

		if (nresv_parent->resv->resv_substate == RESV_DEGRADED) {
			if (vnodes_down >= 0)
				log_eventf(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv_parent->name,
					"Reservation is in degraded mode, %d out of %d vnodes are unavailable; %s",
					vnodes_down, tot_vnodes, names_of_down_vnodes);

			/* we failed to confirm the degraded reservation but we still need to
			 * set the remaining occurrences start time to avoid looking at them
			 * in the future. We had set the occr_start_arr times in the main loop
			 * so we only care about the remaining ones
			 */
			for (; cur_count < occr_count; cur_count++) {
				next = get_occurrence(rrule, dtstart, tz, cur_count + 1);
				occr_start_arr[cur_count] = next;
			}
		}
		free(short_xc);
	}
	/* If the (re)confirmation was a success then we update the sequence of
	 * occurrence start times, the number of occurrences, and the sequence of
	 * execvnodes
	 */
	else if (rconf == RESV_CONFIRM_SUCCESS) {
		log_event(PBSEVENT_RESV, PBS_EVENTCLASS_RESV, LOG_INFO, nresv_parent->name, "Reservation Confirmed");

		/* If handling a degraded reservation or while altering a standing reservation
		 * we recreate a new execvnode sequence string, so the old should be cleared.
		 */
		free(nresv_parent->resv->execvnodes_seq);

		/* set or update (for reconfirmation) the sequence of execvnodes */
		nresv_parent->resv->execvnodes_seq = short_xc;
	}
	/* The sequence of occurrence times and the total number of occurrences are
	 * made available to populate the 'real' sinfo in check_new_reservations
	 */
	nresv_parent->resv->occr_start_arr = occr_start_arr;
	nresv_parent->resv->count = occr_count;

	/* clean up */
	free_schd_error(err);

	/* the return value is initialized to RESV_CONFIRM_SUCCESS */
	return rconf;
}

/**
 * @brief determine if a nspec superchunk/chunk has unavailable nodes
 * 		and checks for running jobs on the chunk
 * @param[in] resv - reservation to check
 * @param[in] chunk_ind - index of the chunk start
 *
 * @return int
 * @retval 1 - running jobs, no unavailable nodes
 * @retval 0 no unavailable nodes, no running jobs
 * @retval -1 unavailable nodes, but no running jobs on the chunk
 * @retval -2 unavailable nodes, running jobs within the chunk
 * @retval -3 error
 *
 */
int
check_down_running(resource_resv *resv, int chunk_ind)
{
	int i, j, k;
	int ret = 0;

	if (resv == NULL || chunk_ind < 0 || !resv->is_resv ||
	    resv->resv == NULL || resv->resv->resv_queue == NULL)
		return -3;

	for (i = chunk_ind; resv->resv->orig_nspec_arr[i] != NULL && ret != -2; i++) {
		nspec *ns = resv->resv->orig_nspec_arr[i];
		node_info *ninfo = ns->ninfo;

		if (ninfo != NULL) {
			if (ninfo->is_down || ninfo->is_offline || ninfo->is_stale || ninfo->is_unknown || ninfo->is_maintenance || ninfo->is_sleeping) {
				if (ret == 1)
					ret = -2;
				else
					ret = -1;
			}

			if (resv->resv->resv_queue->running_jobs != NULL)
				for (j = 0; resv->resv->resv_queue->running_jobs[j] != NULL && ret != -2; j++) {
					resource_resv *job = resv->resv->resv_queue->running_jobs[j];
					for (k = 0; job->ninfo_arr[k] != NULL && ret != -2; k++)
						if (job->ninfo_arr[k]->rank == ninfo->rank) {
							if (ret == -1)
								ret = -2;
							else
								ret = 1;
						}
				}

			if (ns->end_of_chunk)
				break;
		}
	}

	return ret;
}

/**
 * @brief remove nodes from a reservation.  Per call, we remove one type of chunk based on the chunk's seq_num
 * 	We remove num_chunks if we can (or fail if we can't)  We remove based on node_type which controls whether
 * 	we are removing unavailable nodes or available nodes
 *
 * @param[in] resv - reservation to remove nodes from
 * @param[in] start_of_chk - index into resv->orig_nspec_arr of where to start
 * @param[in] chk_seq_num - sequence number of chunk to remove nodes from
 * @param[in] num_chks - number of nodes to remove.  Depending on node_type, it is OK to remove less than this
 * @param[in] node_type - type of node to remove: -1 unavailable, 0 normal, 1 either all with no running jobs
 *
 * @note all nspec chunks to be removed will have their ninfo pointer NULL'd.  It up to the caller to actually remove them from the reservation.
 *
 * @return int
 * @retval number of chunks removed from the nspec array
 * @retval -1 nspecs are not mapped to select chunks
 */
int ralter_remove_nodes(resource_resv *resv, int start_of_chk, int chk_seq_num, int num_chks, int node_type) {
	int i,k;
	int cur_chks;

	cur_chks = num_chks;
	nspec **nspec_arr;

	nspec_arr = resv->resv->orig_nspec_arr;

	for (i = start_of_chk; nspec_arr[i] != NULL && cur_chks; i++) {
		int ret;
		if (nspec_arr[i]->chk == NULL)
			return -1;

		if (nspec_arr[i]->chk->seq_num != chk_seq_num)
			break;
		ret = check_down_running(resv, i);
		if (ret == node_type || node_type == 1) {
			k = i;
			do {
				if (k != i)
					k++;
				nspec_arr[k]->ninfo = NULL;
			} while (nspec_arr[k] != NULL && !nspec_arr[k]->end_of_chunk);

			cur_chks--;
			/* In the case of a superchunk, we advance past it.  In the case of a normal chunk, k didn't move, so we reassign i to i */
			i = k;
		}
	}
	return num_chks - cur_chks;
}

/**
 * @brief reduce nodes of a reservation based on it new altered select.
 * 	The original select's chunks have been already mapped onto the resv's nodes.
 * 	When choosing nodes to release, we will first choose nodes which are unavailable.
 * 	A node with a running job on it can not be released.
 *
 * @param[in] resv - the reservation to shrink
 *
 * @return int
 * @retval 1 the reservation has been successfully shrunk
 * @retval 0 no select_orig, not doing a pbs_ralter -l select
 * @retval -1 can't remove enough chunks due to running jobs
 * @retval -2 can't reduce due to resv_nodes not correctly mapped to select_orig
 */
int
ralter_reduce_chunks(resource_resv *resv)
{
	int cnt;
	int start_of_chunk = 0;
	int i, j, k;
	chunk **chks_orig, **chks;

	if (resv == NULL)
		return -2;

	/* We're not altering the select, just return success */
	if (resv->resv->select_orig == NULL)
		return 0;

	cnt = count_array(resv->resv->orig_nspec_arr);
	j = 0;
	chks_orig = resv->resv->select_orig->chunks;
	chks = resv->select->chunks;
	for (i = 0; chks_orig[i] != NULL; i++) {
		int num_chks = 0;
		if (chks[j] == NULL || chks_orig[i]->seq_num != chks[j]->seq_num) {
			/* We're removing the entire chunk, so remove all the nodes of the chunk */
			num_chks = ralter_remove_nodes(resv, start_of_chunk, chks_orig[i]->seq_num, chks_orig[i]->num_chunks, 1);
			if (num_chks == -1)
				return -2;
			/* If we didn't remove all the nodes, some of them must have running jobs on them */
			if (num_chks != chks_orig[i]->num_chunks)
				return -1;

		} else {
			int tot_num_chunks = 0;
			int chk_diff = chks_orig[i]->num_chunks - chks[j]->num_chunks;
			if (chk_diff > 0) {
				/* First remove nodes that are unavailable */
				num_chks = ralter_remove_nodes(resv, start_of_chunk, chks_orig[i]->seq_num, chk_diff, -1);
				if (num_chks == -1)
					return -2;

				if (chk_diff - num_chks > 0) {
					/* Once we have all the unavailable nodes, fill in the rest with available nodes */
					tot_num_chunks = ralter_remove_nodes(resv, start_of_chunk, chks_orig[i]->seq_num, chk_diff - num_chks, 0);
					if (tot_num_chunks == -1)
						return -2;
				}
				tot_num_chunks += num_chks;
				/* We weren't able to find enough nodes without runniung jobs on them.  Fail the alter */
				if (tot_num_chunks < chk_diff)
					return -1;
			}

			j++;
		}
		for (k = start_of_chunk; k < cnt && resv->resv->orig_nspec_arr[k]->chk->seq_num == chks_orig[i]->seq_num; k++)
			;
		start_of_chunk = k;
	}
	i = 0;
	while (resv->resv->orig_nspec_arr[i] != NULL) {
		if (resv->resv->orig_nspec_arr[i]->ninfo == NULL) {
			free_nspec(resv->resv->orig_nspec_arr[i]);
			for (j = i; resv->resv->orig_nspec_arr[j] != NULL; j++)
				resv->resv->orig_nspec_arr[j] = resv->resv->orig_nspec_arr[j + 1];
		} else
			i++;
	}
	free_nspecs(resv->nspec_arr);
	resv->nspec_arr = combine_nspec_array(resv->resv->orig_nspec_arr);

	return 1;
}

/**
 * @brief
 * 		Checks the state of all vnodes associated to a reservation and reports
 * 		if there are any unavailable.  The ninfo ptr of the nspec is cleared for
 * 		unavailable vnodes so they can be left out when creating the execselect
 *
 * @param[in]	resv	-	resv to check
 *
 * @return	1	: there is an unavailable vnode
 * @retval	0	: all nodes are up and happy
 * @retval	-1	: there is a running job on one of the unavailable nodes.
 * @retval	-2	: can't map original chunks to resv_nodes
 * @retval	-3	: error
 */
int
check_vnodes_unavailable(resource_resv *resv)
{
	int ret = 0;
	int i;
	int has_down_node = 0;
	int has_superchunk = 0;
	int num_chunks = 0;
	nspec **chunks_to_remove = NULL;
	int del_i = 0;

	if (resv == NULL || resv->nspec_arr == NULL)
		return -3;

	for (i = 0; resv->resv->orig_nspec_arr[i] != NULL; i++) {
		if (!resv->resv->orig_nspec_arr[i]->end_of_chunk)
			has_superchunk = 1;
		num_chunks++;
	}

	if (has_superchunk) {
		if ((chunks_to_remove = static_cast<nspec **>(malloc((num_chunks + 1) * sizeof(nspec *)))) == NULL) {
			log_err(errno, __func__, MEM_ERR_MSG);
			return -3;
		}
	}

	for (i = 0; resv->resv->orig_nspec_arr[i] != NULL; i++) {

		/* If the original select chunks haven't been mapped to the resv_nodes and the reservation is running, we can't continue */
		if (resv->resv->is_running && resv->resv->orig_nspec_arr[i]->chk == NULL) {
			free(chunks_to_remove);
			return -2;
		}

		/* Part of a superchunk we've already handled */
		if (resv->resv->orig_nspec_arr[i]->ninfo == NULL)
			continue;

		ret = check_down_running(resv, i);
		/* running jobs on unavailable nodes in chunk */
		if (ret == -2) {
			free(chunks_to_remove);
			return -1;
		}
		/* unavailable nodes in chunk */
		else if (ret == -1) {
			int j;
			has_down_node = 1;
			for (j = i; resv->resv->orig_nspec_arr[j] != NULL && !resv->resv->orig_nspec_arr[j]->end_of_chunk; j++) {
				resv->resv->orig_nspec_arr[j]->ninfo = NULL;
				if (has_superchunk)
					chunks_to_remove[del_i++] = resv->resv->orig_nspec_arr[j];
			}
			/* We ran into an error where we ran into the end of the array before the end of the chunk
			 * The entire chunk is in chunks_to_remove, so we'll just remove it.
			 */
			if (resv->resv->orig_nspec_arr[j] == NULL)
				break;

			resv->resv->orig_nspec_arr[j]->ninfo = NULL;
			/*
			 * The nspec_arr only has consumable resources.  When replacing a vnode we need
			 * to make sure to replace it with the right type of node matching the non-consumables
			 */
			free_resource_req_list(resv->resv->orig_nspec_arr[j]->resreq);
			resv->resv->orig_nspec_arr[j]->resreq = dup_resource_req_list(resv->resv->orig_nspec_arr[j]->chk->req);
			resv->resv->orig_nspec_arr[j]->sub_seq_num = 1;
		} else /* Node is available for use */
			resv->resv->orig_nspec_arr[i]->sub_seq_num = 0;
	}

	if (has_superchunk) {
		chunks_to_remove[del_i] = NULL;

		for(i = 0; chunks_to_remove[i] != NULL; i++) {
			free_nspec(chunks_to_remove[i]);
			remove_ptr_from_array(resv->resv->orig_nspec_arr, chunks_to_remove[i]);
		}
	}
	if (has_down_node == 1)
		/* Move all the specific chunks ahead of the generic chunks.
		 * This will ensure that we get all the nodes back we need to,
		 * before we start looking for replacements
		 */
		qsort(resv->resv->orig_nspec_arr, count_array(resv->resv->orig_nspec_arr), sizeof(nspec *), cmp_nspec_by_sub_seq);

	free(chunks_to_remove);

	return has_down_node;
}

/**
 * @brief
 * 		Release resources allocated to a reservation
 *
 * @param[in]	resresv	-	the reservation
 *
 * @return	void
 */
void
release_nodes(resource_resv *resresv)
{
	free_nodes(resresv->resv->resv_nodes);
	resresv->resv->resv_nodes = NULL;

	free(resresv->ninfo_arr);
	resresv->ninfo_arr = NULL;

	free_nspecs(resresv->nspec_arr);
	resresv->nspec_arr = NULL;

	free_nspecs(resresv->resv->orig_nspec_arr);
	resresv->resv->orig_nspec_arr = NULL;

	if (resresv->nodepart_name != NULL) {
		free(resresv->nodepart_name);
		resresv->nodepart_name = NULL;
	}
}

/**
 * @brief
 *		create_resv_nodes - create a node info array by copying the
 *			    nodes and setting available resources to
 *			    only the ones assigned to the reservation
 *
 * @param[in]	nspec_arr -	the nspec array created from the resv_nodes
 * @param[in]	sinfo     -	server reservation belongs too
 * 
 * @par 	the new duplicated nodes will NOT be part of sinfo->nodes.  
 *		We include the server because it is expected that every node 
 *		have a server pointer and parts of the code gets 
 *		cranky if it isn't there.
 *
 */
node_info **
create_resv_nodes(nspec * *nspec_arr, server_info *sinfo)
{
	node_info **nodes = NULL;
	schd_resource *res;
	resource_req *req;
	int i;

	if (nspec_arr != NULL) {
		for (i = 0; nspec_arr[i] != NULL; i++)
			;
		nodes = static_cast<node_info **>(malloc((i + 1) * sizeof(node_info *)));
		if (nodes != NULL) {
			for (i = 0; nspec_arr[i] != NULL; i++) {
				node_info *svr_node;
				/* Find the node in sinfo->nodes because nspec->ninfo might be pointed to an old resv node */
				svr_node = find_node_by_indrank(sinfo->nodes, nspec_arr[i]->ninfo->node_ind, nspec_arr[i]->ninfo->rank);
				nodes[i] = dup_node_info(svr_node, sinfo, DUP_INDIRECT);
				nodes[i]->svr_node = svr_node;

				set_node_info_state(nodes[i], ND_free);

				free(nodes[i]->run_resvs_arr);
				nodes[i]->run_resvs_arr = NULL;

				req = nspec_arr[i]->resreq;
				while (req != NULL) {
					res = find_alloc_resource(nodes[i]->res, req->def);

					if (res != NULL) {
						if (res->indirect_res != NULL)
							res = res->indirect_res;
						res->avail = req->amount;
						memcpy(&(res->type), &(req->type), sizeof(struct resource_type));
						if (res->type.is_consumable)
							res->assigned = 0; /* clear now, set later */
					}
					req = req->next;
				}
			}
			nodes[i] = NULL;
		}
	}
	return nodes;
}

/**
 * @brief end a running reservation on its nodes.  This is used so we can assign
 * 		them back to a running reservation when reconfirming it.
 *
 * @param[in] resv - the reservation
 * @param[in] all_nodes - The server's nodes list
 *
 * @return void
 */
void
end_resv_on_nodes(resource_resv *resv, node_info **all_nodes)
{
	node_info **resv_nodes = NULL;
	node_info *ninfo = NULL;
	int i;

	resv_nodes = resv->ninfo_arr;
	for (i = 0; resv_nodes[i] != NULL; i++) {
		ninfo = find_node_by_indrank(all_nodes, resv_nodes[i]->node_ind, resv_nodes[i]->rank);
		update_node_on_end(ninfo, resv, NULL);
	}
}

/**
 * @brief - adjust resources on nodes belonging to a reservation that is
 *	    running and is either degraded or being altered.  We need to free
 * 	    the resources on these nodes so the resources are available for
 * 	    check_nodes() to assign back to the reservation.
 *
 * @param[in] resv - reservation to alter nodes for
 * @param[in] sinfo - PBS universe
 *
 */

void
release_running_resv_nodes(resource_resv *resv, server_info *sinfo)
{
	int i = 0;
	node_info **resv_nodes = NULL;
	node_info *ninfo = NULL;

	if (resv == NULL || sinfo == NULL )
		return;
	if (resv->resv->is_running && (resv->resv->resv_substate == RESV_DEGRADED || resv->resv->resv_state == RESV_BEING_ALTERED)) {
		resv_nodes = resv->ninfo_arr;
		for (i = 0; resv_nodes[i] != NULL; i++) {
			ninfo = find_node_by_indrank(sinfo->nodes, resv_nodes[i]->node_ind, resv_nodes[i]->rank);
			update_node_on_end(ninfo, resv, NULL);
		}
		sinfo->pset_metadata_stale = 1;
	}
}

/**
 * 	@brief determine if the scheduler will attempt to confirm a reservation
 *
 * 	@return int
 * 	@retval 1 - will attempt confirmation
 * 	@retval 0 - will not attempt confirmation
 */
int will_confirm(resource_resv *resv, time_t server_time) {
	/* If the reservation is unconfirmed OR is degraded and not running, with a
	 * retry time that is in the past, then the reservation has to be
	 * respectively confirmed and reconfirmed.
	 */
	if (resv->resv->resv_state == RESV_UNCONFIRMED ||
		resv->resv->resv_state == RESV_BEING_ALTERED ||
		((resv->resv->resv_substate == RESV_DEGRADED || resv->resv->resv_substate == RESV_IN_CONFLICT) &&
		resv->resv->retry_time != UNSPECIFIED &&
		resv->resv->retry_time <= server_time))
		return 1;

	return 0;
}

int remove_resv_from_server(resource_resv **resvs, resource_resv *resresv) {
	resource_resv *sresv;
	int num_resvs_removed = 0;

	if (is_resresv_running(resresv))
		for (int i = 0; resresv->ninfo_arr[i] != NULL; i++)
			resresv->ninfo_arr[i]->num_run_resv--;

	if (resresv->resv->is_standing && resresv->resv->resv_state != RESV_UNCONFIRMED) {
		/* Remove all occurrences of the reservation */
		sresv = resresv;
		do {
			resource_resv *tmpresv;
			sresv->resv->resv_queue->resv = NULL;
			remove_ptr_from_array(resvs, sresv);
			tmpresv = find_resource_resv(resvs, sresv->name);
			delete sresv;
			num_resvs_removed++;
			sresv = tmpresv;
		} while (sresv != NULL);
	} else {
		remove_ptr_from_array(resvs, resresv);
		delete resresv;
		num_resvs_removed = 1;
	}
	return num_resvs_removed;
}