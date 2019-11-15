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

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/types.h>
#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "pbs_ifl.h"
#include "list_link.h"
#include "attribute.h"
#include "server_limits.h"
#include "net_connect.h"
#include "job.h"
#include "reservation.h"
#include "pbs_nodes.h"
#include "pbs_error.h"
#include "pbs_internal.h"


static struct node_state {
	unsigned long	 bit;
	char		*name;
} ns[] = {	{INUSE_UNKNOWN,	ND_state_unknown},
	{INUSE_DOWN,    ND_down},
	{INUSE_STALE,   ND_Stale},
	{INUSE_OFFLINE, ND_offline},
	{INUSE_JOB,	ND_jobbusy},
	{INUSE_JOBEXCL, ND_job_exclusive},
	{INUSE_BUSY,	ND_busy},
	{INUSE_INIT,	ND_Initializing},
	{INUSE_PROV,	ND_prov},
	{INUSE_WAIT_PROV, ND_wait_prov},
	{INUSE_RESVEXCL, ND_resv_exclusive},
	{INUSE_UNRESOLVABLE, ND_unresolvable},
	{INUSE_OFFLINE_BY_MOM, ND_offline_by_mom},
	{INUSE_MAINTENANCE, ND_maintenance},
	{INUSE_SLEEP, ND_sleep},
	{0,		NULL} };

static struct node_type {
	short	 bit;
	char	*name;
} nt[] = {	{NTYPE_PBS,	ND_pbs},
	{0,		NULL} };

/**
 * @file	attr_node_func.c
 * @brief
 * 	This file contains functions for deriving attribute values from a pbsnode
 * 	and for updating the "state" (inuse), "node type" (ntype) or "properties"
 * 	list using the "value" carried in an attribute.
 *
 * @par Included are:
 *
 * global:
 * decode_state()		"functions for at_decode func pointer"
 * decode_ntype()
 * decode_props()
 * decode_sharing()
 *
 * encode_state()		"functions for at_encode func pointer"
 * encode_ntype()
 * encode_props()
 * encode_jobs()
 * encode_sharing()
 *
 * set_node_state()		"functions for at_set func pointer"
 * set_node_ntype()
 *
 * node_state()			"functions for at_action func pointer"
 * node_ntype()
 *
 * get_vnode_state_str()	"helper functions"
 * vnode_state_to_str()
 * vnode_ntype_to_str()
 * str_to_vnode_state()
 * str_to_vnode_ntype()
 *
 * local:
 * load_prop()
 * set_nodeflag()
 *
 * The prototypes are declared in "attr_func.h"
 */


/*
 * Set of forward declarations for functions used before defined
 * keeps the compiler happy
 */
static	int	set_nodeflag(char*, unsigned long*);

/**
 * @brief
 *	Given a 'state_bit' value of a vnode, returns the human-readable
 *	form.
 *
 * @par Example:
 *	If <state_bit> == 3, this returns string "offline,down"
 *	since bit 1 is INUSE_OFFLINE and bit 3 is INUSE_DOWN.
 *
 * @par Note:
 * 	Do not free the return value - it's a statically allocated string.
 *
 * @param[in]	state_bit - the numeric state bit value
 *
 * @return	char * (i.e. string)
 * @retval	"<state1>,<state2>,..." - a comma-separated list of states.
 * @retval	""			- corresponding state not found.
 *
 */
char	*
vnode_state_to_str(int state_bit)
{
	static	char *state_str = NULL;
	int	state_bit_tmp;
	int	i;

	/* Ensure that the state_bit_str value contains only valid */
	/* vnode state values. */

	state_bit_tmp = state_bit;
	for (i=0; ns[i].name && (state_bit_tmp != 0); i++) {
		/* clear all the valid states in the value */
		state_bit_tmp &= ~ns[i].bit;
	}

	/* Now clear any internal states */
	if (state_bit_tmp != 0)
		state_bit_tmp &= ~(INUSE_DELETED|INUSE_INIT);

	if (state_bit_tmp != 0)
		return ("");	/* found an unknown state bit set! */

	if (state_str == NULL) {

		int	alloc_sz = 0;


		alloc_sz = strlen(ND_free)+1;

		for (i=0; ns[i].name; i++) {
			alloc_sz += strlen(ns[i].name)+1; /* +1 for comma */
		}
		alloc_sz += 1;	/* for null character */

		state_str = malloc(alloc_sz);

		if (state_str == NULL)
			return (""); /* malloc failure, just return empty */
	}

	if (state_bit == 0) {
		strcpy(state_str, ND_free);
	} else {
		state_str[0] = '\0';
		for (i=0; ns[i].name; i++) {
			if (state_bit & ns[i].bit) {
				if (state_str[0] != '\0')
					(void)strcat(state_str, ",");
				(void)strcat(state_str, ns[i].name);
			}
		}
	}

	return (state_str);
}

/**
 *
 * @brief
 *	Same as vnode_state_to_str() ecept the argument is a string
 *	instead of an int.
 *
 * @param[in]	state_bit_str - the numeric state bit value in string format.
 *
 * @return	char * (i.e. string)
 * @retval	"<state1>,<state2>,..." - a comma-separated list of states.
 * @retval	""			- corresponding state not found.
 *
 */
char	*
get_vnode_state_str(char *state_bit_str)
{
	int	state_bit;

	if ((state_bit_str == NULL) || (state_bit_str[0] == '\0'))
		return ("");

	state_bit = atoi(state_bit_str);

	return (vnode_state_to_str(state_bit));
}

/**
 *
 * @brief
 *	Given a vnode state string 'vnstate', containing the list of descriptive
 *	states, comma separated, return the int bit mask equivalent.
 *
 * @param[in]	vnstate - the vnode state attribute: "<state1>,<state2>,..."
 *
 * @return int
 * @reteval <n>	the bitmask value
 *
 */
int
str_to_vnode_state(char *vnstate)
{
	int statebit = 0;
	char *pc = NULL;
	char *vnstate_dup = NULL;
	int i;

	if (vnstate == NULL) {
		return 0;
	}

	vnstate_dup = strdup(vnstate);
	if (vnstate_dup == NULL)
		return 0;

	pc = strtok(vnstate_dup, ",");
	while (pc) {
		for (i=0; ns[i].name; i++) {
			if (strcmp(ns[i].name, pc) == 0) {
				statebit |= ns[i].bit;
				break;
			}
		}
		pc = strtok(NULL, ",");
	}
	free(vnstate_dup);

	return (statebit);
}

/*
 * @brief
 * 	Encodes the node attribute pattr's  state into an external
 * 	representation, via the  head of a svrattrl structure (ph).
 *
 * @param[in]	pattr	- input attribute
 * @param[in] 	ph 	- head of a list of "svrattrl" structs.
 * @param[out] 	aname	- attribute name
 * @param[out] 	rname	- resource's name (null if none)
 * @param[out] 	mode	- action mode code, unused here
 * @param[out] 	rtnl	- pointer to the actual svrattrl entry.
 *
 * @return 	int
 * @retval	<0	an error encountered; value is negative of an error code
 * @retval	0       ok, encode happened and svrattrl created and linked in,
 *		     	or nothing to encode.
 */

int
encode_state(attribute *pattr, pbs_list_head *ph, char *aname, char *rname, int mode, svrattrl **rtnl)
{
	int	  i;
	svrattrl *pal;
	unsigned long	  state;
	static   char	state_str[ MAX_ENCODE_BFR ];
	int      offline_str_seen;
	char	 *ns_name;

	if (!pattr)
		return -(PBSE_INTERNAL);

	if (!(pattr->at_flags & ATR_VFLAG_SET))
		return (0);      /*nothing to report back*/

	state = pattr->at_val.at_long & INUSE_SUBNODE_MASK;
	if (!state)
		strcpy(state_str, ND_free);

	else {
		state_str[0] = '\0';
		offline_str_seen = 0;
		for (i=0; ns[i].name; i++) {
			if (state & ns[i].bit) {
				ns_name = ns[i].name;
				if (strcmp(ns_name, ND_offline) == 0) {
					offline_str_seen = 1;
				} else if (strcmp(ns_name,
					ND_offline_by_mom) == 0) {
					if (offline_str_seen)
						continue;
					/* ND_offline_by_mom will always be */
					/* shown externally as ND_offline */
					ns_name = ND_offline;
				}

				if (state_str[0] != '\0') {
					(void)strcat(state_str, ",");
				}
				(void)strcat(state_str, ns_name);
			}
		}
	}

	pal = attrlist_create(aname, rname, (int)strlen(state_str)+1);
	if (pal == NULL)
		return -(PBSE_SYSTEM);

	(void)strcpy(pal->al_value, state_str);
	pal->al_flags = ATR_VFLAG_SET;
	if (ph)
		append_link(ph, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (0);             /*success*/
}

/**
 *
 * @brief
 *	Given a vnode type string 'vntype', return the int equivalent.
 *
 * @param[in]	vntype - the vnode type attribute as a string.
 *
 * @return 	int
 * @retval  	<n> 	mapped value from nt[] array.
 * @retval  	-1  	if failed to find a mapping.
 *
 */
int
str_to_vnode_ntype(char *vntype)
{
	int i;

	if (vntype == NULL)
		return (-1);

	for (i=0; nt[i].name; i++) {
		if (strcmp(vntype, nt[i].name) == 0)
			return nt[i].bit;
	}

	return (-1);
}

/**
 *
 * @brief
 *	Given a vnode type 'vntype' in int form, return the string equivalent.
 *
 * @par Note:
 * 	Do not free the return value - it's a statically allocated string.
 *
 * @param[in]	vntype - the vnode type value in int.
 *
 * @return 	str
 * @retval 	mapped value in the nt[] array from int to string.
 * @retval 	""	empty string if not found in nt[] array.
 *
 */
char *
vnode_ntype_to_str(int vntype)
{
	int i;

	for (i=0; nt[i].name; i++) {
		if (vntype == nt[i].bit)
			return nt[i].name;
	}

	return ("");
}

/**
 *
 * @brief
 *	Encodes a node type attribute into a svrattrl structure
 *
 * @param[in]	pattr - struct attribute being encoded
 * @param[in]	ph - head of a list of 'svrattrl' structs which are to be
 *		     return.
 * @param[out]  aname - attribute's name
 * @param[out]  rname - resource's name (null if none)
 * @param[out]	mode - mode code, unused here
 * @param[out]	rtnl - the return value, a pointer to svrattrl
 *
 * @note
 * 	Once the node's "ntype" field is converted to an attribute,
 * 	the attribute can be passed to this function for encoding into
 * 	an svrattrl structure
 *
 * @return 	int
 * @retval    	< 0	an error encountered; value is negative of an error code
 * @retval    	0	ok, encode happened and svrattrl created and linked in,
 *		     	or nothing to encode
 *
 */
int
encode_ntype(attribute *pattr, pbs_list_head *ph, char *aname, char *rname, int mode, svrattrl **rtnl)
{
	svrattrl *pal;
	short	 ntype;

	static   char	ntype_str[ MAX_ENCODE_BFR ];
	int	 i;

	if (!pattr)
		return -(PBSE_INTERNAL);

	if (!(pattr->at_flags & ATR_VFLAG_SET))
		return (0);      /*nothing to report back*/

	ntype = pattr->at_val.at_short & PBSNODE_NTYPE_MASK;
	ntype_str[0] = '\0';
	for (i=0; nt[i].name; i++) {
		if (ntype == nt[i].bit) {
			strcpy(ntype_str, nt[i].name);
			break;
		}
	}

	if (ntype_str[0] == '\0') {
		return -(PBSE_ATVALERANGE);
	}

	pal = attrlist_create(aname, rname, (int)strlen(ntype_str)+1);
	if (pal == NULL)
		return -(PBSE_SYSTEM);

	(void)strcpy(pal->al_value, ntype_str);
	pal->al_flags = ATR_VFLAG_SET;
	if (ph)
		append_link(ph, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (0);             /*success*/
}


/**
 * @brief
 * 	encode_jobs
 * 	Once the node's struct jobinfo pointer is put in the data area of
 *	temporary attribute containing a pointer to the parent node, this
 * 	function will walk the list of jobs and generate the comma separated
 * 	list to send back via an svrattrl structure.
 *
 * @param[in]   pattr - struct attribute being encoded
 * @param[in]   ph - head of a  list of "svrattrl"
 * @param[out]  aname - attribute's name
 * @param[out]  rname - resource's name (null if none)
 * @param[out]  mode - mode code, unused here
 * @param[out]  rtnl - the return value, a pointer to svrattrl
 *
 * @return	int
 * @retval	<0	an error encountered; value is negative of an error code
 * @retval	 0	ok, encode happened and svrattrl created and linked in,
 *			or nothing to encode
 *
 */

int
encode_jobs(attribute *pattr, pbs_list_head *ph, char *aname, char *rname, int mode, svrattrl **rtnl)

{
	svrattrl	*pal;
	struct pbs_job_list 	*jlist;

	if (!pattr)
		return (-1);
	if (!(pattr->at_flags & ATR_VFLAG_SET) || !pattr->at_val.at_jinfo)
		return (0);		/*nothing to report back   */
	jlist = pattr->at_val.at_jinfo->job_list;
	if (!jlist || (jlist->njobs == 0))
		return 0;

	pal = attrlist_create(aname, rname,  jlist->offset + 1);
	if (pal == NULL) {
		return -(PBSE_SYSTEM);
	}

	(void)strcpy(pal->al_value, jlist->job_str);
	pal->al_flags = ATR_VFLAG_SET;

	if (ph)
		append_link(ph, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (0);			/*success*/
}


/**
 * @brief
 * 	encode_resvs
 * 	Once the node's struct resvinfo pointer is put in the data area of
 * 	temporary attribute containing a pointer to the parent node, this
 * 	function will walk the list of reservations and generate the comma
 * 	separated list to send back via an svrattrl structure.
 *
 * @param[in]    pattr - struct attribute being encoded
 * @param[in]    ph - head of a  list of "svrattrl"
 * @param[out]   aname - attribute's name
 * @param[out]   rname - resource's name (null if none)
 * @param[out]   mode - mode code, unused here
 * @param[out]   rtnl - the return value, a pointer to svrattrl
 *
 * @return      int
 * @retval      <0      an error encountered; value is negative of an error code
 * @retval       0      ok, encode happened and svrattrl created and linked in,
 *                      or nothing to encode
 */

int
encode_resvs(attribute *pattr, pbs_list_head *ph, char *aname, char *rname, int mode, svrattrl **rtnl)
{
	svrattrl	*pal;
	struct pbs_job_list 	*rlist;

	DBPRT(("Entering %s", __func__))

	if (!pattr)
		return (-1);

	if (!(pattr->at_flags & ATR_VFLAG_SET) || !pattr->at_val.at_jinfo)
		return (0);                  /*nothing to report back   */

	rlist = pattr->at_val.at_jinfo->resv_list;
	if (!rlist)
		return 0;

	if (*rlist->job_str == 0)
		return (0);	      /*no reservations currently on this node*/

	pal = attrlist_create(aname, rname, rlist->offset + 1);
	if (pal == NULL) {
		return -(PBSE_SYSTEM);
	}

	(void)strcpy(pal->al_value, rlist->job_str);
	pal->al_flags = ATR_VFLAG_SET;

	if (ph)
		append_link(ph, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (0);                  /*success*/
}

/**
 * @brief
 * 	encode_sharing
 * 	Encode the sharing attribute value into one of its possible values,
 *	see "share_words" above
 *
 * @param[in]    pattr - struct attribute being encoded
 * @param[in]    ph - head of a  list of "svrattrl"
 * @param[out]   aname - attribute's name
 * @param[out]   rname - resource's name (null if none)
 * @param[out]   mode - mode code, unused here
 * @param[out]   rtnl - the return value, a pointer to svrattrl
 *
 * @return      int
 * @retval      <0      an error encountered; value is negative of an error code
 * @retval       0      ok, encode happened and svrattrl created and linked in,
 *                      or nothing to encode
 */

int
encode_sharing(attribute *pattr, pbs_list_head *ph, char *aname, char *rname, int mode, svrattrl **rtnl)
{
	int       n;
	svrattrl *pal;
	char *vn_str;

	if (!pattr)
		return -(PBSE_INTERNAL);

	if (!(pattr->at_flags & ATR_VFLAG_SET))
		return (0);      /*nothing to report back*/

	n = (int)pattr->at_val.at_long;
	vn_str = vnode_sharing_to_str((enum vnode_sharing) n);
	if (vn_str == NULL)
		return -(PBSE_INTERNAL);

	pal = attrlist_create(aname, rname, (int)strlen(vn_str)+1);
	if (pal == NULL)
		return -(PBSE_SYSTEM);

	(void)strcpy(pal->al_value, vn_str);
	pal->al_flags = ATR_VFLAG_SET;
	if (ph)
		append_link(ph, &pal->al_link, pal);
	if (rtnl)
		*rtnl = pal;

	return (0);             /*success*/
}

/**
 * @brief
 * 	decode_state
 * 	In this case, the two arguments that get  used are
 *
 * pattr - it points to an attribute whose value is a short,
 * and the argument "val".
 * Once the "value" argument, val, is decoded from its form
 * as a string of comma separated substrings, the component
 * values are used to set the appropriate bits in the attribute's
 * value field.
 *
 * @param[in]    pattr - it points to an attribute whose value is a short,
 *                       and the argument "val".
 * @param[out]   aname - attribute's name
 * @param[out]   rname - resource's name (null if none)
 *
 * @return	int
 * @retval	PBSE_*	error code
 * @retval	0	Success
 *
 */

int
decode_state(attribute *pattr, char *name, char *rescn, char *val)
{
	int	rc = 0;		/*return code; 0==success*/
	unsigned long	flag, currflag;
	char	*str;

	char	strbuf[512];	/*should handle most vals*/
	char	*sbufp;
	int	slen;


	if (val == NULL)
		return (PBSE_BADNDATVAL);

	/*
	 * determine string storage requirement and copy the string "val"
	 * to a work buffer area
	 */

	slen = strlen(val);	/*bufr either on stack or heap*/
	if (slen - 512 < 0)
		sbufp = strbuf;
	else {
		if (!(sbufp = (char *)malloc(slen + 1)))
			return (PBSE_SYSTEM);
	}

	strcpy(sbufp, val);

	if ((str = parse_comma_string(sbufp)) == NULL) {
		if (slen >= 512)
			free(sbufp);
		return rc;
	}

	flag = 0;
	if ((rc = set_nodeflag(str, &flag)) != 0) {
		if (slen >= 512)
			free(sbufp);
		return rc;
	}
	currflag = flag;

	/*calling parse_comma_string with a null ptr continues where*/
	/*last call left off.  The initial comma separated string   */
	/*copy pointed to by sbufp is modified with each func call  */

	while ((str = parse_comma_string(NULL)) != 0) {
		if ((rc = set_nodeflag(str, &flag)) != 0)
			break;

		if ((currflag == 0 && flag) || (currflag && flag == 0)) {
			rc = PBSE_MUTUALEX;	/*free is mutually exclusive*/
			break;
		}
		currflag = flag;
	}

	if (!rc) {
		pattr->at_val.at_long = flag;
		pattr->at_flags |=
			ATR_VFLAG_SET|ATR_VFLAG_MODIFY|ATR_VFLAG_MODCACHE;
	}

	if (slen >= 512)		/*buffer on heap, not stack*/
		free(sbufp);

	return rc;
}

/**
 * @brief
 * 	decode_ntype
 * 	We no longer decode the node type.  Instead, we simply pretend to do so
 * 	and return success.
 *
 * 	Historical information from the previous version of this function:
 *	In this case, the two arguments that get used are
 *	pattr-- it points to an attribute whose value is a short,
 *	and the argument "val". We once had "time-shared" and "cluster"
 *	node types. There may come a time when other ntype values are
 *	needed. The one thing that is assumed is that the types are
 *	going to be mutually exclusive.
 *
 * @param[in] pattr - pointer to attribute structure
 * @param[in] name - attribute name
 * @param[in] rescn - resource name, unused here
 * @param[in] val - attribute value
 *
 * @return	int
 * @retval	0	Success
 */

int
decode_ntype(attribute *pattr, char *name, char *rescn, char *val)
{
	pattr->at_val.at_short = NTYPE_PBS;
	pattr->at_flags |= ATR_VFLAG_SET|ATR_VFLAG_MODIFY|ATR_VFLAG_MODCACHE;

	return 0;
}

/**
 * @brief
 * 	decode_sharing - decode one of the acceptable share value strings into
 *	the array index which is stored as the attribute value;
 *
 * @param[in] pattr - pointer to attribute structure
 * @param[in] name - attribute name
 * @param[in] rescn - resource name, unused here
 * @param[in] val - attribute value
 *
 * @return      int
 * @retval      0       	Success
 * @retval	PBSE code	error
 */

int
decode_sharing(attribute *pattr, char *name, char *rescn, char *val)
{
	int	vns;
	int	rc = 0;		/*return code; 0==success*/


	if (val == NULL)
		rc = (PBSE_BADNDATVAL);
	else {
		vns = (int) str_to_vnode_sharing(val);
		if (vns == VNS_UNSET)
			rc = (PBSE_BADNDATVAL);
	}

	if (!rc) {
		pattr->at_val.at_long = vns;
		pattr->at_flags |= ATR_VFLAG_SET|ATR_VFLAG_MODIFY|ATR_VFLAG_MODCACHE;
	}

	return rc;
}


/**
 * @brief
 *
 *	Update the state information  of 'pattr' using the info from 'new'.
 *
 * @param[out]	pattr - attribute whose  node state is to be updated.
 * @param[in]	new - input information.
 * @param[in]	op - update mode (SET, INCR, DECR).
 *
 * @return int
 * @retval 0 	- for success
 * @retval != 0 - for any failure
 */
int
set_node_state(attribute *pattr, attribute *new, enum batch_op op)
{
	int	rc = 0;

	assert(pattr && new && (new->at_flags & ATR_VFLAG_SET));

	switch (op) {

		case SET:
			pattr->at_val.at_long = new->at_val.at_long;
			break;

		case INCR:
			if (pattr->at_val.at_long && new->at_val.at_long == 0) {
				rc = PBSE_BADNDATVAL;  /*"free" mutually exclusive*/
				break;
			}

			pattr->at_val.at_long |= new->at_val.at_long;
			break;

		case DECR:
			if (pattr->at_val.at_long && new->at_val.at_long == 0) {
				rc = PBSE_BADNDATVAL;  /*"free" mutually exclusive*/
				break;
			}

			pattr->at_val.at_long &= ~new->at_val.at_long;
			if (new->at_val.at_long & INUSE_OFFLINE) {
				/* if INUSE_OFFLINE is being cleared, must also */
				/* clear INUSE_OFFLINE_BY_MOM. */
				pattr->at_val.at_long &= ~INUSE_OFFLINE_BY_MOM;
			}
			break;

		default:
			rc = PBSE_INTERNAL;
			break;
	}

	if (!rc) {
		pattr->at_flags |=
			ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	}
	return rc;
}

/**
 * @brief
 * 	set_node_ntype - the value entry in attribute "new" is a short.  It was
 *	generated by the decode routine is used to update the
 *	value portion of the attribute *pattr
 *	the mode of the update is goverened by the argument "op"
 *	(SET,INCR,DECR)
 *
 * @param[out]	pattr - attribute whose  node state is to be updated.
 * @param[in]	new - input information.
 * @param[in]	op - update mode (SET, INCR, DECR).
 *
 * @return int
 * @retval 0 	- for success
 * @retval != 0 - for any failure
 */

int
set_node_ntype(attribute *pattr, attribute *new, enum batch_op op)
{
	int	rc = 0;

	assert(pattr && new && (new->at_flags & ATR_VFLAG_SET));

	switch (op) {

		case SET:
			pattr->at_val.at_short = new->at_val.at_short;
			break;

		case INCR:
			if (pattr->at_val.at_short != new->at_val.at_short) {

				rc = PBSE_MUTUALEX;  /*types are mutually exclusive*/
			}
			break;

		case DECR:
			if (pattr->at_val.at_short != new->at_val.at_short)
				rc = PBSE_MUTUALEX;  /*types are mutually exclusive*/

			break;

		default:	rc = PBSE_INTERNAL;
	}

	if (!rc)
		pattr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	return rc;
}

/**
 * @brief
 *	sets the node flag
 *
 * @param
 * 	Use the input 'str's value to set a bit in
 *	the "flags" variable pointed to by 'pflag'.
 * @note
 *	Each call sets one more bit in the flags
 *	variable or it clears the flags variable
 *	in the case where *str is the value "free".
 *
 * @param[in]	str - input string state value.
 * @param[out]	pflag - pointer to the variable holding the result.
 *
 * @return	int
 * @retval	0			success
 * @retval	PBSE_BADNDATVAL		error
 *
 */

static int
set_nodeflag(char *str, unsigned long *pflag)
{
	int	rc = 0;

	if (*str == '\0')
		return (PBSE_BADNDATVAL);

	if (!strcmp(str, ND_free))
		*pflag = 0;
	else if (!strcmp(str, ND_offline))
		*pflag = *pflag | INUSE_OFFLINE ;
	else if (!strcmp(str, ND_offline_by_mom))
		*pflag = *pflag | INUSE_OFFLINE_BY_MOM ;
	else if (!strcmp(str, ND_down))
		*pflag = *pflag | INUSE_DOWN ;
	else if (!strcmp(str, ND_sleep))
		*pflag = *pflag | INUSE_SLEEP ;
	else {
		rc = PBSE_BADNDATVAL;
	}

	return rc;
}


/**
 * @brief
 * 	Set node 'pnode's state to either use the non-down or non-inuse node state value,
 * 	or the value derived from the 'new' attribute state.
 *
 * @param[in] 		new - input attribute to derive state from
 * @param[in/out]	pnode - node who state is being set.
 * @param[in]		actmode - action mode: "NEW" or "ALTER"
 *
 * @return int
 * @retval 0			if set normally
 * @retval PBSE_NODESTALE	if pnode's state is INUSE_STALE
 * @retval PBSE_NODEPROV	if pnode's state is INUSE_PROV
 * 	   PBSE_INTERNAL	if 'actmode' is unrecognized
 */

int
node_state(attribute *new, void *pnode, int actmode)
{
	int rc = 0;
	struct pbsnode* np;
	static unsigned long keep = ~(INUSE_DOWN | INUSE_OFFLINE | INUSE_OFFLINE_BY_MOM | INUSE_SLEEP);


	np = (struct pbsnode*)pnode;	/*because of def of at_action  args*/

	/* cannot change state of stale node */
	if (np->nd_state & INUSE_STALE)
		return PBSE_NODESTALE;

	/* cannot change state of provisioning node */
	if (np->nd_state & INUSE_PROV)
		return PBSE_NODEPROV;

	switch (actmode) {

		case ATR_ACTION_NEW:  /*derive attribute*/
			set_vnode_state(np, (np->nd_state & keep) | new->at_val.at_long, Nd_State_Set);
			break;

		case ATR_ACTION_ALTER:
			set_vnode_state(np, (np->nd_state & keep) | new->at_val.at_long, Nd_State_Set);
			break;

		default: rc = PBSE_INTERNAL;
	}
	/* Now that we are setting the node state, same state should also reflect on the mom */
	if (np->nd_nummoms == 1) {
		mom_svrinfo_t *pmom_svr = (mom_svrinfo_t *)np->nd_moms[0]->mi_data;
		pmom_svr->msr_state = (pmom_svr->msr_state & keep) | new->at_val.at_long;
	}
	return rc;
}


/**
 * @brief
 * 	node_ntype - Either derive an "ntype" attribute from the node
 *	or update node's "ntype" field using the
 *	attribute's data
 *
 * @param[out] new - derive ntype into this attribute
 * @param[in] pnode - pointer to a pbsnode struct
 * @param[in] actmode - action mode; "NEW" or "ALTER"
 *
 * @return      int
 * @retval      0                       success
 * @retval      PBSE_INTERNAL	        error
 *
 */

int
node_ntype(attribute *new, void *pnode, int actmode)
{
	int rc = 0;
	struct pbsnode* np;


	np = (struct pbsnode*)pnode;	/*because of def of at_action  args*/
	switch (actmode) {

		case ATR_ACTION_NOOP:
			break;

		case ATR_ACTION_NEW:
		case ATR_ACTION_ALTER:
			np->nd_ntype = new->at_val.at_short;
			break;

		case ATR_ACTION_RECOV:
		case ATR_ACTION_FREE:
		default:
			rc = PBSE_INTERNAL;
	}
	return rc;
}

/**
 *
 * @brief
 *
 *	Returns the "external" form of the attribute 'val' given 'name'.
 *
 * @param[in] 	name - attribute name
 * @param[in] 	val - attribute value
 *
 * @return char * - the external form for name=state: "3" -> "down,offline"
 * @Note
 *     	Returns a static value that can potentially get cleaned up on next call.
 * 	Must use return value immediately!
 */
char *
return_external_value(char *name, char *val)
{
	char *vns;


	if ((name == NULL) || (val == NULL))
		return ("");

	if (strcmp(name, ATTR_NODE_state) == 0) {
		return vnode_state_to_str(atoi(val));
	} else if (strcmp(name, ATTR_NODE_Sharing) == 0) {
		vns = vnode_sharing_to_str((enum vnode_sharing)atoi(val));
		return (vns?vns:"");
	} else if (strcmp(name, ATTR_NODE_ntype) == 0) {
		return vnode_ntype_to_str(atoi(val));
	} else {
		return val;
	}
}

/**
 * @brief
 *		Returns the "internal" form of the attribute 'val' given 'name'.
 *
 * @param[in]	name	-	attribute name
 * @param[in]	val	-	attribute value
 *
 * @return char *	: the external form for name=state: "down,offline" -> "3"
 * @Note
 *     	Returns a static value that can potentially get cleaned up on next call.
 * 		Must use return value immediately!
 *
 * @par MT-safe: No
 */
char *
return_internal_value(char *name, char *val)
{
	static char ret_str[MAX_STR_INT];
	enum vnode_sharing share;
	int  v;

	if ((name == NULL) || (val == NULL))
		return ("");

	if (strcmp(name, ATTR_NODE_state) == 0) {
		v=str_to_vnode_state(val);
		sprintf(ret_str, "%d", v);
		return (ret_str);
	} else if (strcmp(name, ATTR_NODE_Sharing) == 0) {
		share = str_to_vnode_sharing(val);
		if (share == VNS_UNSET)
			return val;
		sprintf(ret_str, "%d", share);
		return (ret_str);
	} else if (strcmp(name, ATTR_NODE_ntype) == 0) {
		v = str_to_vnode_ntype(val);
		if (v == -1)
			return val;
		sprintf(ret_str, "%d", v);
		return (ret_str);
	} else {
		return (val);
	}
}

/**
 *
 * @brief
 *	Prints out the file on opened stream 'fp', the attribute names or
 *	resources and their values as in:
 *		<attribute_name>=<attribute_value>
 *		<attribute_name>[<resource_name>]=<resource_value>
 *		<vnode_name>.<attribute_name>=<attribute value>
 *		<vnode_name>.<attribute_name>[<resource_name>]=<attribute value>
 *		<head_str>[<attribute_name>].p[<resource_name>]=<resource_value>
 * @Note
 *	Only prints out values that were set in a hook script.
 *
 * @param[in]	fp 	- the stream pointer of the file to write output into
 * @param[in]	head_str- some string to print out the beginning.
 * @param[in]	phead	- pointer to the head of the list containing data.
 *
 * @return none
 */
void
fprint_svrattrl_list(FILE *fp, char *head_str, pbs_list_head *phead)
{
	svrattrl *plist = NULL;
	char	*p, *p0;

	if ((fp == NULL) || (head_str == NULL) || (phead == NULL)) {
		log_err(errno, __func__, "NULL input parameters!");
		return;
	}

	for (plist = (svrattrl *)GET_NEXT(*phead); plist != NULL;
			plist = (svrattrl *)GET_NEXT(plist->al_link)) {
		if (plist->al_flags & ATR_VFLAG_HOOK) {
			p = strrchr(plist->al_name, '.');
			p0 = p;
			if (p != NULL) {
				*p = '\0';
				p++; /* this is the actual attribute name */
			}

			if (plist->al_resc != NULL) {
				if (p != NULL)
					fprintf(fp, "%s[\"%s\"].%s[%s]=%s\n", head_str,
						plist->al_name, p,
						plist->al_resc,
						return_external_value(p, plist->al_value));
				else
					fprintf(fp, "%s.%s[%s]=%s\n", head_str,
						plist->al_name, plist->al_resc,
						return_external_value(plist->al_name,
						plist->al_value));
			} else {
				if (p != NULL) {
					fprintf(fp, "%s[\"%s\"].%s=%s\n", head_str,
						plist->al_name, p,
						return_external_value(p, plist->al_value));
				} else {
					if (strcmp(plist->al_name, ATTR_v) == 0) {
						fprintf(fp, "%s.%s=\"\"\"%s\"\"\"\n",
							head_str,
							plist->al_name,
							return_external_value(
							plist->al_name,
							plist->al_value));
					} else {
						fprintf(fp, "%s.%s=%s\n", head_str,
							plist->al_name,
							return_external_value(
							plist->al_name,
							plist->al_value));
					}
				}
			}
			if (p0 != NULL)
				*p0 = '.';
		}
	}
}