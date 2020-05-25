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
 * @file	int_rdrpy.c
 * @brief
 * Read the reply to a batch request.
 * A reply structure is allocated and cleared.
 * The reply is read and decoded into the structure.
 * The reply structure is returned.
 *
 * The caller MUST free the reply structure by calling
 * PBS_FreeReply().
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "libpbs.h"
#include "dis.h"


/**
 * @brief read a batch reply from the given socket
 *
 * @param[in] sock - The socket fd to read from
 * @param[out] rc  - Return DIS error code
 *
 * @return Batch reply structure
 * @retval  !NULL - Success
 * @retval   NULL - Failure
 *
 */
struct batch_reply *
PBSD_rdrpy_sock(int sock, int *rc)
{
	struct batch_reply *reply;
	time_t old_timeout;

	*rc = DIS_SUCCESS;
	/* clear any prior error message */
	if ((reply = (struct batch_reply *)malloc(sizeof(struct batch_reply))) == 0) {
		pbs_errno = PBSE_SYSTEM;
		return NULL;
	}
	(void)memset(reply, 0, sizeof(struct batch_reply));

	DIS_tcp_funcs();
	old_timeout = pbs_tcp_timeout;
	if (pbs_tcp_timeout < PBS_DIS_TCP_TIMEOUT_LONG)
		pbs_tcp_timeout = PBS_DIS_TCP_TIMEOUT_LONG;

	if ((*rc = decode_DIS_replyCmd(sock, reply)) != 0) {
		(void)free(reply);
		pbs_errno = PBSE_PROTOCOL;
		return NULL;
	}
	dis_reset_buf(sock, DIS_READ_BUF);
	pbs_tcp_timeout = old_timeout;

	pbs_errno = reply->brp_code;
	return reply;
}

/**
 * @brief read a batch reply from the given connecction index
 *
 * @param[in] c - The connection index to read from
 *
 * @return DIS error code
 * @retval   DIS_SUCCESS  - Success
 * @retval  !DIS_SUCCESS  - Failure
 */
struct batch_reply *
PBSD_rdrpy(int c)
{
	int rc;
	struct batch_reply *reply;
	int sock;
	int index;

	/* clear any prior error message */

	if (set_conn_errtxt(c, NULL) != 0) {
		pbs_errno = PBSE_SYSTEM;
		return NULL;
	}

	sock = get_svr_shard_connection(c, OTHERS, NULL, &index);
	if (sock == -1) {
		pbs_errno = PBSE_NOCONNECTION;
		return NULL;
	}

	reply = PBSD_rdrpy_sock(sock, &rc);
	if (reply == NULL) {
		if (set_conn_errno(c, PBSE_PROTOCOL) != 0) {
			pbs_errno = PBSE_SYSTEM;
			return NULL;
		}
		if (set_conn_errtxt(c, dis_emsg[rc]) != 0) {
			pbs_errno = PBSE_SYSTEM;
			return NULL;
		}
		return NULL;
	}
	if (set_conn_errno(c, reply->brp_code) != 0) {
		pbs_errno = reply->brp_code;
		return NULL;
	}
	pbs_errno = reply->brp_code;

	if (reply->brp_choice == BATCH_REPLY_CHOICE_Text) {
		if (reply->brp_un.brp_txt.brp_str != NULL) {
			if (set_conn_errtxt(c, reply->brp_un.brp_txt.brp_str) != 0) {
				pbs_errno = PBSE_SYSTEM;
				return NULL;
			}
		}
	}
	return reply;
}

/*
 * PBS_FreeReply - Free a batch_reply structure allocated in PBS_rdrpy()
 *
 *	Any additional allocated substructures pointed to from the
 *	reply structure are freed, then the base struture itself is gone.
 */

void
PBSD_FreeReply(reply)
struct batch_reply *reply;
{
	struct brp_select   *psel;
	struct brp_select   *pselx;
	struct brp_cmdstat  *pstc;
	struct brp_cmdstat  *pstcx;
	struct attrl        *pattrl;
	struct attrl	    *pattrx;

	if (reply == 0)
		return;
	if (reply->brp_choice == BATCH_REPLY_CHOICE_Text) {
		if (reply->brp_un.brp_txt.brp_str) {
			(void)free(reply->brp_un.brp_txt.brp_str);
			reply->brp_un.brp_txt.brp_str = NULL;
			reply->brp_un.brp_txt.brp_txtlen = 0;
		}

	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_Select) {
		psel = reply->brp_un.brp_select;
		while (psel) {
			pselx = psel->brp_next;
			(void)free(psel);
			psel = pselx;
		}

	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_Status) {
		pstc = reply->brp_un.brp_statc;
		while (pstc) {
			pstcx = pstc->brp_stlink;
			pattrl = pstc->brp_attrl;
			while (pattrl) {
				pattrx = pattrl->next;
				if (pattrl->name)
					(void)free(pattrl->name);
				if (pattrl->resource)
					(void)free(pattrl->resource);
				if (pattrl->value)
					(void)free(pattrl->value);
				(void)free(pattrl);
				pattrl = pattrx;
			}
			(void)free(pstc);
			pstc = pstcx;
		}
	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_RescQuery) {
		(void)free(reply->brp_un.brp_rescq.brq_avail);
		(void)free(reply->brp_un.brp_rescq.brq_alloc);
		(void)free(reply->brp_un.brp_rescq.brq_resvd);
		(void)free(reply->brp_un.brp_rescq.brq_down);
	} else if (reply->brp_choice == BATCH_REPLY_CHOICE_PreemptJobs) {
		(void)free(reply->brp_un.brp_preempt_jobs.ppj_list);
	}

	(void)free(reply);
}
