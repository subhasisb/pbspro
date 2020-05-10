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
#include <pbs_config.h>   /* the master config generated by configure */

#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include "portability.h"
#include "libpbs.h"
#include "dis.h"
#include "net_connect.h"
#include "tpp.h"

/**
 * @file	int_hook.c
 */
/**
 *
 * @brief
 *	Send a chunk of data (buf) of size 'len', sequence 'seq'  associated
 *	with the 'hook_filename', over the connection handle 'c'.
 *
 * @param[in]	c - connection channel
 * @param[in]   reqtype - request type
 * @param[in] 	seq - sequence of a block of data (0,1,...)
 * @param[in] 	buf - a block of data
 * @param[in] 	len - size of buf
 * @param[in]	hook_filename - hook filename
 * @param[in]   prot - PROT_TCP or PROT_TPP
 * @param[in]   msgid - msg
 *
 * @return 	int
 * @retval	0 for success
 * @retval	non-zero otherwise.
 */
static int
PBSD_hookbuf(int c, int reqtype, int seq, char *buf, int len, char *hook_filename, int prot, char **msgid)
{
	struct batch_reply   *reply;
	int	rc;
	int	sock;
	int index;

	if (prot == PROT_TCP) {
		sock = get_svr_shard_connection(c, -1, NULL, &index);
		if (sock == -1) {
			return (pbs_errno = PBSE_NOCONNECTION);
		}
		DIS_tcp_funcs();
	} else {
		sock = c;
		if ((rc = is_compose_cmd(sock, IS_CMD, msgid)) != DIS_SUCCESS)
			return rc;
	}

	if ((hook_filename == NULL) || (hook_filename[0] == '\0'))
		return (pbs_errno = PBSE_PROTOCOL);

	if ((rc = encode_DIS_ReqHdr(sock, reqtype, pbs_current_user)) ||
		(rc = encode_DIS_CopyHookFile(sock, seq, buf, len,
		hook_filename)) ||
		(rc = encode_DIS_ReqExtend(sock, NULL))) {

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

	/* read reply */
	reply = PBSD_rdrpy(c);
	PBSD_FreeReply(reply);

	return get_conn_errno(c);
}

/**
 *
 * @brief
 *	Copy the contents of 'hook_filepath' over the network connection
 *	handle 'c'.
 *
 * @param[in]	c - connection channel
 * @param[in]	hook_filepath - local full file pathname
 * @param[in]   prot - PROT_TCP or PROT_TPP
 * @param[in]   msgid - msg
 *
 * @return int
 * @retval	0 for success
 * @retval	-2 for success, no hookfile or empty hookfile
 * @retval	non-zero otherwise.
 */
int
PBSD_copyhookfile(int c, char *hook_filepath, int prot, char **msgid)
{
	int i;
	int fd;
	int cc;
	int rc = -2;
	char s_buf[SCRIPT_CHUNK_Z];
	char	*p;
	char	hook_file[MAXPATHLEN+1];

	if ((fd = open(hook_filepath, O_RDONLY, 0)) < 0) {
		if (prot == PROT_TPP)
			return (-2);  /* ok, if nothing to copy */
		else
			return 0;
	}

	/* set hook_file to the relative path of 'hook_filepath' */
	strncpy(hook_file, hook_filepath, MAXPATHLEN);
	if ((p=strrchr(hook_filepath, '/')) != NULL) {
		strncpy(hook_file, p+1, MAXPATHLEN);
	}

	i = 0;
	cc = read(fd, s_buf, SCRIPT_CHUNK_Z);

	while ((cc > 0) &&
		((rc = PBSD_hookbuf(c, PBS_BATCH_CopyHookFile, i, s_buf, cc, hook_file, prot, msgid)) == 0)) {
		i++;
		cc = read(fd, s_buf, SCRIPT_CHUNK_Z);
	}

	close(fd);
	if (cc < 0)	/* read failed */
		return (-1);

	return rc; /* rc has the return value from PBSD_hookbuf */
}

/**
 *
 * @brief
 *	Send a Delete Hook file request of 'hook_filename' over the network
 *	channel 'c'.
 *
 * @param[in]	c - connection channel
 * @param[in]	hook_filename - hook filename
 * @param[in] 	prot - PROT_TCP or PROT_TPP
 * @param[in] 	msgid - msg
 *
 * @return 	int
 * @retval	0 for success
 * @retval	non-zero otherwise.
 */
int
PBSD_delhookfile(int c, char *hook_filename, int prot, char **msgid)
{
	struct batch_reply   *reply;
	int	rc;
	int	sock;
	int index;

	if (prot == PROT_TCP) {
		sock = get_svr_shard_connection(c, -1, NULL, &index);
		if (sock == -1) {
			return (pbs_errno = PBSE_NOCONNECTION);
		}
		DIS_tcp_funcs();
	} else {
		sock = c;
		if ((rc = is_compose_cmd(sock, IS_CMD, msgid)) != DIS_SUCCESS)
			return rc;
	}

	if ((hook_filename == NULL) || (hook_filename[0] == '\0'))
		return (pbs_errno = PBSE_PROTOCOL);

	if ((rc = encode_DIS_ReqHdr(sock, PBS_BATCH_DelHookFile, pbs_current_user)) ||
		(rc = encode_DIS_DelHookFile(sock, hook_filename)) ||
		(rc = encode_DIS_ReqExtend(sock, NULL))) {
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

	/* read reply */
	reply = PBSD_rdrpy(c);
	PBSD_FreeReply(reply);

	return get_conn_errno(c);
}
