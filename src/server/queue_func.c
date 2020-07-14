/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
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
 * @file    queue_func.c
 *
 *@brief
 * 		queue_func.c - various functions dealing with queues
 *
 * Included functions are:
 *	que_alloc()	- allocacte and initialize space for queue structure
 *	que_free()	- free queue structure
 *	que_purge()	- remove queue from server
 *	find_queuebyname() - find a queue with a given name
 #ifdef NAS localmod 075
 *	find_resvqueuebyname() - find a reservation queue, given resv name
 #endif localmod 075
 *	get_dfltque()	- get default queue
 * 	qstart_action() - determine accrue type for all jobs in queue
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/param.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "log.h"
#include "attribute.h"
#include "server_limits.h"
#include "server.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "pbs_error.h"
#include "sched_cmds.h"
#include "pbs_db.h"
#include "pbs_nodes.h"
#include "pbs_sched.h"
#include "pbs_idx.h"

/* Global Data */

extern char     *msg_err_unlink;
extern char	*path_queues;
extern struct    server server;
extern pbs_list_head svr_queues;
extern time_t	 time_now;
extern long	 svr_history_enable;
#ifndef PBS_MOM
extern pbs_db_conn_t	*svr_db_conn;
#endif

/**
 * @brief
 * 		que_alloc - allocate space for a queue structure and initialize
 *		attributes to "unset"
 *
 * @param[in]	name	- queue name
 *
 * @return	pbs_queue *
 * @retval	null	- space not available.
 */

pbs_queue *
que_alloc(char *name)
{
	int i;
	pbs_queue *pq;

	pq = (pbs_queue *) malloc(sizeof(pbs_queue));
	if (pq == NULL) {
		log_err(errno, __func__, "no memory");
		return NULL;
	}
	(void) memset((char *) pq, (int) 0, (size_t) sizeof(pbs_queue));

	pq->qu_qs.qu_type = QTYPE_Unset;
	pq->newobj = 1;
	CLEAR_HEAD(pq->qu_jobs);
	CLEAR_LINK(pq->qu_link);

	snprintf(pq->qu_qs.qu_name, sizeof(pq->qu_qs.qu_name), "%s", name);
	if (pbs_idx_insert(queues_idx, pq->qu_qs.qu_name, pq) != PBS_IDX_RET_OK) {
		log_eventf(PBSEVENT_ERROR | PBSEVENT_FORCE, PBS_EVENTCLASS_QUEUE, LOG_ERR,
			   "Failed to add queue in index %s", pq->qu_qs.qu_name);
		free(pq);
		return NULL;
	}
	append_link(&svr_queues, &pq->qu_link, pq);
	server.sv_qs.sv_numque++;

	/* set the working attributes to "unspecified" */
	for (i = 0; i < (int) QA_ATR_LAST; i++)
		clear_attr(&pq->qu_attr[i], &que_attr_def[i]);

	return (pq);
}

/**
 * @brief
 *		que_free - free queue structure and its various sub-structures
 *		Queue ACL's are regular attributes that are stored in the DB
 *		and not in separate files.
 *
 * @param[in]	pq	- The pointer to the queue to free
 *
 */
void
que_free(pbs_queue *pq)
{
	int i;
	attribute *pattr;
	attribute_def *pdef;
	key_value_pair *pkvp = NULL;

	/* remove any malloc working attribute space */
	for (i = 0; i < (int) QA_ATR_LAST; i++) {
		pdef = &que_attr_def[i];
		pattr = &pq->qu_attr[i];

		pdef->at_free(pattr);
	}

	/* free default chunks set on queue */
	pkvp = pq->qu_seldft;
	if (pkvp) {
		for (i = 0; i < pq->qu_nseldft; ++i) {
			free((pkvp + i)->kv_keyw);
			free((pkvp + i)->kv_val);
		}
		free(pkvp);
	}

	/* now free the main structure */
	server.sv_qs.sv_numque--;
	delete_link(&pq->qu_link);
	if (pbs_idx_delete(queues_idx, pq->qu_qs.qu_name) != PBS_IDX_RET_OK)
		log_eventf(PBSEVENT_ERROR | PBSEVENT_FORCE, PBS_EVENTCLASS_QUEUE, LOG_ERR,
			   "Failed to delete queue %s from index", pq->qu_qs.qu_name);
	(void) free(pq);
}

/**
 * @brief
 *		que_purge - purge queue from system
 *		The queue is dequeued, the queue file is unlinked.
 *		If the queue contains any jobs, the purge is not allowed.
 *		Eventually the queue is deleted from the database
 *
 * @param[in]	pque	- The pointer to the queue to purge
 *
 * @return	error code
 * @retval	0	- queue purged or queue not valid
 * @retval	PBSE_OBJBUSY	- queue deletion not allowed
 */
int
que_purge(pbs_queue *pque)
{
	pbs_db_obj_info_t   obj;
	pbs_db_que_info_t   dbque;
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;

	/*
	 * If the queue (pque) is not valid, then nothing to
	 * do, just return 0.
	 */
	if (pque == NULL)
		return (0);

	/* are there any jobs still in the queue */
	if (pque->qu_numjobs != 0) {
		/*
		 * If the queue still has job(s), check if the SERVER
		 * is configured for history info and all the jobs in
		 * queue are history jobs. If yes, then allow queue
		 * deletion otherwise return PBSE_OBJBUSY.
		 */
		if (svr_history_enable) { /* SVR histconf chk */

			job 	*pjob = NULL;
			job 	*nxpjob = NULL;

			pjob = (job *)GET_NEXT(pque->qu_jobs);
			while (pjob) {
				/*
				 * If it is not a history job (MOVED/FINISHED), then
				 * return with PBSE_OBJBUSY error.
				 */
				if ((pjob->ji_qs.ji_state != JOB_STATE_MOVED) &&
					(pjob->ji_qs.ji_state != JOB_STATE_FINISHED) &&
					(pjob->ji_qs.ji_state != JOB_STATE_EXPIRED))
					return (PBSE_OBJBUSY);
				pjob = (job *)GET_NEXT(pjob->ji_jobque);
			}
			/*
			 * All are history jobs, unlink all of them from queue.
			 * Update the number of jobs in the queue and their state
			 * count as the queue is going to be purged. No job(s)
			 * should point to the queue to be purged, make the queue
			 * header pointer of job(pjob->ji_qhdr) to NULL.
			 */
			pjob = (job *)GET_NEXT(pque->qu_jobs);
			while (pjob) {
				nxpjob = (job *)GET_NEXT(pjob->ji_jobque);
				delete_link(&pjob->ji_jobque);
				--pque->qu_numjobs;
				--pque->qu_njstate[pjob->ji_qs.ji_state];
				pjob->ji_qhdr = NULL;
				pjob = nxpjob;
			}
		} else {
			return (PBSE_OBJBUSY);
		}
	}

	/* delete queue from database */
	strcpy(dbque.qu_name, pque->qu_qs.qu_name);
	obj.pbs_db_obj_type = PBS_DB_QUEUE;
	obj.pbs_db_un.pbs_db_que = &dbque;
	if (pbs_db_delete_obj(conn, &obj) != 0) {
		(void)sprintf(log_buffer,
			"delete of que %s from datastore failed",
			pque->qu_qs.qu_name);
		log_err(errno, "queue_purge", log_buffer);
	}
	que_free(pque);

	return (0);
}

/**
 * @brief
 * 		find_queuebyname() - find a queue by its name
 *
 * @param[in]	quename	- queue name
 *
 * @return	pbs_queue *
 */

pbs_queue *
find_queuebyname(char *quename)
{
	char *at;
	pbs_queue *pque = NULL;
	int rc = PBS_IDX_RET_FAIL;

	if (quename == NULL || quename[0] == '\0')
		return NULL;

	at = strchr(quename, (int)'@');	/* strip off server (fragment) */
	if (at)
		*at = '\0';

	rc = pbs_idx_find(queues_idx, (void **)&quename, (void **)&pque, NULL);
	if (at)
		*at = '@'; /* restore '@' server portion */
	if (rc == PBS_IDX_RET_OK)
		return pque;
	return NULL;
}

#ifdef NAS /* localmod 075 */
/**
 * @brief
 * 		find_resvqueuebyname() - find a queue by the name of its reservation
 *
 * @param[in]	quename	- queue name.
 *
 * @return	pbs_queue *
 */
pbs_queue *
find_resvqueuebyname(char *quename)
{
	char *pc;
	pbs_queue *pque;
	char qname[PBS_MAXDEST + 1];

	(void)strncpy(qname, quename, PBS_MAXDEST);
	qname[PBS_MAXDEST] = '\0';
	pc = strchr(qname, (int)'@');	/* strip off server (fragment) */
	if (pc)
		*pc = '\0';
	for (pque = (pbs_queue *)GET_NEXT(svr_queues);
		pque != NULL; pque = (pbs_queue *)GET_NEXT(pque->qu_link)) {
		if (pque->qu_resvp != NULL
			&& (strcmp(qname, pque->qu_resvp->ri_wattr[(int)RESV_ATR_resv_name].at_val.at_str) == 0))
			break;
	}
	if (pc)
		*pc = '@';	/* restore '@' server portion */
	return (pque);
}
#endif /* localmod 075 */

/**
 * @brief
 * 	find_resv() - find reservation resc_resv struct by its ID or queue name
 *
 *	Search list of all server resc_resv structs for one with given
 *	reservation id or reservation queue name
 *
 * @param[in]	id_or_quename - reservation ID or queue name
 *
 * @return	pointer to resc_resv struct
 * @retval	NULL	- not found
 */
resc_resv *
find_resv(char *id_or_quename)
{
	char *dot = NULL;
	resc_resv *presv = NULL;
	void *prid;

	if (id_or_quename == NULL || id_or_quename[0] == '\0')
		return NULL;

	if ((dot = strchr(id_or_quename, (int)'.')) != 0)
		*dot = '\0';

	prid = id_or_quename + 1; /* ignore first char, as index key doesn't have it */
	if (pbs_idx_find(resvs_idx, &prid, (void **)&presv, NULL) != PBS_IDX_RET_OK) {
		if (dot)
			*dot = '.';
		return NULL;
	}
	if (dot)
		*dot = '.';
	return presv;
}

/**
 * @brief
 * 		get_dftque - get the default queue (if declared)
 *
 * @return	pbs_queue *
 */

pbs_queue *
get_dfltque(void)
{
	pbs_queue *pq = NULL;

	if (server.sv_attr[SVR_ATR_dflt_que].at_flags & ATR_VFLAG_SET)
		pq = find_queuebyname(server.sv_attr[SVR_ATR_dflt_que].at_val.at_str);
	return (pq);
}

/**
 * @brief
 * 		queuestart_action - when queue is stopped or started,
 *		for all jobs in queue and determine their accrue type
 * 		action function for QA_ATR_started.
 *
 * @param[in]	pattr	- pointer to special attributes of an Array Job
 * @param[in]	pobject	- queue which is stopped or started
 * @param[in]	actmode	- not used.
 *
 * @return	int
 * @retval	0	- success
 */
int
queuestart_action(attribute *pattr, void *pobject, int actmode)
{
	job 	*pj;		/* pointer to job */
	long	oldtype;
	long 	newaccruetype = -1;	/* if determining accrue type */
	pbs_queue *pque = (pbs_queue *) pobject;
	pbs_sched *psched;

	if ((pque != NULL) && (server.sv_attr[SVR_ATR_EligibleTimeEnable].at_val.at_long == 1)) {

		if (pattr->at_val.at_long == 0) { /* started = OFF */
			/* queue stopped, start accruing eligible time */
			/* running jobs and jobs accruing ineligible time are exempted */
			/* jobs accruing eligible time are also exempted */

			pj = (job*)GET_NEXT(pque->qu_jobs);

			while (pj != NULL) {

				oldtype = pj->ji_wattr[(int)JOB_ATR_accrue_type].at_val.at_long;

				if (oldtype != JOB_RUNNING && oldtype != JOB_INELIGIBLE &&
					oldtype != JOB_ELIGIBLE) {

					/* determination of accruetype not required here */
					(void)update_eligible_time(JOB_ELIGIBLE, pj);
				}

				pj = (job*)GET_NEXT(pj->ji_jobque);
			}

		} else { 			/* started = ON */
			/* determine accrue type and accrue time */

			pj = (job*)GET_NEXT(pque->qu_jobs);

			while (pj != NULL) {

				oldtype = pj->ji_wattr[(int)JOB_ATR_accrue_type].at_val.at_long;

				if (oldtype != JOB_RUNNING && oldtype != JOB_INELIGIBLE &&
					oldtype != JOB_ELIGIBLE) {

					newaccruetype = determine_accruetype(pj);
					update_eligible_time(newaccruetype, pj);
				}

				pj = (job*)GET_NEXT(pj->ji_jobque);
			}

			/* if scheduling = True, notify scheduler to start */
			if (server.sv_attr[SVR_ATR_scheduling].at_val.at_long) {
				if (find_assoc_sched_pque(pque, &psched))
					set_scheduler_flag(SCH_SCHEDULE_STARTQ, psched);
				else {
					sprintf(log_buffer, "No scheduler associated with the partition %s", pque->qu_attr[QA_ATR_partition].at_val.at_str);
					log_err(-1, __func__, log_buffer);
				}
			}
		}
	}

	return 0;
}


/**
 * @brief
 * 		action routine for the queue's "partition" attribute
 *
 * @param[in]	pattr	-	attribute being set
 * @param[in]	pobj	-	Object on which attribute is being set
 * @param[in]	actmode	-	the mode of setting, recovery or just alter
 *
 * @return	error code
 * @retval	PBSE_NONE	-	Success
 * @retval	!PBSE_NONE	-	Failure
 *
 */
int
action_queue_partition(attribute *pattr, void *pobj, int actmode)
{
	int i;

	if (actmode == ATR_ACTION_RECOV)
		return PBSE_NONE;

	if (((pbs_queue *)pobj)->qu_qs.qu_type  == QTYPE_RoutePush)
		return PBSE_ROUTE_QUE_NO_PARTITION;

	if (strcmp(pattr->at_val.at_str, DEFAULT_PARTITION) == 0)
		return PBSE_DEFAULT_PARTITION;

	for (i=0; i < svr_totnodes; i++) {
		if (pbsndlist[i]->nd_pque) {
			if (strcmp(pbsndlist[i]->nd_pque->qu_qs.qu_name, ((pbs_queue *) pobj)->qu_qs.qu_name) == 0) {
				if ((pbsndlist[i]->nd_attr[ND_ATR_partition].at_flags) & ATR_VFLAG_SET &&
						(pattr->at_flags) & ATR_VFLAG_SET)
				if (strcmp(pbsndlist[i]->nd_attr[ND_ATR_partition].at_val.at_str,
						pattr->at_val.at_str))
					return PBSE_INVALID_PARTITION_QUE;
			}
		}
	}

	return PBSE_NONE;
}
