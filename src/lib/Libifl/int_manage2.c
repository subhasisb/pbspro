/*
 * Copyright (C) 1994-2020 Altair Engineering, Inc.
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
 * @file	int_manage2.c
 * @brief
 * 	The send-request side of the PBS_manager function
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"
#include "net_connect.h"
#include "tpp.h"


/**
 * @brief
 *      -encode a Manager Batch Request
 *
 * @par Functionality:
 *              This request is used for most operations where an object is being
 *              created, deleted, or altered.
 *
 * @param[in] c - socket descriptor
 * @param[in] command - command type
 * @param[in] objtype - object type
 * @param[in] objname - object name
 * @param[in] aoplp - pointer to attropl structure(list)
 * @param[in] prot - PROT_TCP or PROT_TPP
 * @param[in] msgid - message id
 *
 * @return      int
 * @retval      DIS_SUCCESS(0)  success
 * @retval      error code      error
 *
 */
int
PBSD_mgr_put(int c, int function, int command, int objtype, char *objname, struct attropl *aoplp, char *extend, int prot, char **msgid)
{
	int rc;
	int sock;
	int index;

	if (prot == PROT_TCP) {
		int shardtype = OTHERS;
		char *shardhint = NULL;
		if (objtype == MGR_OBJ_JOB) {
			shardtype = JOB;
			shardhint = objname;
		}
		else if (objtype == MGR_OBJ_RESV) {
			shardtype = RESERVATION;
			shardhint = objname;
		}

		sock = get_svr_shard_connection(c, shardtype, shardhint, &index);
		if (sock == -1) {
			if (set_conn_errtxt(c, pbse_to_txt(PBSE_NOCONNECTION)) != 0)
				return (pbs_errno = PBSE_SYSTEM);
			return (pbs_errno = PBSE_NOCONNECTION);
		}
		DIS_tcp_funcs();
	} else {
		sock = c;
		if ((rc = is_compose_cmd(sock, IS_CMD, msgid)) != DIS_SUCCESS)
			return rc;
	}

	if ((rc = encode_DIS_ReqHdr(sock, function, pbs_current_user)) ||
		(rc = encode_DIS_Manage(sock, command, objtype, objname, aoplp)) ||
		(rc = encode_DIS_ReqExtend(sock, extend))) {
		if (prot == PROT_TCP) {
			if (set_conn_errtxt(c, dis_emsg[rc]) != 0)
				return (pbs_errno = PBSE_SYSTEM);
		}
		return (pbs_errno = PBSE_PROTOCOL);
	}


	pbs_errno = PBSE_NONE;
	if (dis_flush(sock)) {
		return (pbs_errno = PBSE_PROTOCOL);
	}
	if (prot == PROT_TPP) {
		return pbs_errno;
	}
	return 0;
}
