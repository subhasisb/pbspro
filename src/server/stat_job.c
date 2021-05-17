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

#include <pbs_config.h>   /* the master config generated by configure */

/*
 *
 * @brief
 * 	Functions which support the Status Job Batch Request.
 *
 */
#include <sys/types.h>
#include <stdlib.h>
#include "libpbs.h"
#include <ctype.h>
#include <time.h>
#include <sys/time.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "work_task.h"
#include "pbs_error.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "pbs_ifl.h"
#include "ifl_internal.h"


/* Global Data Items: */

extern attribute_def job_attr_def[];
extern int	     resc_access_perm; /* see encode_resc() in attr_fn_resc.c */
extern struct server server;
extern char	     statechars[];
extern time_t time_now;

/* convenience macro to check for diffstat and print attribute name and value to logs */
#ifdef DEBUG
#define LOG_DIFFSTAT_ATTR(x, from_tm, msg) if (!IS_FULLSTAT(from_tm)) { \
							log_eventf(PBSEVENT_DEBUG4, PBS_EVENTCLASS_SERVER, LOG_DEBUG, \
								msg_daemonname, \
								"%s %s.%s=%s", \
								(msg), \
								(x)->al_atopl.name, \
								(x)->al_atopl.resource ? (x)->al_atopl.resource : "", \
								(x)->al_atopl.value ? (x)->al_atopl.value : ""); \
						}
#else
#define LOG_DIFFSTAT_ATTR(x, from_tm, msg)
#endif

/**
 * @brief
 * 		svrcached - either link in (to phead) a cached svrattrl struct which is
 *		pointed to by the attribute, or if the cached struct isn't there or
 *		is out of date, then replace it with a new svrattrl structure.
 * @par
 *		When replacing, unlink and delete old one if the reference count goes
 *		to zero.
 *
 * @param[in,out] pat - attribute structure which contains a cached svrattrl struct
 * @param[in,out] phead - list of new attribute values
 * @param[in] pdef - attribute for any parent object
 * @param[in] from_tm - consider attributes that were updated after from_
 *
 * @note
 *	If an attribute has the ATR_DFLAG_HIDDEN flag set, then no
 *	need to obtain and cache new svrattrl values.
 */

static void
svrcached(attribute *pat, pbs_list_head *phead, attribute_def *pdef, struct timeval from_tm)
{
	svrattrl *working = NULL;
	svrattrl *wcopy;
	svrattrl *encoded;

	if (pdef == NULL)
		return;

	if ((pdef->at_flags & ATR_DFLAG_HIDDEN) &&
		(get_sattr_long(SVR_ATR_show_hidden_attribs) == 0)) {
		return;
	}

	if (pat->at_flags & ATR_VFLAG_MODCACHE) {
		/* free old cache value if the value has changed */
		free_svrcache(pat);
		encoded = NULL;
	} else {
		if (resc_access_perm & PRIV_READ)
			encoded = pat->at_priv_encoded;
		else
			encoded = pat->at_user_encoded;
	}

	if ((encoded == NULL) || (pat->at_flags & ATR_VFLAG_MODCACHE)) {
		if (is_attr_set(pat)) {
			/* encode and cache new svrattrl structure */
			(void)pdef->at_encode(pat, phead, pdef->at_name, NULL, ATR_ENCODE_CLIENT, &working);	
			if (resc_access_perm & PRIV_READ)
				pat->at_priv_encoded = working;
			else
				pat->at_user_encoded = working;

			pat->at_flags &= ~ATR_VFLAG_MODCACHE;
			while (working) {
				LOG_DIFFSTAT_ATTR(working, from_tm, "Adding attr (update)");
				working->al_refct++;	/* incr ref count */
				working = working->al_sister;
			}
		} else if (!IS_FULLSTAT(from_tm)) {
			/* attribute was not set, but was modified, so add "unset" in case of diffstat */
			encode_unset(pat, phead, pdef->at_name, NULL, ATR_ENCODE_CLIENT, &working); /* we must encode empty attrs since MODCACHE flag will vanish */
			LOG_DIFFSTAT_ATTR(working, from_tm, "Adding attr (unset)");
		}
	} else if (encoded) {
		/* can use the existing cached svrattrl structure */

		working = encoded;
		if (working->al_refct < 2) {
			while (working) {
				CLEAR_LINK(working->al_link);
				if (phead != NULL) {
					append_link(phead, &working->al_link, working);
					LOG_DIFFSTAT_ATTR(working, from_tm, "Adding attr (cached)");
				}
				working->al_refct++;	/* incr ref count */
				working = working->al_sister;
			}
		} else {
			/*
			 * already linked in, must make a copy to link
			 * NOTE: the copy points to the original's data
			 * so it should be freed by itself, hence the
			 * ref count is set to 1 and the sisters are not
			 * linked in
			 */
			while (working) {
				wcopy = malloc(sizeof(struct svrattrl));
				if (wcopy) {
					*wcopy = *working;
					working = working->al_sister;
					CLEAR_LINK(wcopy->al_link);
					if (phead != NULL) {
						append_link(phead, &wcopy->al_link, wcopy);
						LOG_DIFFSTAT_ATTR(wcopy, from_tm, "Adding attr (copy)");
					}
					wcopy->al_refct = 1;
					wcopy->al_sister = NULL;
				}
			}
		}
	} else if (!IS_FULLSTAT(from_tm)) {
		/* not encoded, so value is not set, but in diffstat we need to encode empty anyway, if attr timestamp has changed */
		encode_unset(pat, phead, pdef->at_name, NULL, ATR_ENCODE_CLIENT, &working); /* we must encode empty attrs since MODCACHE flag will vanish */
		LOG_DIFFSTAT_ATTR(working, from_tm, "Adding attr (unset)");
	}
}

/*
 * status_attrib - add each requested or all attributes to the status reply
 *
 * @param[in,out]	pal 	-	specific attributes to status
 * @param[in]		pidx 	-	Search index of the attribute array
 * @param[in]		padef	-	attribute definition structure
 * @param[in,out]	pattr	-	attribute structure
 * @param[in]		limit	-	limit on size of def array
 * @param[in]		priv	-	user-client privilege
 * @param[in,out]	phead	-	pbs_list_head
 * @param[out]		bad 	-	RETURN: index of first bad attribute
 * @param[in]		from_tm - 	consider attributes that were updated after from_tm
 *
 * @return	int
 * @retval	0	: success
 * @retval	-1	: on error (bad attribute)
 */

int
status_attrib(svrattrl *pal, void *pidx, attribute_def *padef, attribute *pattr, int limit, int priv, pbs_list_head *phead, int *bad, struct timeval from_tm)
{
	int   index;
	int   nth = 0;

	priv &= (ATR_DFLAG_RDACC | ATR_DFLAG_SvWR);  /* user-client privilege */
	resc_access_perm = priv;  /* pass privilege to encode_resc()	*/

	/* for each attribute asked for or for all attributes, add to reply */

	if (pal) {		/* client specified certain attributes */
		while (pal) {
			++nth;
			index = find_attr(pidx, padef, pal->al_name);
			if (index < 0) {
				*bad = nth;
				return (-1);
			}
			if ((padef+index)->at_flags & priv) {
				if (TS_NEWER((pattr+index)->update_tm, from_tm)) 
					svrcached(pattr+index, phead, padef+index, from_tm);
			}
			pal = (svrattrl *)GET_NEXT(pal->al_link);
		}
	} else {	/* non specified, return all readable attributes */
		for (index = 0; index < limit; index++) {
			if ((padef+index)->at_flags & priv) {
				if (TS_NEWER((pattr+index)->update_tm, from_tm))
					svrcached(pattr+index, phead, padef+index, from_tm);
			}
		}
	}
	return (0);
}

/**
 * @brief
 * 		status_job - Build the status reply for a single job, regular or Array,
 *		but not a subjob of an Array Job.
 *
 * @param[in,out]	pjob	 -	ptr to job to status
 * @param[in]	    preq	 -	request structure
 * @param[in]	    pal	     -	specific attributes to status
 * @param[in,out]	pstathd	 -	RETURN: head of list to append status to
 * @param[out]	    bad	     -	RETURN: index of first bad attribute
 * @param[in]		dohistjobs	return history jobs?
 * @param[in]		dosubjobs -	flag to expand a Array job to include all subjobs
 * @param[in]       from_tm -  timestamp from when to stat jobs, in case of diffstat
 *
 * @return	int
 * @retval	0	: success
 * @retval	PBSE_PERM	: client is not authorized to status the job
 * @retval	PBSE_SYSTEM	: memory allocation error
 * @retval	PBSE_NOATTR	: attribute error
 */

int
status_job(job *pjob, struct batch_request *preq, 
	svrattrl *pal, pbs_list_head *pstathd, int *bad,  
	int dohistjobs, int dosubjobs, struct timeval from_tm)
{
	struct brp_status *pstat;
	long oldtime = 0;
	int revert_state_r = 0;
	int rc = 0;
	char state;
	struct batch_reply *preply = &preq->rq_reply;

	/* first flush if buffer is full */
	if (preply->brp_count >= MAX_JOBS_PER_REPLY) {
		rc = reply_send_status_part(preq);
		if (rc != PBSE_NONE)
			return rc;
	}

	/* see if the client is authorized to status this job */

	if (! get_sattr_long(SVR_ATR_query_others))
		if (svr_authorize_jobreq(preq, pjob))
			return (PBSE_PERM);

	/* if history job and not asking for them */
	state = get_job_state(pjob);
	if (!dohistjobs
			&& ((state == JOB_STATE_LTR_FINISHED) || (state == JOB_STATE_LTR_MOVED) || (state == JOB_STATE_LTR_EXPIRED))) {
		
		/* if this is diffstat from scheduler, then we must send the history as "deleted" job */
		if ((dosubjobs == 2) && (!IS_FULLSTAT(from_tm)))
			return (status_deleted_id(pjob->ji_qs.ji_jobid, preply));
		else
			return (PBSE_NONE);
	}

	/* calc eligible time on the fly and return, don't save. */
	if (get_sattr_long(SVR_ATR_EligibleTimeEnable) == TRUE) {
		if (get_jattr_long(pjob, JOB_ATR_accrue_type) == JOB_ELIGIBLE) {
			oldtime = get_jattr_long(pjob, JOB_ATR_eligible_time);
			set_jattr_l_slim(pjob, JOB_ATR_eligible_time,
					time_now - get_jattr_long(pjob, JOB_ATR_sample_starttime), INCR);
		}
	} else {
		/* eligible_time_enable is off so, clear set flag so that eligible_time and accrue type dont show */
		if (is_jattr_set(pjob, JOB_ATR_eligible_time))
			mark_jattr_not_set(pjob, JOB_ATR_eligible_time);

		if (is_jattr_set(pjob, JOB_ATR_accrue_type))
			mark_jattr_not_set(pjob, JOB_ATR_accrue_type);
	}

	/* allocate reply structure and fill in header portion */

	pstat = (struct brp_status *)malloc(sizeof(struct brp_status));
	if (pstat == NULL)
		return (PBSE_SYSTEM);
	CLEAR_LINK(pstat->brp_stlink);

	/* 
	 * In sched selstat, sched wants to see only real jobs (passes 'S' in extend), 
	 * not queued subjobs, in this case dosubjobs == 2.
	 *  
	 * So do not set the brp_objtype = MGR_OBJ_JOBARRAY_PARENT
	 * or MGR_OBJ_SUBJOB, which will lead IFL to expand queued jobs
	 * in the case of dosubjobs == 2
	 * 
	 * However, client statjobs, they might pass 't' to extend, dosubjobs == 1 in
	 * that case, and clients do want the queued jobs to be expanded by IFL.
	 * 
	 * So, set the brp_objtype = MGR_OBJ_JOBARRAY_PARENT/MGR_OBJ_SUBJOB in the case
	 * dosubjobs == 1
	 * 
	 */
	if ((dosubjobs == 1) && (preq->rq_type == PBS_BATCH_StatusJob)) {
		if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_ArrayJob) != 0) {
			pstat->brp_objtype = MGR_OBJ_JOBARRAY_PARENT;
			/* in case of diffstat, we must also send the array_indices_remaining attribute, so that
			 * IFL can synthesize updates for queued subjobs as well
			 * To do that, we touch the timestamp of the Array_indicies_remaining attribute
			 */
			gettimeofday(&(get_jattr(pjob, JOB_ATR_array_indices_remaining)->update_tm), NULL);
		} else if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_SubJob) != 0)
			pstat->brp_objtype = MGR_OBJ_SUBJOB;
		else
			pstat->brp_objtype = MGR_OBJ_JOB;
	} else {
		pstat->brp_objtype = MGR_OBJ_JOB; /* this is dosubjobs == 0 or 2, so set nothing for expansion */
	}
	
	(void)strcpy(pstat->brp_objname, pjob->ji_qs.ji_jobid);
	CLEAR_HEAD(pstat->brp_attr);

	/* Temporarily set suspend/user suspend states for the stat */
	if (check_job_state(pjob, JOB_STATE_LTR_RUNNING)) {
		if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_Suspend) {
			set_job_state(pjob, JOB_STATE_LTR_SUSPENDED);
			revert_state_r = 1;
		} else if (pjob->ji_qs.ji_svrflags & JOB_SVFLG_Actsuspd) {
			set_job_state(pjob, JOB_STATE_LTR_USUSPENDED);
			revert_state_r = 1;
		}
	}

	/* add attributes to the status reply */
	*bad = 0;
	if (!IS_FULLSTAT(from_tm)) {
		log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, pjob->ji_qs.ji_jobid, "Added job to diffstat reply");
	}
	rc = status_attrib(pal, job_attr_idx, job_attr_def, pjob->ji_wattr, JOB_ATR_LAST, preq->rq_perm, &pstat->brp_attr, bad, from_tm);
	if ((rc != 0) || (!GET_NEXT(pstat->brp_attr))) {
		free(pstat);
		if (IS_FULLSTAT(from_tm))
			return (PBSE_NOATTR);
		else
			return (PBSE_NONE);
	}

	append_link(pstathd, &pstat->brp_stlink, pstat);
	preq->rq_reply.brp_count++;

	if (get_sattr_long(SVR_ATR_EligibleTimeEnable) != 0) {
		if (get_jattr_long(pjob, JOB_ATR_accrue_type) == JOB_ELIGIBLE)
			set_jattr_l_slim(pjob, JOB_ATR_eligible_time, oldtime, SET);
	}

	if (revert_state_r)
		set_job_state(pjob, JOB_STATE_LTR_RUNNING);

	return (0);
}

/**
 * @brief
 * 		status_subjob - status a single subjob (of an Array Job)
 *		Works by statusing the parrent unless subjob is actually running.
 *
 * @param[in,out]	pjob	-	ptr to parent Array
 * @param[in]		preq	-	request structure
 * @param[in]		pal	-	specific attributes to status
 * @param[in]		subj	-	if not = -1 then include subjob [n]
 * @param[in,out]	pstathd	-	RETURN: head of list to append status to
 * @param[out]		bad	-	RETURN: index of first bad attribute
 * @param[in]		dohistjos - return history jobs?
 * @param[in]		dosubjobs -	flag to expand a Array job to include all subjobs
 * @param[in]       from_tm -  timestamp from when to stat jobs, in case of diffstat
 *
 * @return	int
 * @retval	0	: success
 * @retval	PBSE_PERM	: client is not authorized to status the job
 * @retval	PBSE_SYSTEM	: memory allocation error
 * @retval	PBSE_IVALREQ	: something wrong with the flags
 */
int
status_subjob(job *pjob, struct batch_request *preq, svrattrl *pal, int subj, pbs_list_head *pstathd, int *bad, 
	int dohistjobs, int dosubjobs, struct timeval from_tm)
{
	int limit = (int)JOB_ATR_LAST;
	struct brp_status *pstat;
	job *psubjob; /* ptr to job to status */
	char realstate;
	int rc = 0;
	char *old_subjob_comment = NULL;
	char sjst;
	int sjsst;
	char *objname;
	struct batch_reply *preply = &preq->rq_reply;

	/* first flush if buffer is full */
	if (preply->brp_count >= MAX_JOBS_PER_REPLY) {
		rc = reply_send_status_part(preq);
		if (rc != PBSE_NONE)
			return rc;
	}

	/* see if the client is authorized to status this job */

	if (! get_sattr_long(SVR_ATR_query_others))
		if (svr_authorize_jobreq(preq, pjob))
			return (PBSE_PERM);

	if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_ArrayJob) == 0)
		return PBSE_IVALREQ;

	/* if subjob job obj exists, use real job structure */

	psubjob = get_subjob_and_state(pjob, subj, &sjst, &sjsst);
	if (psubjob)
		return status_job(psubjob, preq, pal, pstathd, bad, dohistjobs, dosubjobs, from_tm);

	if (sjst == JOB_STATE_LTR_UNKNOWN)
		return PBSE_UNKJOBID;

	/* otherwise we fake it with info from the parent      */
	/* allocate reply structure and fill in header portion */

	objname = create_subjob_id(pjob->ji_qs.ji_jobid, subj);
	if (objname == NULL)
		return PBSE_SYSTEM;

	/* for the general case, we don't want to include the parent's */
	/* array related attrbutes as they belong only to the Array    */
	if (pal == NULL)
		limit = JOB_ATR_array;
	pstat = (struct brp_status *)malloc(sizeof(struct brp_status));
	if (pstat == NULL)
		return (PBSE_SYSTEM);
	
	CLEAR_LINK(pstat->brp_stlink);

	if (dosubjobs == 1) /* this is the if 't' in extend, allow IFL to expand quued jobs */
		pstat->brp_objtype = MGR_OBJ_SUBJOB;
	else
		pstat->brp_objtype = MGR_OBJ_JOB;

	(void)strcpy(pstat->brp_objname, objname);
	CLEAR_HEAD(pstat->brp_attr);
	append_link(pstathd, &pstat->brp_stlink, pstat);
	preq->rq_reply.brp_count++;

	/* add attributes to the status reply */

	*bad = 0;

	/*
	 * fake the job state and comment by setting the parent job's state
	 * and comment to that of the subjob
	 */
	realstate = get_job_state(pjob);
	set_job_state(pjob, sjst);

	log_eventf(PBSEVENT_DEBUG3, PBS_EVENTCLASS_JOB, LOG_DEBUG, pjob->ji_qs.ji_jobid, "Added subjob with state=%c to diffstat reply", sjst);

	if (sjst == JOB_STATE_LTR_EXPIRED || sjst == JOB_STATE_LTR_FINISHED) {
		if (sjsst == JOB_SUBSTATE_FINISHED) {
			if (is_jattr_set(pjob, JOB_ATR_Comment)) {
				old_subjob_comment = strdup(get_jattr_str(pjob, JOB_ATR_Comment));
				if (old_subjob_comment == NULL)
					return (PBSE_SYSTEM);
			}
			if (set_jattr_str_slim(pjob, JOB_ATR_Comment, "Subjob finished", NULL)) {
				return (PBSE_SYSTEM);
			}
		} else if (sjsst == JOB_SUBSTATE_FAILED) {
			if (is_jattr_set(pjob, JOB_ATR_Comment)) {
				old_subjob_comment = strdup(get_jattr_str(pjob, JOB_ATR_Comment));
				if (old_subjob_comment == NULL)
					return (PBSE_SYSTEM);
			}
			if (set_jattr_str_slim(pjob, JOB_ATR_Comment, "Subjob failed", NULL)) {
				return (PBSE_SYSTEM);
			}
		} else if (sjsst == JOB_SUBSTATE_TERMINATED) {
			if (is_jattr_set(pjob, JOB_ATR_Comment)) {
				old_subjob_comment = strdup(get_jattr_str(pjob, JOB_ATR_Comment));
				if (old_subjob_comment == NULL)
					return (PBSE_SYSTEM);
			}
			if (set_jattr_str_slim(pjob, JOB_ATR_Comment, "Subjob terminated", NULL)) {
				return (PBSE_SYSTEM);
			}
		}
	}

	/* when eligible_time_enable is off,				      */
	/* clear the set flag so that eligible_time and accrue_type dont show */
	if (get_sattr_long(SVR_ATR_EligibleTimeEnable) == 0) {
		if (is_jattr_set(pjob, JOB_ATR_eligible_time))
			mark_jattr_not_set(pjob, JOB_ATR_eligible_time);

		if (is_jattr_set(pjob, JOB_ATR_accrue_type))
			mark_jattr_not_set(pjob, JOB_ATR_accrue_type);
	}

	if (status_attrib(pal, job_attr_idx, job_attr_def, pjob->ji_wattr, limit, preq->rq_perm, &pstat->brp_attr, bad, from_tm)) {
		if (IS_FULLSTAT(from_tm))
			rc =  PBSE_NOATTR; /* normal stat, return error */
	}

	/* Set the parent state back to what it really is */
	set_job_state(pjob, realstate);

	/* Set the parent comment back to what it really is */
	if (old_subjob_comment != NULL) {
		if (set_jattr_str_slim(pjob, JOB_ATR_Comment, old_subjob_comment, NULL)) {
			return (PBSE_SYSTEM);
		}

		free(old_subjob_comment);
	}

	return (rc);
}
