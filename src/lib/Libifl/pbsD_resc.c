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
 * @file	pbs_resc.c
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <stdio.h>
#include "libpbs.h"
#include "dis.h"
#include "pbs_ecl.h"


/* following structure is used by pbsD_resc.c, functions totpool and usepool */
struct node_pool {
	int nodes_avail;
	int nodes_alloc;
	int nodes_resrv;
	int nodes_down;
	char *resc_nodes;
};

/**
 * @brief
 *	-frees the node pool
 *
 * @param[in] np - pointer to node pool list
 *
 * @return	Void
 *
 */
void
free_node_pool(struct node_pool *np)
{
	if (np) {
		if (np->resc_nodes)
			free(np->resc_nodes);
		free(np);
	}
}

/**
 * @brief
 * 	-encode_DIS_resc() - encode a resource related request,
 *	Used by pbs_rescquery(), pbs_rescreserve() and pbs_rescfree()
 *
 * @param[in] sock - socket fd
 * @param[in] rlist - pointer to resource list
 * @param[in] ct - count of query strings
 * @param[in] rh - resource handle
 *
 * @return	int
 * @retval	0	success
 * @retval	!0	error
 *
 */

static int
encode_DIS_Resc(int sock, char **rlist, int ct, pbs_resource_t rh)
{
	int    i;
	int    rc;

	if ((rc = diswsi(sock, rh)) == 0) {  /* resource reservation handle */

		/* next send the number of resource strings */

		if ((rc = diswui(sock, ct)) == 0) {

			/* now send each string (if any) */

			for (i = 0; i < ct; ++i) {
				if ((rc = diswst(sock, *(rlist + i))) != 0)
					break;
			}
		}
	}
	return rc;
}

/**
 * @brief
 * 	-PBS_resc() - internal common code for sending resource requests
 *
 * @par Functionality:
 *	Formats and sends the requests for pbs_rescquery(), pbs_rescreserve(),
 *	and pbs_rescfree().   Note, while the request is overloaded for all
 *	three, each has its own expected reply format.
 *
 * @param[in] c - communication handle
 * @param[in] reqtype - request type
 * @param[in] rescl- pointer to resource list
 * @param[in] ct - count of query strings
 * @param[in] rh - resource handle
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */
static int
PBS_resc(int c, int reqtype, char **rescl, int ct, pbs_resource_t rh)
{
	int rc;
	int sock;

	sock = get_svr_shard_connection(c, -1, NULL);
	if (sock == -1) {
		pbs_errno = PBSE_NOCONNECTION;
		return pbs_errno;
	}

	/* setup DIS support routines for following DIS calls */

	DIS_tcp_funcs();

	if ((rc = encode_DIS_ReqHdr(sock, reqtype, pbs_current_user)) ||
		(rc = encode_DIS_Resc(sock, rescl, ct, rh)) ||
		(rc = encode_DIS_ReqExtend(sock, NULL))) {
		if (set_conn_errtxt(c, dis_emsg[rc]) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_PROTOCOL;
		}
		return (pbs_errno);
	}
	if (dis_flush(sock)) {
		return (pbs_errno = PBSE_PROTOCOL);
	}
	return (0);
}

/**
 * @brief
 * 	-pbs_rescquery() - query the availability of resources
 *
 * @param[in] c - communication handle
 * @param[in] resclist - list of queries
 * @param[in] num_resc - number in list
 * @param[out] available - number available per query
 * @param[out] allocated - number allocated per query
 * @param[out] reserved - number reserved  per query
 * @param[out] down - number down/off  per query
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */

int
pbs_rescquery(int c, char **resclist, int num_resc,
	int *available, int *allocated, int *reserved, int *down)
{
	int i;
	struct batch_reply *reply;
	int rc = 0;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	if (resclist == 0) {
		if (set_conn_errno(c, PBSE_RMNOPARAM) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_RMNOPARAM;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}

	/* send request */

	if ((rc = PBS_resc(c, PBS_BATCH_Rescq, resclist,
		num_resc, (pbs_resource_t)0)) != 0) {
		(void)pbs_client_thread_unlock_connection(c);
		return rc;
	}

	/* read in reply */

	reply = PBSD_rdrpy(c);
	if ((rc = get_conn_errno(c)) == PBSE_NONE &&
		reply->brp_choice == BATCH_REPLY_CHOICE_RescQuery) {
		struct	brp_rescq	*resq = &reply->brp_un.brp_rescq;

		if (resq == NULL || num_resc != resq->brq_number) {
			rc = PBSE_IRESVE;
			if (set_conn_errno(c, PBSE_IRESVE) != 0) {
				pbs_errno = PBSE_SYSTEM;
			} else {
				pbs_errno = PBSE_IRESVE;
			}
			goto done;
		}

		/* copy in available and allocated numbers */

		for (i=0; i<num_resc; i++) {
			available[i] = resq->brq_avail[i];
			allocated[i] = resq->brq_alloc[i];
			reserved[i]  = resq->brq_resvd[i];
			down[i]	     = resq->brq_down[i];
		}
	}

done:
	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return (rc);
}

/**
 * @brief
 * 	-pbs_reserve() - reserver resources
 *
 * @param[in] c - communication handle
 * @param[in] rl - list of resources
 * @param[in] num_resc - number of items in list
 * @param[in] prh - ptr to resource reservation handle
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */

int
pbs_rescreserve(int c, char **rl, int num_resc, pbs_resource_t *prh)
{
	int	rc;
	struct batch_reply *reply;


	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	if (rl == NULL) {
		if (set_conn_errno(c, PBSE_RMNOPARAM) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_RMNOPARAM;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}
	if (prh == NULL) {
		if (set_conn_errno(c, PBSE_RMNOPARAM) != 0) {
			pbs_errno = PBSE_SYSTEM;
		} else {
			pbs_errno = PBSE_RMNOPARAM;
		}
		(void)pbs_client_thread_unlock_connection(c);
		return pbs_errno;
	}
	/* send request */

	if ((rc = PBS_resc(c, PBS_BATCH_ReserveResc, rl, num_resc, *prh)) != 0) {
		(void)pbs_client_thread_unlock_connection(c);
		return (rc);
	}

	/*
	 * now get reply, if reservation successful, the reservation handle,
	 * pbs_resource_t, is in the  aux field
	 */

	reply = PBSD_rdrpy(c);

	if (((rc = get_conn_errno(c)) == PBSE_NONE) ||
		(rc == PBSE_RMPART)) {
		*prh = reply->brp_auxcode;
	}
	PBSD_FreeReply(reply);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return (rc);
}

/**
 * @brief
 * 	-pbs_release() - release a resource reservation
 *
 * @par Note:
 *	To encode we send same info as for reserve except that the resource
 *	list is empty.
 *
 * @param[in] c - connection handle
 * @param[in] rh - resorce handle
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */

int
pbs_rescrelease(int c, pbs_resource_t rh)
{
	struct batch_reply *reply;
	int	rc;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return pbs_errno;

	/* lock pthread mutex here for this connection */
	/* blocking call, waits for mutex release */
	if (pbs_client_thread_lock_connection(c) != 0)
		return pbs_errno;

	if ((rc = PBS_resc(c, PBS_BATCH_ReleaseResc, NULL, 0, rh)) != 0) {
		(void)pbs_client_thread_unlock_connection(c);
		return (rc);
	}

	/* now get reply */

	reply = PBSD_rdrpy(c);

	PBSD_FreeReply(reply);

	rc = get_conn_errno(c);

	/* unlock the thread lock and update the thread context data */
	if (pbs_client_thread_unlock_connection(c) != 0)
		return pbs_errno;

	return (rc);
}

/*
 * The following routines are provided as a convience in converting
 * older schedulers which did addreq() of "totpool", "usepool", and
 * "avail".
 *
 * The "update" flag if non-zero, causes a new resource query to be sent
 * to the server.  If zero, the existing numbers are used.
 */

/**
 * @brief
 * 	-totpool() - return total number of nodes
 *
 * @param[in] con - connection handle
 * @param[in] update - flag indicating update or not
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */

int
totpool(int con, int update)
{
	struct pbs_client_thread_context *ptr;
	struct node_pool *np;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	ptr = (struct pbs_client_thread_context *)
		pbs_client_thread_get_context_data();
	if (!ptr) {
		pbs_errno = PBSE_INTERNAL;
		return -1;
	}

	if (!ptr->th_node_pool) {
		np = (struct node_pool *) malloc(sizeof(struct node_pool));
		if (!np) {
			pbs_errno = PBSE_INTERNAL;
			return -1;
		}
		ptr->th_node_pool = (void *) np;
		if ((np->resc_nodes = strdup("nodes")) == NULL) {
			free(np);
			np = NULL;
			pbs_errno = PBSE_SYSTEM;
			return -1;
		}
	} else
		np = (struct node_pool *) ptr->th_node_pool;


	if (update) {
		if (pbs_rescquery(con, &np->resc_nodes, 1,
			&np->nodes_avail,
			&np->nodes_alloc,
			&np->nodes_resrv,
			&np->nodes_down) != 0) {
			return (-1);
		}
	}
	return (np->nodes_avail +
		np->nodes_alloc +
		np->nodes_resrv +
		np->nodes_down);
}


/**
 * @brief
 * 	-usepool() - return number of nodes in use, includes reserved and down
 *
 * @param[in] con - connection handle
 * @param[in] update - flag indicating update or not
 *
 * @return      int
 * @retval      0       success
 * @retval      !0      error
 *
 */

int
usepool(int con, int update)
{
	struct pbs_client_thread_context *ptr;
	struct node_pool *np;

	/* initialize the thread context data, if not already initialized */
	if (pbs_client_thread_init_thread_context() != 0)
		return -1;

	ptr = (struct pbs_client_thread_context *)
		pbs_client_thread_get_context_data();
	if (!ptr) {
		pbs_errno = PBSE_INTERNAL;
		return -1;
	}
	if (!ptr->th_node_pool) {
		np = (struct node_pool *) malloc(sizeof(struct node_pool));
		if (!np) {
			pbs_errno = PBSE_INTERNAL;
			return -1;
		}
		ptr->th_node_pool = (void *) np;
		if ((np->resc_nodes = strdup("nodes")) == NULL) {
			free(np);
			np = NULL;
			pbs_errno = PBSE_SYSTEM;
			return -1;
		}
	} else
		np = (struct node_pool *) ptr->th_node_pool;

	if (update) {
		if (pbs_rescquery(con, &np->resc_nodes, 1,
			&np->nodes_avail,
			&np->nodes_alloc,
			&np->nodes_resrv,
			&np->nodes_down) != 0) {
			return (-1);
		}
	}
	return (np->nodes_alloc +
		np->nodes_resrv +
		np->nodes_down);
}

/**
 * @brief
 * 	-avail - returns answer about available of a specified node set
 *
 * @param[in] con - connection handler
 * @param[in] resc - resources
 *
 * @return	string
 * @retval	"yes"		if available (job could be run)
 * @return	"no"		if not currently available
 * @return	"never"		if can never be satified
 * @retval	"?"		if error in request
 */

char *
avail(int con, char *resc)
{
	int av;
	int al;
	int res;
	int dwn;

	if (pbs_rescquery(con, &resc, 1, &av, &al, &res, &dwn) != 0)
		return ("?");

	else if (av > 0)
		return ("yes");
	else if (av == 0)
		return ("no");
	else
		return ("never");
}
