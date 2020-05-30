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
 * @file    db_svr.c
 *
 * @brief
 *      Implementation of the svr data access functions for postgres
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include <errno.h>
#include "db_postgres.h"

extern char *errmsg_cache;

/**
 * @brief
 *	Prepare all the server related sqls. Typically called after connect
 *	and before any other sql exeuction
 *
 * @param[in]	conn - Database connection handle
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pbs_db_prepare_svr_sqls(void *conn)
{
	char conn_sql[MAX_SQL_LENGTH];

	snprintf(conn_sql, MAX_SQL_LENGTH, "insert into pbs.server( "
		"sv_savetm, "
		"sv_creattm, "
		"attributes "
		") "
		"values "
		"(localtimestamp, localtimestamp, hstore($1::text[])) "
		"returning to_char(sv_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as sv_savetm");
	if (db_prepare_stmt(conn, STMT_INSERT_SVR, conn_sql, 1) != 0)
		return -1;

	/* replace all attributes for a FULL update */
	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.server set "
		"sv_savetm = localtimestamp, "
		"attributes = attributes || hstore($1::text[]) "
		"returning to_char(sv_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as sv_savetm");
	if (db_prepare_stmt(conn, STMT_UPDATE_SVR, conn_sql, 1) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "update pbs.server set "
		"sv_savetm = localtimestamp,"
		"attributes = attributes - $1::text[] "
		"returning to_char(sv_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as sv_savetm");
	if (db_prepare_stmt(conn, STMT_REMOVE_SVRATTRS, conn_sql, 1) != 0)
		return -1;

	snprintf(conn_sql, MAX_SQL_LENGTH, "select "
		"to_char(sv_savetm, 'YYYY-MM-DD HH24:MI:SS.US') as sv_savetm, "
		"hstore_to_array(attributes) as attributes "
		"from "
		"pbs.server ");
	if (db_prepare_stmt(conn, STMT_SELECT_SVR, conn_sql, 0) != 0)
		return -1;

	return 0;
}

/**
 * @brief
 *	Truncate all data from ALL tables from the database
 *
 * @param[in]	conn - The database connection handle
 *
 * @return      Error code
 * @retval	-1 - Failure
 *		 0 - Success
 *
 */
int
pbs_db_truncate_all(void *conn)
{
	char conn_sql[MAX_SQL_LENGTH]; /* sql buffer */

	snprintf(conn_sql, MAX_SQL_LENGTH, "truncate table 	"
		"pbs.scheduler, "
		"pbs.node, "
		"pbs.queue, "
		"pbs.resv, "
		"pbs.job_scr, "
		"pbs.job, "
		"pbs.server");

	if (db_execute_str(conn, conn_sql) == -1)
		return -1;

	return 0;
}
/**
 * @brief
 *	Insert server data into the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Information of server to be inserted
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 *
 */
int
pbs_db_save_svr(void *conn, pbs_db_obj_info_t *obj, int savetype)
{
	pbs_db_svr_info_t *ps = obj->pbs_db_un.pbs_db_svr;
	char *stmt = NULL;
	int params;
	char *raw_array = NULL;
	int len = 0;
	static int sv_savetm_fnum;
	static int fnums_inited = 0;
	PGresult *res;

	/* Svr does not have a QS area, so ignoring that */

	/* are there attributes to save to memory or local cache? */
	if (ps->cache_attr_list.attr_count > 0) {
		dist_cache_save_attrs("server", &ps->cache_attr_list);
	}

	if ((ps->db_attr_list.attr_count > 0) || (savetype & OBJ_SAVE_NEW)) {
		/* convert attributes to postgres raw array format */
		if ((len = attrlist_2_dbarray(&raw_array, &ps->db_attr_list)) <= 0)
			return -1;

		SET_PARAM_BIN(conn_data, raw_array, len, 0);
		params = 1;
		stmt = STMT_UPDATE_SVR;
	}

	if (savetype & OBJ_SAVE_NEW) {
		stmt = STMT_INSERT_SVR;
		/* reinitialize schema by dropping PBS schema */
		if (pbs_db_truncate_all(conn) == -1) {
			db_set_error(conn, &errmsg_cache, "Could not truncate PBS data", stmt, "");
			return -1;
		}
	}

	if (stmt != NULL) {
		if (db_cmd(conn, stmt, params, &res) != 0) {
			free(raw_array);
			return -1;
		}

		if (fnums_inited == 0) {
			sv_savetm_fnum = PQfnumber(res, "sv_savetm");
			fnums_inited = 1;
		}
		GET_PARAM_STR(res, 0, ps->sv_savetm, sv_savetm_fnum);
		PQclear(res);
		free(raw_array);
	}

	return 0;
}

/**
 * @brief
 *	Load server data from the database
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj  - Load server information into this object
 *
 * @return      Error code
 * @retval	-1 - Failure
 * @retval	 0 - Success
 * @retval	 1 -  Success but no rows loaded
 *
 */
int
pbs_db_load_svr(void *conn, pbs_db_obj_info_t *obj)
{
	PGresult *res;
	int rc;
	char *raw_array;
	pbs_db_svr_info_t *ps = obj->pbs_db_un.pbs_db_svr;
	static int sv_savetm_fnum, attributes_fnum;
	static int fnums_inited = 0;

	if ((rc = db_query(conn, STMT_SELECT_SVR, 0, &res)) != 0)
		return rc;

	if (fnums_inited == 0) {
		sv_savetm_fnum = PQfnumber(res, "sv_savetm");
		attributes_fnum = PQfnumber(res, "attributes");
		fnums_inited = 1;
	}

	GET_PARAM_STR(res, 0, ps->sv_savetm, sv_savetm_fnum);
	GET_PARAM_BIN(res, 0, raw_array, attributes_fnum);

	/* convert attributes from postgres raw array format */
	rc = dbarray_2_attrlist(raw_array, &ps->db_attr_list);

	PQclear(res);

	if (rc == 0) {
		/* in case of multi-server, also read NOSAVM attributes from distributed cache */
		/* call in this functions since all call paths lead to this before decode */
		//if (use_dist_cache) {
		//	dist_cache_recov_attrs("server", &ps->sv_savetm, &ps->cache_attr_list);
		//}
	}

	return rc;
}

/**
 * @brief
 *	Deletes attributes of a server
 *
 * @param[in]	conn - Connection handle
 * @param[in]	obj_id  - server id
 * @param[in]	sv_time  - server save timestamp
 * @param[in]	attr_list - List of attributes
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - On Failure
 *
 */
int
pbs_db_del_attr_svr(void *conn, void *obj_id, char *sv_time, pbs_db_attr_list_t *attr_list)
{
	char *raw_array = NULL;
	int len = 0;
	static int sv_savetm_fnum;
	static int fnums_inited = 0;
	PGresult *res;

	if ((len = attrlist_2_dbarray_ex(&raw_array, attr_list, 1)) <= 0)
		return -1;

	SET_PARAM_BIN(conn_data, raw_array, len, 0);

	if (db_cmd(conn, STMT_REMOVE_SVRATTRS, 1, &res) != 0) {
		PQclear(res);
		return -1;
	}

	if (fnums_inited == 0) {
		sv_savetm_fnum = PQfnumber(res, "sv_savetm");
		fnums_inited = 1;
	}
	GET_PARAM_STR(res, 0, sv_time, sv_savetm_fnum);
	PQclear(res);
	free(raw_array);

	return 0;
}
