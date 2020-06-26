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
 * @file    queue_recov_db.c
 *
 * @brief
 *		queue_recov_db.c - This file contains the functions to record a queue
 *		data structure to database and to recover it from database.
 *
 *		The data is recorded in the database
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

/**
 * @brief
 *		convert queue structure to DB format
 *
 * @param[in]	pque	- Address of the queue in the server
 * @param[out]	pdbque  - Address of the database queue object
 *
 * @retval   -1  Failure
 * @retval	>=0 What to save: 0=nothing, OBJ_SAVE_NEW or OBJ_SAVE_QS
 */
static int
que_to_db(pbs_queue *pque, pbs_db_que_info_t *pdbque)
{
	int savetype = 0;

	strcpy(pdbque->qu_name, pque->qu_qs.qu_name);
	pdbque->qu_type = pque->qu_qs.qu_type;

	if ((encode_attr_db(que_attr_def, pque->qu_attr, (int)QA_ATR_LAST, &pdbque->db_attr_list, 0)) != 0)
		return -1;

	if (pque->newobj) /* object was never saved or loaded before */
		savetype |= (OBJ_SAVE_NEW | OBJ_SAVE_QS);

	if (compare_obj_hash(&pque->qu_qs, sizeof(pque->qu_qs), pque->qs_hash) == 1) {
		savetype |= OBJ_SAVE_QS;
		pdbque->qu_type = pque->qu_qs.qu_type;
	}

	return savetype;
}

/**
 * @brief
 *		convert from database to queue structure
 *
 * @param[out]	pque	- Address of the queue in the server
 * @param[in]	pdbque	- Address of the database queue object
 *
 *@return 0      Success
 *@return !=0    Failure
 */
static int
db_to_que(pbs_queue *pque, pbs_db_que_info_t *pdbque)
{
	strcpy(pque->qu_qs.qu_name, pdbque->qu_name);
	pque->qu_qs.qu_type = pdbque->qu_type;

	if ((decode_attr_db(pque, &pdbque->db_attr_list, que_attr_def, pque->qu_attr, (int) QA_ATR_LAST, 0)) != 0)
		return -1;

	compare_obj_hash(&pque->qu_qs, sizeof(pque->qu_qs), pque->qs_hash);

	pque->newobj = 0;

	return 0;
}

/**
 * @brief
 *	Save a queue to the database
 *
 * @param[in]	pque  - Pointer to the queue to save
 *
 * @return      Error code
 * @retval	0 - Success
 * @retval	1 - Failure
 *
 */
int
que_save_db(pbs_queue *pque)
{
	pbs_db_que_info_t	dbque = {{0}};
	pbs_db_obj_info_t	obj;
	pbs_db_conn_t *conn = (pbs_db_conn_t *) svr_db_conn;
	int savetype;
	int rc = -1;

	if ((savetype = que_to_db(pque, &dbque)) == -1)
		goto done;
	
	obj.pbs_db_obj_type = PBS_DB_QUEUE;
	obj.pbs_db_un.pbs_db_que = &dbque;

	if ((rc = pbs_db_save_obj(conn, &obj, savetype)) == 0)
		pque->newobj = 0;

done:
	free_db_attr_list(&dbque.db_attr_list);
	
	if (rc != 0) {
		log_errf(PBSE_INTERNAL, __func__, "Failed to save queue %s %s", pque->qu_qs.qu_name, (conn->conn_db_err)? conn->conn_db_err : "");
		panic_stop_db(log_buffer);
	}
	return rc;
}

/**
 * @brief
 *		Recover a queue from the database
 *
 * @param[in]	qname	- Name of the queue to recover
 * @param[out]  pq - Queue pointer, if any, to be updated
 *
 * @return	The recovered queue structure
 * @retval	NULL	- Failure
 * @retval	!NULL	- Success - address of recovered queue returned
 *
 */
pbs_queue *
que_recov_db(char *qname, pbs_queue	*pq)
{
	pbs_queue *pque = NULL;
	pbs_db_que_info_t	dbque = {{0}};
	pbs_db_obj_info_t	obj;
	pbs_db_conn_t		*conn = (pbs_db_conn_t *) svr_db_conn;
	int rc = -1;

	if (!pq) {
		if ((pque = que_alloc(qname)) == NULL) {
			log_err(-1, __func__, "que_alloc failed");
			return NULL;
		}
		pq = pque;
	}

	strcpy(dbque.qu_name, qname);
	obj.pbs_db_obj_type = PBS_DB_QUEUE;
	obj.pbs_db_un.pbs_db_que = &dbque;

	rc = pbs_db_load_obj(conn, &obj);
	if (rc == -2)
		return pq; /* no change in que, return the same pq */

	if (rc == 0)
		rc = db_to_que(pq, &dbque);
	else
		log_errf(PBSE_INTERNAL, __func__, "Failed to load queue %s %s", qname, (conn->conn_db_err)? conn->conn_db_err : "");
		
	free_db_attr_list(&dbque.db_attr_list);

	if (rc != 0) {
		pq = NULL; /* so we return NULL */

		if (pque)
			que_free(pque); /* free if we allocated here */
		
	}
	return pq;
}
