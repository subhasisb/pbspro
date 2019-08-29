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
 * @file    db_postgres_common.c
 *
 * @brief
 *      This file contains Postgres specific implementation of functions
 *	to access the PBS postgres database.
 *	This is postgres specific data store implementation, and should not be
 *	used directly by the rest of the PBS code.
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include "db_postgres.h"
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h>
#include "ticket.h"

#define IPV4_STR_LEN	15

extern int pbs_decrypt_pwd(char *, int, size_t, char **);

/**
 * @brief
 *	Function to set the database error into the db_err field of the
 *	connection object
 *
 * @param[in]	conn - The connnection handle
 * @param[in]	fnc - Custom string added to the error message
 *			This can be used to provide the name of the
 *			functionality.
 * @param[in]	msg - Custom string added to the error message. This can be
 *			used to provide a failure message.
 */
void
pg_set_error(pbs_db_conn_t *conn, char *fnc, char *msg)
{
	char *str;
	char *p;
	char fmt[] = "%s %s failed: %s";

	if (conn->conn_db_err) {
		free(conn->conn_db_err);
		conn->conn_db_err = NULL;
	}

	str = PQerrorMessage((PGconn *) conn->conn_db_handle);
	if (!str)
		return;

	p = str + strlen(str) - 1;
	while ((p >= str) && (*p == '\r' || *p == '\n'))
		*p-- = 0; /* supress the last newline */

	conn->conn_db_err = malloc(strlen(fnc) + strlen(msg) +
		strlen(str) + sizeof(fmt) + 1);
	if (!conn->conn_db_err)
		return;

	sprintf((char *) conn->conn_db_err, fmt, fnc, msg, str);
#ifdef DEBUG
	printf("%s\n", (char *) conn->conn_db_err);
	fflush(stdout);
#endif
}


/**
 * @brief
 *	Function to prepare a database statement
 *
 * @param[in]	conn - The connnection handle
 * @param[in]	stmt - Name of the statement
 * @param[in]	sql  - The string sql that has to be prepared
 * @param[in]	num_vars - The number of parameters in the sql ($1, $2 etc)
 *
 * @return      Error code
 * @retval	-1 Failure
 * @retval	 0 Success
 *
 */
int
pg_prepare_stmt(pbs_db_conn_t *conn, char *stmt, char *sql,
	int num_vars)
{
	PGresult *res;
	res = PQprepare((PGconn*) conn->conn_db_handle,
		stmt,
		sql,
		num_vars,
		NULL);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		pg_set_error(conn, "Prepare of statement", stmt);
		PQclear(res);
		return -1;
	}
	PQclear(res);
	return 0;
}


/**
 * @brief
 *	Execute a prepared DML (insert or update) statement
 *
 * @param[in]	conn - The connnection handle
 * @param[in]	stmt - Name of the statement (prepared previously)
 * @param[in]	num_vars - The number of parameters in the sql ($1, $2 etc)
 *
 * @return      Error code
 * @retval	-1 - Execution of prepared statement failed
 * @retval	 0 - Success and > 0 rows were affected
 * @retval	 1 - Execution succeeded but statement did not affect any rows
 *
 *
 */
int
pg_db_cmd(pbs_db_conn_t *conn, char *stmt, int num_vars)
{
	PGresult *res;
	char *rows_affected = NULL;

	res = PQexecPrepared((PGconn*) conn->conn_db_handle,
		stmt,
		num_vars,
		((pg_conn_data_t *) conn->conn_data)->paramValues,
		((pg_conn_data_t *) conn->conn_data)->paramLengths,
		((pg_conn_data_t *) conn->conn_data)->paramFormats,
		0);
	if (PQresultStatus(res) != PGRES_COMMAND_OK) {
		pg_set_error(conn, "Execution of Prepared statement", stmt);
		PQclear(res);
		return -1;
	}
	rows_affected = PQcmdTuples(res);
	/*
	 *  we can't call PQclear(res) yet, since rows_affected
	 * (used below) is a pointer to a field inside res (PGresult)
	 */
	if (rows_affected == NULL || strtol(rows_affected, NULL, 10) <= 0) {
		PQclear(res);
		return 1;
	}
	PQclear(res);

	return 0;
}

/**
 * @brief
 *	Execute a prepared query (select) statement
 *
 * @param[in]	conn - The connnection handle
 * @param[in]	stmt - Name of the statement (prepared previously)
 * @param[in]	num_vars - The number of parameters in the sql ($1, $2 etc)
 * @param[in]	lock - query for update or not (lock row/object)
 * @param[out]  res - The result set of the query
 *
 * @return      Error code
 * @retval	-1 - Execution of prepared statement failed
 * @retval	 0 - Success and > 0 rows were returned
 * @retval	 1 - Execution succeeded but statement did not return any rows
 *
 */
int
pg_db_query(pbs_db_conn_t *conn, char *stmt, int num_vars, int lock, PGresult **res)
{
	char stmt_tmp[100];
	ExecStatusType res_rc;
	if (lock) {
		strcpy(stmt_tmp, stmt);
		strcat(stmt_tmp, "_locked");
	}
	
	*res = PQexecPrepared((PGconn*) conn->conn_db_handle,
		(lock? stmt_tmp : stmt),
		num_vars,
		((pg_conn_data_t *) conn->conn_data)->paramValues,
		((pg_conn_data_t *) conn->conn_data)->paramLengths,
		((pg_conn_data_t *) conn->conn_data)->paramFormats,
		conn->conn_result_format);

	res_rc = PQresultStatus(*res);
	if (res_rc != PGRES_TUPLES_OK) {
		pg_set_error(conn, "Execution of Prepared statement", stmt);
		PQclear(*res);
		return -1;
	}

	if (PQntuples(*res) <= 0) {
		PQclear(*res);
		return 1;
	}
	conn->conn_resultset = *res; /* store, since caller will retrieve results */
	return 0;
}

/**
 * @brief
 *	resize a buffer. The buffer structure stores the current size of the
 *	buffer. This function determines how much of that buffer size if
 *	free and expands the buffer accordingly
 *
 * @param[in]	dest - buffer to resize
 * @param[in]	size - Size that is required
 *
 * @return      Error code
 * @retval	 0 - Success
 * @retval	-1 - Failure to allocate new memory
 *
 */
int
resize_buff(pbs_db_sql_buffer_t *dest, int size)
{
	char *tmp;
	int used = 0;

	if (dest->buff)
		used = strlen(dest->buff); /* buff is string */

	if (size > (dest->buf_len - used)) {
		/* resize the buffer now */
		tmp = realloc(dest->buff, dest->buf_len + size*2);
		if (!tmp) {
			return -1;
		}
		dest->buff = tmp;
		dest->buf_len += size*2;
	}
	return 0;
}

/**
 * @brief
 *	Retrieves the database password for an user. Currently, the database
 *	password is retrieved from the file under server_priv, called db_passwd
 *	Currently, this function returns the same username as the password, if
 *	a password file is not found under server_priv. However, if a password
 *	file is found but is not readable etc, then an error (indicated by
 *	returning NULL) is returned.
 *
 * @param[in]	user - Name of the user
 * @param[out]  errmsg - Details of the error
 * @param[in]   len    - length of error messge variable
 *
 * @return      Password String
 * @retval	 NULL - Failed to retrieve password
 * @retval	!NULL - Pointer to allocated memory with password string.
 *			Caller should free this memory after usage.
 *
 */
char *
pbs_get_dataservice_password(char *user, char *errmsg, int len)
{
	char pwd_file[MAXPATHLEN+1];
	int fd;
	struct stat st;
	char buf[MAXPATHLEN+1];
	char *str;

#ifdef WIN32
	sprintf(pwd_file, "%s\\server_priv\\db_password", pbs_conf.pbs_home_path);
	if ((fd = open(pwd_file, O_RDONLY | O_BINARY)) == -1)
#else
	sprintf(pwd_file, "%s/server_priv/db_password", pbs_conf.pbs_home_path);
	if ((fd = open(pwd_file, O_RDONLY)) == -1)
#endif
	{
		return strdup(user);
	} else {
		if (fstat(fd, &st) == -1) {
			close(fd);
			snprintf(errmsg, len, "%s: stat failed, errno=%d", pwd_file, errno);
			return NULL;
		}
		if (st.st_size >= sizeof(buf)) {
			close(fd);
			snprintf(errmsg, len, "%s: file too large", pwd_file);
			return NULL;
		}

		if (read(fd, buf, st.st_size) != st.st_size) {
			close(fd);
			snprintf(errmsg, len, "%s: read failed, errno=%d", pwd_file, errno);
			return NULL;
		}
		buf[st.st_size] = 0;
		close(fd);

		if (pbs_decrypt_pwd(buf, PBS_CREDTYPE_AES, st.st_size, &str) != 0)
			return NULL;

		return (str);
	}
}

/**
 * @brief
 *	Escape any special characters contained in a database password.
 *	The list of such characters is found in the description of PQconnectdb
 *	at http://www.postgresql.org/docs/8.3/static/libpq-connect.html.
 *
 * @param[out]	dest - destination string, which will hold the escaped password
 * @param[in]	src - the original password string, which may contain characters
 *		      that must be escaped
 * @param[in]   len - amount of space in the destination string;  to ensure
 *		      successful conversion, this value should be at least one
 *		      more than twice the length of the original password string
 *
 * @return      void
 *
 */
void
escape_passwd(char *dest, char *src, int len)
{
	char *p = dest;

	while (*src && ((p - dest) < len)) {
		if (*src == '\'' || *src == '\\') {
			*p = '\\';
			p++;
		}
		*p = *src;
		p++;
		src++;
	}
	*p = '\0';
}

/**
 * @brief
 *	Creates the database connect string by retreiving the
 *      database password and appending the other connection
 *      parameters.
 *	If parameter host is passed as NULL, then the "host =" portion
 *	of the connection info is not set, allowing the database to
 *	connect to the default host (which is local).
 *
 * @param[in]   host - The hostname to connect to, if NULL the not used
 * @param[in]   timeout - The timeout parameter of the connection
 * @param[in]   err_code - The error code in case of failure
 * @param[out]  errmsg - Details of the error
 * @param[in]   len    - length of error messge variable
 *
 * @return      The newly allocated and populated connection string
 * @retval       NULL  - Failure
 * @retval       !NULL - Success
 *
 */
char *
pbs_get_connect_string(char *host, int timeout, int *err_code, char *errmsg, int len)
{
	char		*svr_conn_info;
	int			pquoted_len = 0;
	char		*p = NULL, *pquoted = NULL;
	char		*usr = NULL;
	pbs_net_t	hostaddr;
	struct 		in_addr in;
	char		hostaddr_str[IPV4_STR_LEN + 1];
	char		*q;
	char		template1[]="hostaddr = '%s' port = %d dbname = '%s' user = '%s' password = '%s' "
		"connect_timeout = %d";
	char		template2[]="port = %d dbname = '%s' user = '%s' password = '%s' "
		"connect_timeout = %d";

	usr = pbs_get_dataservice_usr(errmsg, len);
	if (usr == NULL) {
		*err_code = PBS_DB_AUTH_FAILED;
		return NULL;
	}

	p = pbs_get_dataservice_password(usr, errmsg, len);
	if (p == NULL) {
		free(usr);
		*err_code = PBS_DB_AUTH_FAILED;
		return NULL;
	}

	pquoted_len = strlen(p) * 2 + 1;
	pquoted = malloc(pquoted_len);
	if (!pquoted) {
		free(p);
		free(usr);
		*err_code = PBS_DB_NOMEM;
		return NULL;
	}

	escape_passwd(pquoted, p, pquoted_len);

	svr_conn_info = malloc(MAX(sizeof(template1), sizeof(template2)) +
		((host)?IPV4_STR_LEN:0) + /* length of IPv4 only if host is not NULL */
		5 + /* possible length of port */
		strlen(PBS_DATA_SERVICE_STORE_NAME) +
		strlen(usr) + /* NULL checked earlier */
		strlen(p) + /* NULL checked earlier */
		10); /* max 9 char timeout + null char */
	if (svr_conn_info == NULL) {
		free(pquoted);
		free(p);
		free(usr);
		*err_code = PBS_DB_NOMEM;
		return NULL;
	}

	if (host == NULL) {
		sprintf(svr_conn_info,
			template2,
			pbs_conf.pbs_data_service_port,
			PBS_DATA_SERVICE_STORE_NAME,
			usr,
			pquoted,
			timeout);
	} else {
		if ((hostaddr = get_hostaddr(host)) == (pbs_net_t)0) {
			free(pquoted);
			free(p);
			free(usr);
			snprintf(errmsg, len, "Could not resolve dataservice host %s", host);
			*err_code = PBS_DB_CONNFAILED;
			return NULL;
		}
		in.s_addr = htonl(hostaddr);
		q = inet_ntoa(in);
		if (!q) {
			free(pquoted);
			free(p);
			free(usr);
			snprintf(errmsg, len, "inet_ntoa failed, errno=%d", errno);
			*err_code = PBS_DB_CONNFAILED;
			return NULL;
		}
		strncpy(hostaddr_str, q, IPV4_STR_LEN);
		hostaddr_str[IPV4_STR_LEN] = '\0';

		sprintf(svr_conn_info,
			template1,
			hostaddr_str,
			pbs_conf.pbs_data_service_port,
			PBS_DATA_SERVICE_STORE_NAME,
			usr,
			pquoted,
			timeout);
	}
	memset(p, 0, strlen(p)); 			 /* clear password from memory */
	memset(pquoted, 0, strlen(pquoted)); /* clear password from memory */
	free(pquoted);
	free(p);
	free(usr);

	return svr_conn_info;
}

#ifdef WIN32
void
repl_slash(char *path)
{
	char *p = path;
	while (*p) {
		if (*p == '/')
			*p = '\\';
		p++;
	}
}
#endif

/**
 * @brief
 *	Function to start/stop the database service/daemons
 *	Basically calls the pbs_dataservice script/batch file with
 *	the specified command. It adds a second parameter
 *	"PBS" to the command string. This way the script/batch file
 *	knows that the call came from the pbs code rather than
 *	being invoked from commandline by the admin
 *
 * @return      Error code
 * @retval       !=0 - Failure
 * @retval         0 - Success
 *
 */
int
pbs_dataservice_control(char *cmd, char **errmsg)
{
	char dbcmd[2*MAXPATHLEN+1];
	int rc = 0;
	char errfile[MAXPATHLEN+1];
	struct stat stbuf;
	int fd;
	char *p;
#ifdef WIN32
	char buf[MAXPATHLEN+1];
#endif

	if (*errmsg != NULL) {
		free(*errmsg);
		*errmsg = NULL;
	}

#ifdef WIN32
	strcpy(buf, pbs_conf.pbs_home_path);
	repl_slash(buf);
	/* create unique filename by appending pid */
	sprintf(errfile, "%s\\spool\\db_errfile_%s_%d",
		buf, cmd, getpid());

	/* execute service startup and redirect output to file */
	strcpy(buf, pbs_conf.pbs_exec_path);
	repl_slash(buf);
	sprintf(dbcmd,
		"%s\\sbin\\pbs_dataservice %s PBS %d > %s 2>&1",
		buf,
		cmd,
		pbs_conf.pbs_data_service_port,
		errfile);
	rc = wsystem(dbcmd, INVALID_HANDLE_VALUE);
#else
	/* create unique filename by appending pid */
	sprintf(errfile, "%s/spool/db_errfile_%s_%d",
		pbs_conf.pbs_home_path,
		cmd,
		getpid());

	/* execute service startup and redirect output to file */
	sprintf(dbcmd,
		"PBS_CONF_FILE=%s; "
		"export PBS_CONF_FILE;"
		"%s/sbin/pbs_dataservice %s PBS %d > %s 2>&1",
		pbs_conf.pbs_conf_file,
		pbs_conf.pbs_exec_path,
		cmd,
		pbs_conf.pbs_data_service_port,
		errfile);
	rc = system(dbcmd);
	if (WIFEXITED(rc))
		rc = WEXITSTATUS(rc);
#endif

	if (rc != 0) {
		/* read the contents of errfile and load to errmsg */
		if ((fd = open(errfile, 0)) != -1) {
			if (fstat(fd, &stbuf) != -1) {
				*errmsg = malloc(stbuf.st_size+1);
				if (*errmsg == NULL) {
					close(fd);
					unlink(errfile);
					return -1;
				}
				read(fd, *errmsg, stbuf.st_size);
				*(*errmsg+stbuf.st_size)=0;
				p = *errmsg + strlen(*errmsg) - 1;
				while ((p >= *errmsg) && (*p == '\r' || *p == '\n'))
					*p-- = 0; /* suppress the last newline */
			}
			close(fd);
		}
	}
	unlink(errfile);
	return rc;
}

/**
 * @brief
 *	Function to check whether data-service is running
 *
 * @return      Error code
 * @retval      -1  - Error in routine
 * @retval       0  - Data service running on local host
 * @retval       1  - Data service not running
 * @retval       2  - Data service running on another host
 *
 */
int
pbs_status_db(char **errmsg)
{
	return (pbs_dataservice_control(PBS_DB_CONTROL_STATUS, errmsg));
}

/**
 * @brief
 *	Start the database daemons/service in synchronous mode.
 *  This function waits for the database to complete startup.
 *
 * @param[out]	errmsg - returns the startup error message if any
 *
 * @return       int
 * @retval       0     - success
 * @retval       !=0   - Failure
 *
 */
int
pbs_startup_db(char **errmsg)
{
	return (pbs_dataservice_control(PBS_DB_CONTROL_START, errmsg));
}

/**
 * @brief
 *	Start the database daemons/service in asynchronous mode.
 * This function does not wait for the database to complete startup.
 *
 * @param[out]	errmsg - returns the startup error message if any
 *
 * @return      int
 * @retval       0    - success
 * @retval       !=0  - Failure
 *
 */
int
pbs_startup_db_async(char **errmsg)
{
	return (pbs_dataservice_control(PBS_DB_CONTROL_STARTASYNC, errmsg));
}

/**
 * @brief
 *	Function to stop the database service/daemons
 *	This passes the parameter STOP to the
 *	pbs_dataservice script.
 *
 * @param[out]	errmsg - returns the db error message if any
 *
 * @return      Error code
 * @retval       !=0 - Failure
 * @retval        0  - Success
 *
 */
int
pbs_shutdown_db(char **errmsg)
{
	return (pbs_dataservice_control(PBS_DB_CONTROL_STOP, errmsg));
}

/**
 * @brief
 *	Function to stop the database service/daemons
 *	in an asynchronous manner.
 *	This passes the parameter STOPASYNC to the
 *	pbs_dataservice script, which initiates the
 *	the database stop and returns without waiting.
 *
 * @param[out]	errmsg - returns the db error message if any
 *
 * @return      Error code
 * @retval       !=0 - Failure
 * @retval        0  - Success
 *
 */
int
pbs_shutdown_db_async(char **errmsg)
{
	return (pbs_dataservice_control(PBS_DB_CONTROL_STOPASYNC, errmsg));
}

/**
 * @brief
 *	Function to escape special characters in a string
 *	before using as a column value in the database
 *
 * @param[in]	conn - Handle to the database connection
 * @param[in]	str - the string to escape
 *
 * @return      Escaped string
 * @retval        NULL - Failure to escape string
 * @retval       !NULL - Newly allocated area holding escaped string,
 *                       caller needs to free
 *
 */
char *
pbs_db_escape_str(pbs_db_conn_t *conn, char *str)
{
	char *val_escaped;
	int error;
	int val_len;

	if (str == NULL)
		return NULL;

	val_len = strlen(str);
	/* Use calloc() to ensure the character array is initialized. */
	val_escaped = calloc(((2*val_len) + 1), sizeof(char)); /* 2*orig + 1 as per Postgres API documentation */
	if (val_escaped == NULL)
		return NULL;

	PQescapeStringConn((PGconn*) conn->conn_db_handle,
		val_escaped, str,
		val_len, &error);
	if (error != 0) {
		free(val_escaped);
		return NULL;
	}

	return val_escaped;
}

/**
 * @brief
 *	Translates the error code to an error message
 *
 * @param[in]   err_code - Error code to translate
 * @param[out]   err_msg - The translated error message (newly allocated memory)
 *
 */
void
get_db_errmsg(int err_code, char **err_msg)
{
	if (*err_msg) {
		free(*err_msg);
		*err_msg = NULL;
	}

	switch (err_code) {
		case PBS_DB_STILL_STARTING:
			*err_msg = strdup("PBS dataservice is still starting up");
			break;

		case PBS_DB_AUTH_FAILED:
			*err_msg = strdup("PBS dataservice authentication failed");
			break;

		case PBS_DB_NOMEM:
			*err_msg = strdup("PBS out of memory in connect");
			break;

		case PBS_DB_CONNREFUSED:
			*err_msg = strdup("PBS dataservice not running");
			break;

		case PBS_DB_CONNFAILED:
			*err_msg = strdup("Failed to connect to PBS dataservice");
			break;

		default:
			*err_msg = strdup("PBS dataservice error");
			break;
	}
}

/**
 * @brief
 *	Free the connect string associated with a connection
 *
 * @param[in]   conn - Previously initialized connection structure
 *
 */
void
pbs_db_free_conn_info(pbs_db_conn_t *conn)
{
	if (!conn || !conn->conn_info)
		return;

	memset(conn->conn_info, 0, strlen(conn->conn_info));
	free(conn->conn_info);
	conn->conn_info = NULL;
}

/**
 * @brief convert network to host byte order to unsigned long long
 *
 * @param[in]   x - Value to convert
 *
 * @return Value converted from network to host byte order. Return the original
 * value if network and host byte order are identical.
 */
unsigned long long
pbs_ntohll(unsigned long long x)
{
	if (ntohl(1) == 1)
		return x;

	/*
	 * htonl and ntohl always work on 32 bits, even on a 64 bit platform,
	 * so there is no clash.
	 */
	return (unsigned long long)(((unsigned long long) ntohl((x) & 0xffffffff)) << 32) | ntohl(((unsigned long long)(x)) >> 32);
}

/**
 * @brief
 *	Execute a prepared DML (insert or update) statement
 *
 * @param[in] 	  conn - The connnection handle
 * @param[in]	  stmt - Name of the statement (prepared previously)
 * @param[in]     num_vars - The number of parameters in the sql ($1, $2 etc)
 *
 * @return      Error code
 * @retval	-1 - Execution of prepared statement failed
 * @retval	 0 - Success and > 0 rows were affected
 * @retval	 1 - Execution succeeded but statement did not affect any rows
 *
 *
 */
int pg_db_cmd_ret(pbs_db_conn_t *conn, char *stmt, int num_vars)
{
	PGresult *res;
	char *rows_affected = NULL;
	ExecStatusType res_rc;
	char *sql_error;

	res = PQexecPrepared((PGconn*) conn->conn_db_handle, stmt, num_vars,
			((pg_conn_data_t *) conn->conn_data)->paramValues,
			((pg_conn_data_t *) conn->conn_data)->paramLengths,
			((pg_conn_data_t *) conn->conn_data)->paramFormats,
			conn->conn_result_format);

	res_rc = PQresultStatus(res);
	if (!(res_rc == PGRES_COMMAND_OK || res_rc == PGRES_TUPLES_OK)) {
		sql_error = (char *)PQresultErrorField(res, PG_DIAG_SQLSTATE);
		/* if sql_error returns value "23505" this means PBS is violating the unique key rule.
		 * To fix this, try to delete the existing record and insert a new one */
		if (UNIQUE_KEY_VIOLATION == atoi(sql_error)) {
			PQclear(res);
			return UNIQUE_KEY_VIOLATION;
		}
		pg_set_error(conn, "Execution of Prepared statement", stmt);
		PQclear(res);
		return -1;
	}
	
	if (PQntuples(res) <= 0) {
		PQclear(res);
		return 1;
	}
	rows_affected = PQcmdTuples(res);

	/*
	 *  we can't call PQclear(res) yet, since rows_affected
	 * (used below) is a pointer to a field inside res (PGresult)
	 */
	if (rows_affected == NULL || strtol(rows_affected, NULL, 10) <= 0) {
		PQclear(res);
		return 1;
	}
	conn->conn_resultset = res; /* store, since caller will retrieve results */

	return 0;
}
