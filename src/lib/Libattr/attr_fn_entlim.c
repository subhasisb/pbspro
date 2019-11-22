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

#include <assert.h>
#include <ctype.h>
#include <memory.h>
#ifndef NDEBUG
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <pbs_ifl.h>
#include "log.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_error.h"
#include "pbs_entlim.h"


/**
 * @file	attr_fn_entlim.c
 * @brief
 * 	This file contains functions for manipulating attributes of type "entlim"
 * 	entity limits for Finer Granularity Control (FGC)
 * 	This layer is to somewhat isolate the entlim concept from the specific
 * 	implementation (avl tree).
 * @details
 * The entities are maintained in an AVL tree for fast searching,
 *	see attr_entity in attribute.h.
 * The "key" is the entity+resource and the corresponding data is
 *	an "fgc union", see resource.h.
 */


void free_entlim(attribute *);	/* found in lib/Libattr/attr_fn_entlim.c */

/**
 * @brief
 *	Free a server style entity-limit leaf from the tree;  does not free
 *	the key associated with the leaf.
 *
 * @param[in] pvdlf - pointer to the leaf; void type to allow for indirect
 *		calls to this or similar functions.
 *
 * @return	Void
 *
 */

static void
svr_freeleaf(void *pvdlf)
{
	svr_entlim_leaf_t *plf = pvdlf;

	if (plf) {
		plf->slf_rescd->rs_free(&plf->slf_limit);
		plf->slf_rescd->rs_free(&plf->slf_sum);
		free(plf);
	}
}

/**
 * @brief
 * 	dup_svr_entlim_leaf - duplicate the leaf data (a svr_entlim_leaf struct)
 *	Used when adding a entry from one context (tree) to another, i.e.
 *	in set_entilm():INCR
 *
 *	WARNING: this simple code works only because we are allowing only
 *	data such as integers, floats, sizes; that is self contained within
 *	the attribute structure (no external data as needed for strings, ...)
 *	I.e. it is doing a structure to structure shallow copy.
 *
 * @param[in] orig - pointer to the original svr_entlim_leaf structure
 *
 * @return pointer to new svr_entlim_leaf structure
 *
 */

svr_entlim_leaf_t *
dup_svr_entlim_leaf(svr_entlim_leaf_t *orig)
{
	svr_entlim_leaf_t *newlf;

	newlf = malloc(sizeof(svr_entlim_leaf_t));
	if (newlf)
		*newlf = *orig;
	return (newlf);
}


/**
 * @brief
 * 	alloc_svrleaf - allocate memory for Server entity leaf and do basic
 *	initialization
 *
 * @param[in]	resc_name - either (1) the name of the limited resource for
 *	max_queued_resc and such, or (2) is NULL for the job count attributes
 *	such as max_queued.
 * @param[out] pplf - address of a pointer to a svr_entlim_leaf, set to
 *	newly allocated memory
 *
 * @return  int
 * @retval 0 - success
 * @retval PBS_UNKRESC - resource name is unknown
 * @retval PBS_SYSTEM  - unable to allocate memory
 *
 */

int
alloc_svrleaf(char *resc_name, svr_entlim_leaf_t **pplf)
{
	struct resource_def	*prdef;
	svr_entlim_leaf_t	*plf;

	if (resc_name == NULL) {
		/* use "ncpus" resource_def for the various functions	*/
		/* as it is a simple integer type as needed here	*/
		prdef = find_resc_def(svr_resc_def, "ncpus", svr_resc_size);
	} else {
		prdef = find_resc_def(svr_resc_def, resc_name, svr_resc_size);
	}

	if (prdef == NULL) {
		return PBSE_UNKRESC;
	}

	plf = malloc(sizeof(svr_entlim_leaf_t));
	if (plf == NULL) {
		return PBSE_SYSTEM;
	}
	memset((void *)plf, 0, sizeof(svr_entlim_leaf_t));
	plf->slf_rescd = prdef;
	*pplf = plf;
	return (PBSE_NONE);
}

/**
 * @brief
 * 	svr_addleaf - add an entity limit leaf to the specified context (tree)
 *	and set the slf_limit (server leaf) member.  Also sets
 *	PBS_ELNTLIM_LIMITSET flag in the resource_def structure for the
 *	resource (if one).  Used only by the Server.
 *
 * @param[in] ctx - pointer to "context" - i.e. the tree
 * @param[in] kt  - the entity type enum value
 * @param[in] fulent - the letter associated with the entity type
 * @param[in] entity - the entity name
 * @param[in] rescn  - the resource name, may be null for simple counts
 * @param[in] value  - the resource or count value
 *
 * @return int
 * @retval 0 on success
 * @retval PBSE_ error number on error
 *
 */

int
svr_addleaf(void *ctx, enum lim_keytypes kt, char *fulent, char *entity,
	char *rescn, char *value)
{
	char			*kstr;
	svr_entlim_leaf_t	*plf = NULL;
	int			 rc;

	if (rescn == NULL) {
		/* use "ncpus" resource_def for the various functions	*/
		/* as it is simple integer type needed here		*/
		kstr = entlim_mk_runkey(kt, entity);
	} else {
		kstr = entlim_mk_reskey(kt, entity, rescn);
	}

	if (kstr == NULL)
		return (PBSE_UNKRESC);

	if ((rc = alloc_svrleaf(rescn, &plf)) != PBSE_NONE) {
		free(kstr);
		return (rc);
	}

	rc =  plf->slf_rescd->rs_decode(&plf->slf_limit, NULL, rescn, value);
	if (rc != 0) {
		free(kstr);
		free(plf);
		return rc;
	}

	/* flag that limits are set for this resource name */
	if (rescn != NULL)
		plf->slf_rescd->rs_entlimflg |= PBS_ENTLIM_LIMITSET;

	/* add key+record */
	rc = entlim_add(kstr, (void *)plf, ctx);
	if (rc != 0) {
		svr_freeleaf(plf);
	}
	free(kstr);	/* all cases, free the key string */
	return (rc);
}


/**
 * @brief
 * 	internal_decode_entlim - decode a "attribute name/optional resource/value"
 *		set into a entity type attribute
 *	Used by decode_entlim() and decode_entlim_resc() to do the real work
 *
 * @param[in]  patr - pointer to attribute value into which we are decoding
 *			attribute structure is modified/set
 * @param[in]  name - attribute name, not used
 * @param[in]  rn   - resource name
 * @param[in]  prdef - pointer to resource defininition
 * @param[in]  value - string to be decoded as the attribute value
 *
 * @return int
 * @retval  0 on success
 * @retval  PBSE_* on error
 *
 */

static int
internal_decode_entlim(struct attribute *patr,  char *name, char *rn,
	struct resource_def *prdef, char *val)
{
	void		*petree;
	int		 rc = 0;
	char		*valcopy;

	if ((patr->at_flags & ATR_VFLAG_SET) ||
		(patr->at_val.at_enty.ae_tree != NULL))
		free_entlim(patr);


	/* create header for tree,  no duplicate keys and variable length key */

	petree = entlim_initialize_ctx();
	if (petree == NULL)
		return PBSE_SYSTEM;

	/* entlim_parse munges the input string, so give it a copy */
	valcopy = strdup(val);
	if (valcopy == NULL) {
		(void)entlim_free_ctx(petree, svr_freeleaf);
		return PBSE_SYSTEM;
	}
	rc = entlim_parse(valcopy, rn, petree, svr_addleaf);
	free(valcopy);
	if (rc != 0) {
		(void)entlim_free_ctx(petree, svr_freeleaf);
		return (PBSE_BADATVAL);
	}
	patr->at_val.at_enty.ae_tree = petree;
	patr->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;

	return (0);
}

/**
 * @brief
 * 	decode_entlim - decode a "attribute name/value" pair into
 *	a entity count type attribute (without resource)
 *	The value is of the form "[L:Ename=Rvalue],..."
 *	where L is 'u' (user), 'g' (group), or 'o' (overall)
 *	Ename is a user or group name or "PBS_ALL"
 *	Rvalue is a integer value such as "10"
 *
 * @param[in] 	patr	pointer to attribute to be set
 * @param[in] 	name	attribute name (not used)
 * @param[in] 	rescn	resource name - should be null
 * @param[in] 	val	string to decode as the entity value
 *
 * @return int
 * @retval 0 - success
 * @retval non zero - PBSE_* error number
 *
 */

int
decode_entlim(struct attribute *patr, char *name, char *rescn, char *val)
{

	if (patr == NULL)
		return (PBSE_INTERNAL);
	if (rescn != NULL)
		return (PBSE_INTERNAL);


	return (internal_decode_entlim(patr, name, NULL, NULL, val));
}

/**
 * @brief
 * 	decode_entlim_res - decode a "attribute name/resource name/value" triplet
 *	into a entity type attribute (with resource)
 *	The value is of the form "[L:Ename=Rvalue],..."
 *	where L is 'u' (user), 'g' (group), or 'o' (overall)
 *	Ename is a user or group name or "PBS_ALL"
 *	Rvalue is a resource value such as "10" or "4gb"
 *
 * @param[in] 	patr	pointer to attribute to be set
 * @param[in] 	name	attribute name (not used)
 * @param[in] 	rescn	resource name - must not be null
 * @param[in] 	val	string to decode as the entity value
 *
 * @return int
 * @retval 0 - success
 * @retval non zero - PBSE_* error number
 *
 */

int
decode_entlim_res(struct attribute *patr, char *name, char *rescn, char *val)
{
	resource_def	*prdef;

	if (patr == NULL)
		return (PBSE_INTERNAL);
	if (rescn == NULL)
		return (PBSE_UNKRESC);
	prdef = find_resc_def(svr_resc_def, rescn, svr_resc_size);
	if (prdef == NULL) {
		/*
		 * didn't find resource with matching name
		 * return PBSE_UNKRESC
		 */
		return (PBSE_UNKRESC);
	}
	if ((prdef->rs_type != ATR_TYPE_LONG)  &&
		(prdef->rs_type != ATR_TYPE_SIZE)  &&
		(prdef->rs_type != ATR_TYPE_LL)    &&
		(prdef->rs_type != ATR_TYPE_SHORT) &&
		(prdef->rs_type != ATR_TYPE_FLOAT))
		return (PBSE_INVALJOBRESC);


	return (internal_decode_entlim(patr, name, rescn, NULL, val));
}


/**
 * @brief
 * 	encode_entlim_db - encode attr of type ATR_TYPE_ENTITY into a form suitable
 * 	to be stored as a single record into the database.
 *
 * Here we are a little different from the typical attribute.  Most have a
 * single value to be encoded.  But an entity attribute may have a whole bunch.
 * First get the name of the parent attribute.
 * Then for each entry in the tree, call the individual resource encode
 * routine with "aname" set to the parent attribute name and with a null
 * pbs_list_head .  The encoded resource value is then prepended with the "entity
 * string" and "=" character which is then concatenated together to create a
 * single value string for the entire attribute value. As we find a new pair of
 * "attribute_name+resc_name", we add to a list where we continue to assemble
 * the value strings.
 *
 * Note: entities with an "unset" value will not be encoded.
 *
 *
 *	Returns: >0 if ok
 *		 =0 if no value to encode, no entries added to list
 *		 <0 if some resource entry had an encode error.
 *
 * @param[in] 	attr	pointer to attribute to encode
 * @param[in]	phead;	head of attrlist list onto which the encoded is appended
 * @param[in] 	atname	attribute name (not used)
 * @param[in] 	rsname	resource name, null on call
 * @param[in] 	mode	encode mode
 * @param[out]  rtnl	address of pointer to encoded svrattrl entry which
 *			is also appended to list
 *
 * @return int
 * @retval >0 - success
 * @retval =0  - no value to encode, nothing added to list (phead)
 * @retval <0 - if some entry had an encode error
 *
 */

int
encode_entlim_db(attribute *attr, pbs_list_head *phead, char *atname, char *rsname, int mode, svrattrl **rtnl)
{
	void *ctx;
	int grandtotal = 0;
	pbs_entlim_key_t *pkey = NULL;
	char rescn[PBS_MAX_RESC_NAME + 1];
	char etname[PBS_MAX_RESC_NAME + 1];
	char *pc;
	int needquotes;
	svrattrl *pal;
	svrattrl *tmpsvl;
	int len = 0;
	svr_entlim_leaf_t *plf;
	char *pos = NULL, *p;
	int oldlen = 0;
	svrattrl *xprior = NULL;

	/*
	 * structure to hold the various entity attributes along with their
	 * concatenated values, as we walk the tree
	 */
	struct db_attrib {
		char atname[PBS_MAX_RESC_NAME];
		char rescn[PBS_MAX_RESC_NAME];
		char *val;
	};
	struct db_attrib *db_attrlist = NULL;
	int cursize = 0;
	int index = 0;

	if (!attr)
		return (-1);
	if (!(attr->at_flags & ATR_VFLAG_SET))
		return (0); /* nothing up the tree */

	ctx = attr->at_val.at_enty.ae_tree;

	/* ok, now process each separate entry in the tree */

	/* the call to entlim_get_next with a null key will allocate */
	/* space for a max sized key.  It needs to be freed.	     */

	pkey = entlim_get_next(NULL, ctx);
	while (pkey) {

		rescn[0] = '\0';
		needquotes = 0;

		plf = (svr_entlim_leaf_t *)(pkey->recptr);

		if ((entlim_entity_from_key(pkey, etname, PBS_MAX_RESC_NAME) == 0) &&
			(entlim_resc_from_key(pkey, rescn, PBS_MAX_RESC_NAME) >= 0)) {

			/* decode leaf value into a local svrattrl structure in */
			/* order to obtain a string represnetation of the value */

			if (plf->slf_rescd->rs_encode(&plf->slf_limit, NULL, atname, rescn, mode, &tmpsvl) > 0) {

				/* find out if this etname + rescn pair is created already, if not create an attribute */
				for (index = 0; index < cursize; index++) {
					if ((strcmp(db_attrlist[index].atname, atname) == 0) &&
						(strcmp(db_attrlist[index].rescn, rescn) == 0)) {
						/* found the resource or NULL resource */
						break;
					}
				}
				if (index == cursize) {
					cursize++;
					if (!(p = realloc(db_attrlist, sizeof(struct db_attrib) * cursize)))
						goto err;
					db_attrlist = (struct db_attrib *) p;
					strcpy(db_attrlist[index].atname, atname);
					strcpy(db_attrlist[index].rescn, rescn);
					db_attrlist[index].val = NULL;
				}

				/* Allocate the "real" svrattrl sufficiently large to     */
				/* hold the form "[l:entity;rname=value_string]" plus one */
				/* and assemble the real value into the real svrattrl     */

				/* [u:=]  null = 6 extra characters */
				len = tmpsvl->al_valln + strlen(etname) + 6;

				/* is there whitespace in the entity name ? */
				/* if so, then we quote the whole thing.    */
				pc = etname;
				while (*pc) {
					if (isspace((int) *pc++)) {
						needquotes = 1;
						len += 2;
						break;
					}
				}

				if (!db_attrlist[index].val) {
					if (!(db_attrlist[index].val = malloc(len)))
						goto err;
					pos = db_attrlist[index].val;
				} else {
					oldlen = strlen(db_attrlist[index].val);
					/* add old length + space for comma to total len */
					len = len + oldlen + 1;
					if (!(p = realloc(db_attrlist[index].val, len)))
						goto err;
					db_attrlist[index].val = p;
					strcat(db_attrlist[index].val, ",");
					pos = db_attrlist[index].val + oldlen + 1;
				}

				if (needquotes) {
					sprintf(pos, "[%c:\"%s\"=%s]",
						*pkey->key, etname, tmpsvl->al_atopl.value);
				} else {
					sprintf(pos, "[%c:%s=%s]",
						*pkey->key, etname, tmpsvl->al_atopl.value);
				}
				free(tmpsvl);
				++grandtotal;
			}
		}
		pkey = entlim_get_next(pkey, ctx);
	}
	if (pkey)
		free(pkey);

	/*
	 * now we are done with the tree and should have assembled the strings
	 * for the various attributes. Walk this array and create the real
	 * attribute list
	 */
	for (index = 0; index < cursize; index++) {
		len = strlen(db_attrlist[index].val) + 1;
		if (db_attrlist[index].rescn[0] == '\0')
			pal = attrlist_create(db_attrlist[index].atname, NULL, len);
		else
			pal = attrlist_create(db_attrlist[index].atname, db_attrlist[index].rescn, len);

		strcpy(pal->al_atopl.value, db_attrlist[index].val);
		free(db_attrlist[index].val);
		pal->al_flags = attr->at_flags;
		/* op is not stored in db, so no need to set it */

		if (phead)
			append_link(phead, &pal->al_link, pal);

		if (index == 0) {
			if (rtnl)
				*rtnl = pal;
		} else {
			xprior->al_sister = pal;
		}
		xprior = pal;
	}
	/* finally free the whole db_attrlist */
	if (db_attrlist)
		free(db_attrlist);

	//return (grandtotal);
	return (cursize);

err:
	if (pkey)
		free(pkey);

	/* walk the array and free every set index */
	if (db_attrlist) {
		for (index = 0; index < cursize; index++) {
			if (db_attrlist[index].val)
				free(db_attrlist[index].val);
		}
		free(db_attrlist);
	}

	return (-1);
}


/**
 * @brief
 * 	encode_entlim - encode attr of type ATR_TYPE_ENTITY into attr_extern form
 *
 * Here we are a little different from the typical attribute.  Most have a
 * single value to be encoded.  But an entity attribute may have a whole bunch.
 * First get the name of the parent attribute.
 * Then for each entry in the tree, call the individual resource encode
 * routine with "aname" set to the parent attribute name and with a null
 * pbs_list_head .  The encoded resource value is then prepended with the "entity
 * string" and "=" character which is then placed in a new svrattrl entry
 * which is then added to the real list head.
 *
 * Note: entities with an "unset" value will not be encoded.
 *
 *
 * @param[in] 	attr	pointer to attribute to encode
 * @param[in]	phead;	head of attrlist list onto which the encoded is appended
 * @param[in] 	atname	attribute name
 * @param[in] 	rsname	resource name, null on call
 * @param[in] 	mode	encode mode
 * @param[out]  rtnl	address of pointer to encoded svrattrl entry which
 *			is also appended to list
 * @return int
 * @retval  >0 if ok
 * @retval  =0 if no value to encode, no entries added to list
 * @retval  <0 if some resource entry had an encode error.
 *
 */
int
encode_entlim(attribute *attr, pbs_list_head *phead, char *atname, char *rsname, int mode, svrattrl **rtnl)
{
	void	   *ctx;
	int	    grandtotal = 0;
	int	    first = 1;
	svrattrl   *xprior = NULL;
	pbs_entlim_key_t *pkey = NULL;
	char	    rescn[PBS_MAX_RESC_NAME+1];
	char	    etname[PBS_MAX_RESC_NAME+1];
	char	   *pc;
	int	    needquotes;
	svrattrl   *pal;
	svrattrl   *tmpsvl;
	int	    len;
	enum batch_op   op = SET;
	svr_entlim_leaf_t  *plf;
	char	   **rescn_array;
	char 	   **temp_rescn_array;
	int	    index = 0;
	int	    i=0;
	int	    array_size = ENCODE_ENTITY_MAX;

	if (mode == ATR_ENCODE_DB)
		return (encode_entlim_db(attr, phead, atname, rsname, mode, rtnl));

	if (!attr)
		return (-1);
	if (!(attr->at_flags & ATR_VFLAG_SET))
		return (0);	/* nothing up the tree */

	ctx = attr->at_val.at_enty.ae_tree;

	/* ok, now process each separate entry in the tree */

	/* the call to entlim_get_next with a null key will allocate */
	/* space for a max sized key.  It needs to be freed.	     */

	pkey = entlim_get_next(NULL, ctx);

	rescn_array = malloc(array_size*sizeof(char *));
	if (rescn_array == NULL)
		return (PBSE_SYSTEM);


	while (pkey) {

		rescn[0] = '\0';
		needquotes = 0;

		plf = (svr_entlim_leaf_t *)(pkey->recptr);

		if ((entlim_entity_from_key(pkey, etname, PBS_MAX_RESC_NAME)==0) &&
			(entlim_resc_from_key(pkey, rescn, PBS_MAX_RESC_NAME) >= 0)) {

			/* decode leaf value into a local svrattrl structure in */
			/* order to obtain a string represnetation of the value */

			if (plf->slf_rescd->rs_encode(&plf->slf_limit, NULL, atname, rescn, mode, &tmpsvl) > 0) {

				/* Allocate the "real" svrattrl sufficiently large to     */
				/* hold the form "[l:entity;rname=value_string]" plus one */
				/* and assemble the real value into the real svrattrl     */

				/* [u:=]  null = 6 extra characters */
				len = tmpsvl->al_valln + strlen(etname) + 6;

				/* is there whitespace in the entity name ? */
				/* if so, then we quote the whole thing.    */
				pc = etname;
				while (*pc) {
					if (isspace((int)*pc++)) {
						needquotes = 1;
						len += 2;
						break;
					}
				}

				if (rescn[0] == '\0')
					pal = attrlist_create(atname, NULL, len);
				else
					pal = attrlist_create(atname, rescn, len);

				if (needquotes) {
					sprintf(pal->al_atopl.value, "[%c:\"%s\"=%s]",
						*pkey->key, etname, tmpsvl->al_atopl.value);
				} else {
					sprintf(pal->al_atopl.value, "[%c:%s=%s]",
						*pkey->key, etname, tmpsvl->al_atopl.value);
				}
				free(tmpsvl);
				pal->al_flags = attr->at_flags;
				op = SET;

				/* check whether the resource is appeared first time or is repeated */
				/* After check set the op accordingly */
				if(rescn != NULL) {
					for(i=0; i<index; i++) {
						if (strcmp(rescn, rescn_array[i]) == 0) {
							op = INCR;
							break;
						}
					}
					if (op == SET) {
						/* Doubling the size of array */
						if(index == array_size) {
							array_size = array_size*2;
							temp_rescn_array = realloc(rescn_array, array_size*sizeof(char *));
							if (temp_rescn_array != NULL) {
								rescn_array = temp_rescn_array;
							}
							else {
								for (i=0; i<index; i++)
									free(rescn_array[i]);
								free (rescn_array);
								return (PBSE_SYSTEM);
							}

						}
						rescn_array[index] = strdup(rescn);
						if (rescn_array[index] == NULL) {
							for (i=0; i<index; i++)
								free(rescn_array[i]);
							free (rescn_array);
							return (PBSE_SYSTEM);
						}
						index++;
					}
				}
				pal->al_atopl.op = op;
				if (phead)
					append_link(phead, &pal->al_link, pal);
				if (first) {
					if (rtnl)
						*rtnl  = pal;
					first = 0;
				} else {
					xprior->al_sister = pal;
				}
				xprior = pal;

				++grandtotal;
			}
		}
		pkey = entlim_get_next(pkey, ctx);
	}
	for (i=0; i<index; i++)
		free(rescn_array[i]);
	free (rescn_array);
	if (pkey)
		free(pkey);
	return (grandtotal);
}


/**
 * @brief
 * 	set_entlim - set value of an attribute of type ATR_TYPE_ENTITY to the
 *	value of another attribute of type ATR_TYPE_ENTITY.
 *
 * @par Functionality:
 *	This function is used for all operations on the etlim attributes
 *	EXCEPT for "SET" when the entlim involves a resource, see
 *	set_entlim_resc() below.
 *
 *	For each entity in the list headed by the "new" attribute,
 *	the correspondingly entity in the list headed by "old"
 *	is modified.
 *
 *	The mapping of the operations incr and decr depend on the type are
 *		SET:  all of old entries are replaced by the new entries
 *		INCR: if existing old key (matching new key),
 *		      it is replaced by new (old removed, then set)
 *		      if no existing old key (matching new key), then
 *		      same as set
 *		DECR: old is removed if (a) new has no Rvalue following the
 *		      entity's name or (b) new's Rvalue matches Old's Rvalue
 *
 * @param[in] old pointer to attribute with existing values to be modified
 * @param[in] new pointer to (temp) attribute with new values to be set
 * @param[in] op  set operator: SET, INCR, DECR
 *
 * @return 	int
 * @retval	0 	if ok
 * @retval	>0 	if error
 *
 */

int
set_entlim(attribute *old, attribute *new, enum batch_op op)
{
	pbs_entlim_key_t  *pkey;
	void              *newctx;
	void              *oldctx;
	svr_entlim_leaf_t *newptr;
	svr_entlim_leaf_t *exptr;
	attribute	   save_old;

	assert(old && new && (new->at_flags & ATR_VFLAG_SET));

	switch (op) {
		case SET:
			/* free the old, reinitialize it and then set old  */
			/* to to new by by falling into the "INCR" case    */
			save_old = *old;
			old->at_val.at_enty.ae_tree = entlim_initialize_ctx();
			if (old->at_val.at_enty.ae_tree == NULL) {
				*old = save_old;
				return (PBSE_SYSTEM);
			}
			free_entlim(&save_old);	/* have new alloc, discard the saved */
			/* fall into INCR case */

		case INCR:
			/* walk "new" and for each leaf, add it to "old" */
			pkey = NULL;
			newctx = new->at_val.at_enty.ae_tree;
			if (old->at_val.at_enty.ae_tree == NULL) {
				/* likely the += without any prior values */
				old->at_val.at_enty.ae_tree = entlim_initialize_ctx();
			}
			oldctx = old->at_val.at_enty.ae_tree;
			while ((pkey = entlim_get_next(pkey, newctx)) != NULL) {
				/* duplicate the record to be added */
				newptr = dup_svr_entlim_leaf(pkey->recptr);
				if (newptr) {
					if (entlim_replace(pkey->key, newptr, oldctx, svr_freeleaf) != 0) {
						/* failed to add */
						svr_freeleaf(newptr);
						free(pkey);
						return (PBSE_SYSTEM);
					}
				}
			}
			old->at_val.at_enty.ae_newlimittm = time(0);
			break;

		case DECR:

			if ((old->at_flags & ATR_VFLAG_SET) == 0) {
				/* nothing to unset, just return as done */
				return 0;
			}

			/* walk "new" and for each leaf, remove matching from "old" */
			/* if no "value" for new leaf, then remove if keys match    */
			/* if new leaf has a value, remove old only if values match */

			pkey = NULL;
			newctx = new->at_val.at_enty.ae_tree;
			oldctx = old->at_val.at_enty.ae_tree;

			while ((pkey = entlim_get_next(pkey, newctx)) != NULL) {
				/* "exptr" points to record in "old" attribute */
				if ((exptr = entlim_get(pkey->key, oldctx)) != NULL) {

					/* found existing ("old") record with matching key */
					newptr = pkey->recptr; /* value of item being removed */
					if (newptr->slf_limit.at_flags & ATR_VFLAG_SET) {

						int  (*compf)(attribute *pattr, attribute *with);

						/* user specifed a value that must match current */
						/* if the current one is to be deleted           */
						char rsbuf[PBS_MAX_RESC_NAME+1];
						resource_def *prdef;

						if (entlim_resc_from_key(pkey, rsbuf, PBS_MAX_RESC_NAME) == 0) {

							/* find compare function for this resource */
							prdef=find_resc_def(svr_resc_def, rsbuf, svr_resc_size);
							if (prdef)
								compf = prdef->rs_comp;
							else
								compf = comp_l; /* default unknown resc to long */

						} else {
							compf = comp_l; /* no resource, use long type */
						}
						if (compf(&newptr->slf_limit, &exptr->slf_limit) == 0) {
							/* value matches, delete "old" */
							(void)entlim_delete(pkey->key, oldctx, svr_freeleaf);
						}
					} else {
						/* DECR (a) case in function block comment, */
						/* no value supplied which must match, just */
						/* delete "old"				*/
						(void)entlim_delete(pkey->key, oldctx, svr_freeleaf);
					}
				}
			}
			/* having removed one or more elements from the value tree */
			/* see if any entries are left or if the value is now null */
			pkey = NULL;
			if ((pkey = entlim_get_next(pkey, oldctx)) == NULL) {
				/* no entries left set, clear the entire attribute */
				free_entlim(old);
				/* set _MODIFY flag so up level functions */
				/* know the attribute has been changed    */
				old->at_flags |= ATR_VFLAG_MODIFY;
				return (0);
			}
			free(pkey);
			break;

		default:	return (PBSE_INTERNAL);
	}

	old->at_flags |= ATR_VFLAG_SET | ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	return (0);
}

/**
 * @brief
 *	set_entlim_res - set value of attribute of type ATR_TYPE_ENTITY to another
 *	This function is used for all operations on the etlim attributes which
 *	involves a resource.  However, except for the "SET" operation, the
 *	operations are identical to set_entlim() above and that function does
 *	the real work.
 *
 *	For each entity in the list headed by the "new" attribute,
 *	the correspondingly entity in the list headed by "old"
 *	is modified.
 *
 *	The mapping of the operations incr and decr depend on the type are
 *	   ****	SET:  all of old wth same resource are replace by new ****
 *		INCR: if existing old key (matching new key),
 *		      it is replaced by new (old removed, then set)
 *		      if no existing old key (matching new key), then
 *		      same as set
 *		DECR: old is removed if (a) new has no Rvalue following the
 *		      entity's name or (b) new's Rvalue matches Old's Rvalue
 * @param old pointer to attribute with existing values to be modified
 * @param new pointer to (temp) attribute with new values to be set
 * @param op  set operator: SET, INCR, DECR
 *
 * @return	int
 * @retval	0 	if ok
 * @retval	>0 	if error
 *
 */

int
set_entlim_res(attribute *old, attribute *new, enum batch_op op)
{
	pbs_entlim_key_t  *pkeynew;
	pbs_entlim_key_t  *pkeyold;
	void              *newctx;
	void              *oldctx;
	char		   newresc[PBS_MAX_RESC_NAME+1];
	char		   oldresc[PBS_MAX_RESC_NAME+1];

	assert(old && new && (new->at_flags & ATR_VFLAG_SET));

	if (op == SET) {

		if (old->at_val.at_enty.ae_tree == NULL) {
			/* nothing in old, change op to INCR and use */
			/* other set_entlim function		     */
			op = INCR;
			return (set_entlim(old, new, op));
		}

		newctx = new->at_val.at_enty.ae_tree;
		oldctx = old->at_val.at_enty.ae_tree;

		/* walk the new tree identifying which resources are */
		/* being changed,  walk the old tree and remove any  */
		/* record with the same resource in its key	     */
		pkeynew = NULL;
		while ((pkeynew = entlim_get_next(pkeynew, newctx)) != NULL) {
			/* get the resource name from the "new" key */
			if (entlim_resc_from_key(pkeynew, newresc, PBS_MAX_RESC_NAME) != 0)
				continue;	/* no resc, go to next */

			pkeyold = NULL;
			while ((pkeyold = entlim_get_next(pkeyold, oldctx)) != NULL) {
				/* get the resource name from the "old" key */
				if (entlim_resc_from_key(pkeyold, oldresc, PBS_MAX_RESC_NAME) != 0)
					continue;    /* no resc, go to next */

				/* if old and new resource names match, */
				/* delete old record			*/
				if (strcasecmp(oldresc, newresc) == 0) {
					(void)entlim_delete(pkeyold->key,
						oldctx,
						svr_freeleaf);
				}
			}

		}

		/* now the operation is the same as an INCR, adding	*/
		/* new values and thus we change the operator and use	*/
		/* the set_entlim() code above				*/
		op = INCR;
	}

	/* The other operators (and the SET turned into INCR)	*/
	/* use the set_entlim() code above			*/

	return (set_entlim(old, new, op));
}


/**
 * @brief
 * 	free_entlim - free space associated with attribute value
 *
 *	For each leaf in the tree, the associated structure is freed,
 *	and the key is deleted until the tree is complete pruned.  Then
 *	the tree itself is uprooted and placed in the compost pile.
 *
 * @param[in] pattr - pointer to attrbute
 *
 * @return	Void
 *
 */

void
free_entlim(attribute *pattr)
{
	/* entlim_free_cts walks tree and for each leaf,  */
	/* prunes it and then uproots the tree (frees it) */

	if (pattr->at_val.at_enty.ae_tree)
		(void)entlim_free_ctx(pattr->at_val.at_enty.ae_tree, svr_freeleaf);

	/* now clear the basic attribute */
	pattr->at_val.at_enty.ae_newlimittm = 0;
	free_null(pattr);
	return;

}

/**
 * @brief
 *	Unset the entity limits for a specific resource (rather than the
 *	entire attribute).  For example,  unset the limits on "ncpus" while
 *	leaving the "mem" limits set.
 *
 * @param[in] pattr - pointer to the attribute
 * @param[in] rescname - name of resource for which limits are to be unset
 *
 * @return	Void
 *
 */

void
unset_entlim_resc(attribute *pattr, char *rescname)
{
	void *oldctx;
	pbs_entlim_key_t *pkey = NULL;
	char rsbuf[PBS_MAX_RESC_NAME+1];
	int  modified = 0;
	int  hasentries = 0;

	if (((pattr->at_flags & ATR_VFLAG_SET) == 0) ||
		(rescname == NULL) ||
		(*rescname == '\0'))
		return;	/* nothing to unset */

	/* walk "old" and for each leaf, remove */
	/* entry with matching  resource name   */

	pkey = NULL;
	oldctx = pattr->at_val.at_enty.ae_tree;

	while ((pkey = entlim_get_next(pkey, oldctx)) != NULL) {

		hasentries = 1;	/* found at least one (remaining) entry */

		if (entlim_resc_from_key(pkey, rsbuf, PBS_MAX_RESC_NAME) == 0) {
			if (strcasecmp(rsbuf, rescname) == 0) {

				(void)entlim_delete(pkey->key, oldctx,
					svr_freeleaf);

				/*
				 * now restart search from beginning as we are
				 * not sure what the deletion did to the order
				 */
				free(pkey);
				pkey = NULL;
				modified = 1;
				hasentries = 0; /* will see any in next pass */
			}


		}
	}
	if (modified)
		pattr->at_flags |= ATR_VFLAG_MODIFY | ATR_VFLAG_MODCACHE;
	if (hasentries == 0)
		free_entlim(pattr);	/* no entries left, clear attribute */
	return;
}
