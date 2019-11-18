/*
 * Copyright (C) 1994-2018 Altair Engineering, Inc.
 * For more information, contact Altair at www.altair.com.
 *
 * This file is part of the PBS Professional ("PBS Pro") software.
 *
 * Open Source License Information:
 *
 * PBS Pro is free software. You can redistribute it and/or modify it under the
 * terms of the GNU Affero General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option) any
 * later version.
 *
 * PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial License Information:
 *
 * For a copy of the commercial license terms and conditions,
 * go to: (http://www.pbspro.com/UserArea/agreement.html)
 * or contact the Altair Legal Department.
 *
 * Altair’s dual-license business model allows companies, individuals, and
 * organizations to create proprietary derivative works of PBS Pro and
 * distribute them - whether embedded or bundled with other software -
 * under a commercial license agreement.
 *
 * Use of Altair’s trademarks, including but not limited to "PBS™",
 * "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
 * trademark licensing policies.
 *
 */

/**
 * @file    queue_recov_db.c
 *
 * @brief
 *		queue_recov_db.c - This file contains the functions to record a queue
 *		data structure to database and to recover it from database.
 *
 *		The data is recorded in the database
 *
 *	The following public functions are provided:
 *		que_save_db()   - save queue to database
 *		que_recov_db()  - recover (read) queue from database
 *		svr_to_db_que()	- Load a database queue object from a server queue object
 *		db_to_svr_que()	- Load a server queue object from a database queue object
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <sys/param.h>
#include "pbs_ifl.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "server_limits.h"
#include "list_link.h"
#include "attribute.h"
#include "job.h"
#include "reservation.h"
#include "queue.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "pbs_db.h"


#ifndef PBS_MOM
extern pbs_db_conn_t	*svr_db_conn;
#endif

#ifdef NAS /* localmod 005 */
/* External Functions Called */
extern int save_attr_db(pbs_db_conn_t *conn, pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef, struct attribute *pattr,
	int numattr, int newparent);
extern int recov_attr_db(pbs_db_conn_t *conn,
	void *parent,
	pbs_db_attr_info_t *p_attr_info,
	struct attribute_def *padef,
	struct attribute *pattr,
	int limit,
	int unknown);
#endif /* localmod 005 */

/**
 * @brief
 *		Load a database queue object from a server queue object
 *
 * @param[in]	pque	- Address of the queue in the server
 * @param[out]	pdbque  - Address of the database queue object
 *
 *@return 0      Success
 *@return !=0    Failure
 */
static int
svr_to_db_que(pbs_queue *pque, pbs_db_que_info_t *pdbque, int updatetype)
{
	pdbque->qu_name[sizeof(pdbque->qu_name) - 1] = '\0';
	strncpy(pdbque->qu_name, pque->qu_qs.qu_name, sizeof(pdbque->qu_name));
	pdbque->qu_type = pque->qu_qs.qu_type;
	pdbque->qu_deleted = pque->qu_qs.qu_deleted;

	if (updatetype != PBS_UPDATE_DB_QUICK) {
		if ((encode_attr_db(que_attr_def, pque->qu_attr,
			(int)QA_ATR_LAST, &pdbque->attr_list, 1)) != 0) /* encode all attributes */
			return -1;
	}

	return 0;
}

/**
 * @brief
 *		Load a server queue object from a database queue object
 *
 * @param[out]	pque	- Address of the queue in the server
 * @param[in]	pdbque	- Address of the database queue object
 *
 *@return 0      Success
 *@return !=0    Failure
 */
int
db_to_svr_que(pbs_queue *pque, pbs_db_que_info_t *pdbque, int act_reqd)
{
	pque->qu_qs.qu_name[sizeof(pque->qu_qs.qu_name) - 1] = '\0';
	strncpy(pque->qu_qs.qu_name, pdbque->qu_name, sizeof(pque->qu_qs.qu_name));
	pque->qu_qs.qu_type = pdbque->qu_type;
	pque->qu_qs.qu_deleted = pdbque->qu_deleted;
	strcpy(pque->qu_creattm, pdbque->qu_creattm);
	strcpy(pque->qu_savetm, pdbque->qu_savetm);

	if ((decode_attr_db(pque, &pdbque->attr_list, que_attr_def,
		pque->qu_attr, (int) QA_ATR_LAST, 0, act_reqd)) != 0)
		return -1;

	return 0;
}

/**
 * @brief
 *	Save a queue to the database
 *
 * @param[in]	pque  - Pointer to the queue to save
 * @param[in]	mode:
 *		QUE_SAVE_FULL - Save full queue information (update)
 *		QUE_SAVE_NEW  - Save new queue information (insert)
 *
 * @return      Error code
 * @retval	0 - Success
 * @retval	1 - Failure
 *
 */
int
que_save_db(pbs_queue *pque, int mode)
{
	pbs_db_que_info_t	dbque;
	pbs_db_obj_info_t	obj;
	pbs_db_query_options_t opts;
	pbs_db_conn_t		*conn = (pbs_db_conn_t *) svr_db_conn;
	int savetype = PBS_UPDATE_DB_FULL;
	int rc;
	rc = 0;

	rc = svr_to_db_que(pque, &dbque, savetype);
	if (rc != 0)
		goto db_err;

	obj.pbs_db_obj_type = PBS_DB_QUEUE;
	obj.pbs_db_un.pbs_db_que = &dbque;

	if (mode == QUE_SAVE_NEW) {
		savetype = PBS_INSERT_DB;
	}

	rc = pbs_db_save_obj(conn, &obj, savetype);
	if (rc != 0) {
		if (rc == UNIQUE_KEY_VIOLATION) {
			/* delete the existing queue with same name */
			strcpy(dbque.qu_name, pque->qu_qs.qu_name);
			if (pbs_db_delete_obj(conn, &obj, &opts) != 0) {
				(void)sprintf(log_buffer,
					"deletetion of que %s from datastore failed after unique key violation",
					pque->qu_qs.qu_name);
				log_err(errno, "que_save_db", log_buffer);
				return -1;
			}
			pbs_db_reset_obj(&obj);
			/* old queue entry has been deleted, now try to save again */
			(void)que_save_db(pque, QUE_SAVE_NEW);
		} else {
			goto db_err;
		}
	}

	strcpy(pque->qu_savetm, dbque.qu_savetm);

	pbs_db_reset_obj(&obj);

	pque->qu_last_refresh_time = time(NULL);
	return (0);

db_err:
	/* free the attribute list allocated by encode_attrs */
	free(dbque.attr_list.attributes);

	strcpy(log_buffer, "que_save failed ");
	if (conn->conn_db_err != NULL)
		strncat(log_buffer, conn->conn_db_err, LOG_BUF_SIZE - strlen(log_buffer) - 1);
	log_err(-1, __func__, log_buffer);

	panic_stop_db(log_buffer);
	return (-1);
}

/**
 * @brief
 *		Recover a queue from the database
 *
 * @param[in]	qname	- Name of the queue to recover
 *
 * @return	The recovered queue structure
 * @retval	NULL	- Failure
 * @retval	!NULL	- Success - address of recovered queue returned
 *
 */
pbs_queue *
que_recov_db(char *qname, pbs_queue *pq, int lock)
{
	pbs_db_que_info_t	dbque;
	pbs_db_obj_info_t	obj;
	pbs_db_conn_t		*conn = (pbs_db_conn_t *) svr_db_conn;
	int rc;
	int act_reqd = 0;

	obj.pbs_db_obj_type = PBS_DB_QUEUE;
	obj.pbs_db_un.pbs_db_que = &dbque;
	strcpy(dbque.qu_name, qname);

	if (pq) {
		if (memcache_good(&pq->trx_status, lock))
			return pq;
		strcpy(dbque.qu_savetm, pq->qu_savetm);
	} else {
		pq = que_alloc(qname);  /* allocate & init queue structure space */
		if (pq == NULL) {
			log_err(-1, "que_recov", "que_alloc failed");
			return NULL;
		}
		dbque.qu_savetm[0] = '\0';
		act_reqd = 1;
	}

	/* read in job fixed sub-structure */
	rc = pbs_db_load_obj(conn, &obj, lock);
	if (rc == -1)
		goto db_err;

	/* if queue is marked as deleted in db then remove it from cache also */
	if(dbque.qu_deleted == 1) {
		if(pq) {
			sprintf(log_buffer, "Queue marked as deleted");
			log_event(PBSEVENT_DEBUG3, PBS_EVENTCLASS_QUEUE, LOG_DEBUG, qname, log_buffer);
			/* TODO: Remove all the jobs, related to this queue in the system */
			que_free(pq);
			pbs_db_reset_obj(&obj);
		}
		return NULL;
	}

	if (rc == -2) {
		memcache_update_state(&pq->trx_status, lock);
		/* queue is refreshed now with latest data */
		pq->qu_last_refresh_time = time(NULL);
		return pq;
	}
	
	if (db_to_svr_que(pq, &dbque, act_reqd) != 0)
		goto db_err;

	pbs_db_reset_obj(&obj);
	memcache_update_state(&pq->trx_status, lock);

	/* queue is refreshed now with latest data */
	pq->qu_last_refresh_time = time(NULL);
	/* all done recovering the queue */
	return (pq);

db_err:
	sprintf(log_buffer, "Failed to load queue %s", qname);
	if (pq) {
		/* TODO: Remove all the jobs, related to this queue in the system if exists */
		que_free(pq);
	}

	return NULL;
}

/**
 * @brief
 *	Refresh/retrieve queue from database and add it into AVL tree if not present
 *
 *	@param[in]	dbque - The pointer to the wrapper queue object of type pbs_db_que_info_t
 *  @param[in]  refreshed - To count the no. of queues refreshed
 *
 * @return	The recovered queue
 * @retval	NULL - Failure
 * @retval	!NULL - Success, pointer to queue structure recovered
 *
 */
pbs_queue *
refresh_queue(pbs_db_que_info_t *dbque, int *refreshed) {

	*refreshed = 0;
	char  *pc;
	pbs_queue *pque = NULL;
	char   qname[PBS_MAXDEST + 1];

	(void)strncpy(qname, dbque->qu_name, PBS_MAXDEST);
	qname[PBS_MAXDEST] ='\0';
	pc = strchr(qname, (int)'@');	/* strip off server (fragment) */
	if (pc)
		*pc = '\0';
	/* get the old pointer of the queue, if queue is already in memory */
	pque = (pbs_queue *)GET_NEXT(svr_queues);
	while (pque != NULL) {
		if (strcmp(qname, pque->qu_qs.qu_name) == 0)
			break;
		pque = (pbs_queue *)GET_NEXT(pque->qu_link);
	}
	if (pc)
		*pc = '@';	/* restore '@' server portion */

	if (pque) {
		if (strcmp(dbque->qu_savetm, pque->qu_savetm) != 0) {
			/* if queue had changed in db */
			*refreshed = 1;
			return que_recov_db(dbque->qu_name, pque, 0);
		}
	} else {
		/* if queue is not in memory, fetch it from db */
		if ((pque = que_recov_db(dbque->qu_name, pque, 0)) != NULL) {
			append_link(&svr_queues, &pque->qu_link, pque);
			*refreshed = 1;
		}
	}
	return pque;
}
