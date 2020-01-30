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
 * @file    attr_recov_db.c
 *
 * @brief
 *
 * attr_recov_db.c - This file contains the functions to save attributes to the
 *		    database and recover them
 *
 * Included public functions are:
 *	save_attr_db		Save attributes to the database
 *	recov_attr_db		Read attributes from the database
 *	make_attr		create a svrattrl structure from the attr_name, and values
 *	recov_attr_db_raw	Recover the list of attributes from the database without triggering
 *				the action routines
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include "pbs_ifl.h"
#include <assert.h>
#include <errno.h>
#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "list_link.h"
#include "attribute.h"
#include "log.h"
#include "pbs_nodes.h"
#include "svrfunc.h"
#include "resource.h"
#include "pbs_db.h"


/* Global Variables */
extern int resc_access_perm;

extern struct attribute_def	svr_attr_def[];
extern struct attribute_def	que_attr_def[];

/**
 * @brief
 *	Create a svrattrl structure from the attr_name, and values
 *
 * @param[in]	attr_name - name of the attributes
 * @param[in]	attr_resc - name of the resouce, if any
 * @param[in]	attr_value - value of the attribute
 * @param[in]	attr_flags - Flags associated with the attribute
 *
 * @retval - Pointer to the newly created attribute
 * @retval - NULL - Failure
 * @retval - Not NULL - Success
 *
 */
static svrattrl *
make_attr(char *attr_name, char *attr_resc,
	char *attr_value, int attr_flags)
{
	int tsize;
	char *p;
	svrattrl *psvrat = NULL;

	tsize = sizeof(svrattrl);
	if (!attr_name)
		return NULL;

	tsize += strlen(attr_name) + 1;
	if (attr_resc) tsize += strlen(attr_resc) + 1;
	if (attr_value) tsize += strlen(attr_value) + 1;

	if ((psvrat = (svrattrl *) malloc(tsize)) == 0)
		return NULL;

	CLEAR_LINK(psvrat->al_link);
	psvrat->al_sister = NULL;
	psvrat->al_atopl.next = 0;
	psvrat->al_tsize = tsize;
	psvrat->al_name = (char *) psvrat + sizeof(svrattrl);
	psvrat->al_resc = 0;
	psvrat->al_value = 0;
	psvrat->al_nameln = 0;
	psvrat->al_rescln = 0;
	psvrat->al_valln = 0;
	psvrat->al_flags = attr_flags;
	psvrat->al_refct = 1;

	strcpy(psvrat->al_name, attr_name);
	psvrat->al_nameln = strlen(attr_name);
	p = psvrat->al_name + psvrat->al_nameln + 1;

	if (attr_resc && strlen(attr_resc) > 0) {
		psvrat->al_resc = p;
		strcpy(psvrat->al_resc, attr_resc);
		psvrat->al_rescln = strlen(attr_resc);
		p = p + psvrat->al_rescln + 1;
	}

	psvrat->al_value = p;
	if (attr_value) {
		strcpy(psvrat->al_value, attr_value);
		psvrat->al_valln = strlen(attr_value);
	} else
		psvrat->al_valln = 0;

	psvrat->al_op = SET;

	return (psvrat);
}

/**
 * @brief
 *	Encode the given attributes to the database structure of type pbs_db_attr_list_t
 *
 * @param[in]	padef - Address of parent's attribute definition array
 * @param[in]	pattr - Address of the parent objects attribute array
 * @param[in]	numattr - Number of attributes in the list
 * @param[out]  attr_list - pointer to the structure of type pbs_db_attr_list_t to which the attributes are encoded
 * @param[in]   all  - Encode all attributes
 *
 * @return   Array of Encoded attributes
 * @retval   NULL - Failure
 * @retval   !NULL - Success
 *
 */
int
encode_attr_db(struct attribute_def *padef, struct attribute *pattr, int numattr, pbs_db_attr_list_t *attr_list, int all)
{
	pbs_list_head lhead;
	int i;
	int j;
	svrattrl *pal;
	int rc = 0;
	int count = 0;
	pbs_db_attr_info_t *attrs = NULL;

	attr_list->attr_count = 0;

	/* encode each attribute which has a value (not non-set) */
	CLEAR_HEAD(lhead);

	j = 0;
	for (i = 0; i < numattr; i++) {

		if ((pattr+i)->at_flags & ATR_DFLAG_NOSAVM)
			continue;

		if (!((all == 1) || ((pattr+i)->at_flags & ATR_VFLAG_MODIFY)))
			continue;

		rc = (padef+i)->at_encode(pattr+i, &lhead,
			(padef+i)->at_name,
			(char *)0, ATR_ENCODE_DB, NULL);
		if (rc < 0)
			return -1;

		(pattr+i)->at_flags &= ~ATR_VFLAG_MODIFY;
	}
	count = 0;
	pal = (svrattrl *)GET_NEXT(lhead);
	while (pal) {
		pal = (svrattrl *)GET_NEXT(pal->al_link);
		count++;
	}

	if (count == 0) {
		attr_list->attributes = NULL;
		attr_list->attr_count = 0;
		return 0;
	}

	attr_list->attributes = calloc(count, sizeof(pbs_db_attr_info_t));
	if (!attr_list->attributes)
		return -1;

	attrs = attr_list->attributes;

	/* now that attribute has been encoded, update to db */
	while ((pal = (svrattrl *)GET_NEXT(lhead)) !=
		(svrattrl *)0) {
		attrs[j].attr_name[sizeof(attrs[j].attr_name) - 1] = '\0';
		strncpy(attrs[j].attr_name, pal->al_atopl.name, sizeof(attrs[j].attr_name));
		if (pal->al_atopl.resource) {
			attrs[j].attr_resc[sizeof(attrs[j].attr_resc) - 1] = '\0';
			strncpy(attrs[j].attr_resc, pal->al_atopl.resource, sizeof(attrs[j].attr_resc));
		} else
			attrs[j].attr_resc[0] = 0;

		attrs[j].attr_value = strdup(pal->al_atopl.value);
		attrs[j].attr_flags = pal->al_flags;
		j++;

		delete_link(&pal->al_link);
		(void)free(pal);
	}
	attr_list->attr_count = j;
	return 0;
}

/**
 * @brief
 *	Decode the list of attributes from the database to the regular attribute structure
 *
 * @param[in]	  parent - pointer to parent object
 * @param[in]	  attr_list - Information about the database attributes
 * @param[in]	  padef - Address of parent's attribute definition array
 * @param[in/out] pattr - Address of the parent objects attribute array
 * @param[in]	  limit - Number of attributes in the list
 * @param[in]	  unknown	- The index of the unknown attribute if any
 *
 * @return      Error code
 * @retval	 0  - Success
 * @retval	-1  - Failure
 *
 *
 */
int
decode_attr_db(
	void *parent,
	pbs_db_attr_list_t *attr_list,
	struct attribute_def *padef,
	struct attribute *pattr,
	int limit,
	int unknown, char *savetm)
{
	int amt;
	int index;
	svrattrl *pal = (svrattrl *)0;
	svrattrl *tmp_pal = (svrattrl *)0;
	int ret = 0;
	int i;
	pbs_db_attr_info_t *attrs = attr_list->attributes;
	void **palarray = NULL;

	if ((palarray = calloc(limit, sizeof(void *))) == NULL) {
		log_err(-1, __func__, "Out of memory");
		ret = -1;
		goto out;
	}

	/* set all privileges (read and write) for decoding resources	*/
	/* This is a special (kludge) flag for the recovery case, see	*/
	/* decode_resc() in lib/Libattr/attr_fn_resc.c			*/

	resc_access_perm = ATR_DFLAG_ACCESS;

	for (i = 0; i < attr_list->attr_count; i++) {
		/* Below ensures that a server or queue resource is not set */
		/* if that resource is not known to the current server. */
		if ( (attrs[i].attr_resc != NULL) && (strlen(attrs[i].attr_resc) > 0) && ((padef == svr_attr_def) || (padef == que_attr_def)) ) {
			resource_def	*prdef;

			prdef = find_resc_def(svr_resc_def, attrs[i].attr_resc, svr_resc_size);
			if (prdef == (resource_def *)0) {
				snprintf(log_buffer, sizeof(log_buffer),
					"%s's unknown resource \"%s.%s\" ignored",
					((padef == svr_attr_def)?"server":"queue"),
					attrs[i].attr_name,
					attrs[i].attr_resc);
				log_err(-1, __func__, log_buffer);
				continue;
			}
		}

		pal = make_attr(attrs[i].attr_name,
		                attrs[i].attr_resc,
		                attrs[i].attr_value,
		                attrs[i].attr_flags);

		/* Return when make_attr fails to create a svrattrl structure */
		if (pal == NULL) {
			log_err(-1, __func__, "Out of memory");
			free(palarray);
			ret = -1;
			goto out;
		}

		amt = pal->al_tsize - sizeof(svrattrl);
		if (amt < 1) {
			snprintf(log_buffer,LOG_BUF_SIZE, "Invalid attr list size in DB");
			log_err(-1, __func__, log_buffer);
			goto out;
		}
		CLEAR_LINK(pal->al_link);

		pal->al_refct = 1;	/* ref count reset to 1 */

		/* find the attribute definition based on the name */
		index = find_attr(padef, pal->al_name, limit);
		if (index < 0) {

			/*
			 * There are two ways this could happen:
			 * 1. if the (job) attribute is in the "unknown" list -
			 *    keep it there;
			 * 2. if the server was rebuilt and an attribute was
			 *    deleted, -  the fact is logged and the attribute
			 *    is discarded (system,queue) or kept (job)
			 */
			if (unknown > 0) {
				index = unknown;
			} else {
				snprintf(log_buffer,LOG_BUF_SIZE, "unknown attribute \"%s\" discarded", pal->al_name);
				log_err(-1, __func__, log_buffer);
				(void)free(pal);
				continue;
			}
		}
		if (palarray[index] == NULL)
			palarray[index] = pal;
		else {
			tmp_pal = palarray[index];
			while (tmp_pal->al_sister)
				tmp_pal = tmp_pal->al_sister;

			/* this is the end of the list of attributes */
			tmp_pal->al_sister = pal;
		}
	}

	if (ret == -1) {
		/*
		 * some error happened above
		 * Error has already been logged
		 * so just free palarray indexes and return
		 */
		if (palarray) {
			for (index = 0; index < limit; index++)
				if (palarray[index])
					free(palarray[index]);
			free(palarray);
		}
		goto out;
	}

	/* now do the decoding */
	for (index = 0; index < limit; index++) {
		/*
		 * In the normal case we just decode the attribute directly
		 * into the real attribute since there will be one entry only
		 * for that attribute.
		 *
		 * However, "entity limits" are special and may have multiple,
		 * the first of which is "SET" and the following are "INCR".
		 * For the SET case, we do it directly as for the normal attrs.
		 * For the INCR,  we have to decode into a temp attr and then
		 * call set_entity to do the INCR.
		 */
		/*
		 * we don't store the op value into the database, so we need to
		 * determine (in case of an ENTITY) whether it is the first
		 * value, or was decoded before. We decide this based on whether
		 * the flag has ATR_VFLAG_SET
		 *
		 */

		/* first free the existing attribute value, if any */
		if (!(padef[index].at_flags & ATR_DFLAG_NOSAVM))
			padef[index].at_free(&pattr[index]);

		pal = palarray[index];
		while (pal) {
			if (!(padef[index].at_flags & ATR_DFLAG_NOSAVM)) { /* dont load NOSAVM attributes, even if set in databae */
				if ((padef[index].at_type == ATR_TYPE_ENTITY) && (pattr[index].at_flags & ATR_VFLAG_SET)) {
					attribute tmpa;
					memset(&tmpa, 0, sizeof(attribute));
					/* for INCR case of entity limit, decode locally */
					if (padef[index].at_decode) {
						padef[index].at_decode(&tmpa, pal->al_name, pal->al_resc, pal->al_value);
						padef[index].at_set(&pattr[index], &tmpa, INCR);
						padef[index].at_free(&tmpa);
					}
				} else {
					if (padef[index].at_decode) {
						padef[index].at_decode(&pattr[index], pal->al_name, pal->al_resc, pal->al_value);
						if (*savetm == '\0' && padef[index].at_action)
							padef[index].at_action(&pattr[index], parent, ATR_ACTION_RECOV);
					}
				}
				pattr[index].at_flags = pal->al_flags & ~ATR_VFLAG_MODIFY;
			}

			tmp_pal = pal->al_sister;
			(void)free(pal);
			pal = tmp_pal;
		}
	}
	(void)free(palarray);
	ret = 0;

	/* fall through to free attribute array stuff */

out:

	return ret;
}

/**
 * @brief
 *	Recover the list of attributes from the database
 *
 * @param[in]	        parent - pointer to the parent object
 * @param[in]	        attr_list - Information about the database attributes
 * @param[in]	        padef - Address of parent's attribute definition array
 * @param[in/out]	pattr - Address of the parent objects attribute array
 * @param[in]	        limit - Number of attributes in the list
 * @param[in]	        unknown	- The index of the unknown attribute if any
 *
 * @return      Error code
 * @retval	 0  - Success
 * @retval	-1  - Failure
 *
 *
 */
int
make_pbs_list_attr_db(
	void *parent,
	pbs_db_attr_list_t *attr_list,
	struct attribute_def *padef,
	pbs_list_head *phead,
	int limit,
	int unknown)
{
	int	  amt;
	svrattrl *pal = (svrattrl *)0;
	svrattrl *tmp = (svrattrl *)0;
	int i;
	pbs_db_attr_info_t *attrs = attr_list->attributes;

	/* set all privileges (read and write) for decoding resources	*/
	/* This is a special (kludge) flag for the recovery case, see	*/
	/* decode_resc() in lib/Libattr/attr_fn_resc.c			*/

	resc_access_perm = ATR_DFLAG_ACCESS;

	for (i = 0; i < attr_list->attr_count; i++) {
		pal = make_attr(
			attrs[i].attr_name,
			attrs[i].attr_resc,
			attrs[i].attr_value,
			attrs[i].attr_flags);

		if (pal == NULL) {
			log_err(-1, __func__, "Out of memory");
			goto err;
		}

		amt = pal->al_tsize - sizeof(svrattrl);
		if (amt < 1) {
			snprintf(log_buffer,LOG_BUF_SIZE, "Invalid attr list size in DB");
			log_err(-1, __func__, log_buffer);
			(void)free(pal);
			goto err;
		}
		CLEAR_LINK(pal->al_link);
		pal->al_refct = 1; /* ref count reset to 1 */
		append_link(phead, &pal->al_link, pal);
	}
	return (0);

err:
	pal = GET_NEXT(*phead);
	while (pal) {
		tmp = pal;
		pal = (svrattrl *)GET_NEXT(pal->al_link);
		delete_link((struct pbs_list_link *) tmp);
		free(tmp);
	}
	return (-1);
}
