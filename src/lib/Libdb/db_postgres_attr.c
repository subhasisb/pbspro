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
 * @file    db_postgres_attr.c
 *
 * @brief
 *	Implementation of the attribute related functions for postgres
 *
 */

#include <pbs_config.h>   /* the master config generated by configure */
#include "pbs_db.h"
#include "db_postgres.h"
#include "jsmn.h"

/*
 * initially allocate some space to buffer, anything more will be
 * allocated later as required. Just allocate 1000 chars, hoping that
 * most common sql's might fit within it without needing to resize
 */
#define INIT_BUF_SIZE 1000

static int
jsoneq(const char *json, jsmntok_t *tok, const char *s)
{
  if (tok->type == JSMN_STRING && (int)strlen(s) == tok->end - tok->start &&
      strncmp(json + tok->start, s, tok->end - tok->start) == 0) {
    return 0;
  }
  return -1;
}

/**
 * @brief
 *	Converts a postgres jsonb[] to attribute list.
 *
 * @param[in]	buf - String which is in the form of postgres jsonb[]
 * @param[out]  attr_list - List of pbs_db_attr_list_t objects
 *
 * @return      Error code
 * @retval	-1 - On Error
 * @retval	 0 - On Success
 * @retval	>1 - Number of attributes
 *
 */
int
convert_json_to_db_attr_list(char *buf, pbs_db_attr_list_t *attr_list)
{
#define DEF_TOKENS 100
#define MAX_TOKENS 1000
	int i;
	int k;
	int r;
	int tot_attrs=0;
	char *p;
	char *attr_value;
	int attr_flags;
	pbs_db_attr_info_t *attrs = NULL;
	jsmn_parser jp;
	static int num_tokens = DEF_TOKENS;
	static jsmntok_t *t = NULL; /* We expect no more than 128 JSON tokens */

	if (t == NULL) {
again:
		if ((t = malloc(sizeof(jsmntok_t) * num_tokens)) == NULL) {
				printf("No memory\n");
				goto err;
		}
	}

 	jsmn_init(&jp);
	r = jsmn_parse(&jp, buf, strlen(buf), t, num_tokens);
	if (r < 0) { 
			if (r == JSMN_ERROR_NOMEM) {
					num_tokens += DEF_TOKENS;
					if (num_tokens > MAX_TOKENS) {
							printf("Too many tokens\n");
							goto err;
					}
					printf("Resizing to %d\n", num_tokens);
					goto again;
			} else {
				printf("Failed to parse JSON: %d\n", r);
				goto err;
			}
	}
	
	attrs = malloc(sizeof(pbs_db_attr_info_t)*r);
	if (!attrs)
		goto err;

	attr_list->attributes = attrs;

	/* Assume the top-level element is an object */
	if (r < 1 || t[0].type != JSMN_OBJECT) {
		goto err;
	}

	for (i = 1; i < r; i++) {
		/* expect the first token to be name of an attribute, which can be anything */
		if (t[i].type != JSMN_STRING || t[i].size != 1) {
			printf("Bad json, expected string with single child at %s\n", buf + t[i].start);
			goto err;
		}
		
		snprintf(attrs[tot_attrs].attr_name, sizeof(attrs[tot_attrs].attr_name), "%.*s", t[i].end - t[i].start, buf + t[i].start);
		if ((p = strchr(attrs[tot_attrs].attr_name, '.'))) {
			*p = '\0';
			p++;
			strncpy(attrs[tot_attrs].attr_resc, p, sizeof(attrs[tot_attrs].attr_resc));
		}
		i++;

		/* There has to be a object value for this attribute name */
		if (t[i].type != JSMN_OBJECT || t[i].size != 2) {
			printf("Bad json, expected object with 2 children at %s\n", buf + t[i + k].start);
			goto err;
		}
		i++;

		for(k = 0; k < 2; k++ ) {
			/* Now read attr_value and attr_flags */
			if (jsoneq(buf, &t[i + k], "attr_value") == 0) {
					if (t[i + k + 1].type != JSMN_STRING) {
							printf("Bad json, expected string at %s\n", buf + t[i + k + 1].start);
							goto err;
					}
					attr_value = strndup(buf + t[i + k + 1].start, t[i + k + 1].end - t[i + k + 1].start);
					i = i + k + 1;
			} else if (jsoneq(buf, &t[i + k], "attr_flags") == 0) {
					if (t[i + k + 1].type != JSMN_PRIMITIVE) {
							printf("Bad json, expected primitive at %s\n", buf + t[i + k + 1].start);
							goto err;
					}
					attr_flags = strtol(buf + t[i + k + 1].start, NULL, 10);
					i = i + k + 1;
			} else {
					printf("Bad json, expected attr_value or attr_flags at %s\n", buf + t[i + k].start);
					goto err;

			}
		}

		attrs[tot_attrs].attr_value = attr_value;
		attrs[tot_attrs].attr_flags = attr_flags;

		tot_attrs++;
	}

	attr_list->attr_count = tot_attrs;

	return 0;

err:
	free(attrs);
	attr_list->attributes = NULL;
	attr_list->attr_count = 0;
	return -1;
}

/**
 * @brief
 *	Converts an attribute list to string array which is in the form of postgres jsonb[].
 *
 * @param[in]	attr_list - List of pbs_db_attr_list_t objects
 * @param[out]  outbuf - string which is in the form of postgres jsonb[]
 *
 * @return      Error code
 * @retval	-1 - On Error
 * @retval	 0 - On Success
 *
 */
int
convert_db_attr_list_to_json_inner(char **outbuf, pbs_db_attr_list_t *attr_list, int type)
{
	#define DEFAULT_LEN 1000

	int i;
	pbs_db_attr_info_t *attrs = attr_list->attributes;
	int tot_space;
	int space_left;
	int space_needed;
	int used;
	int p_offset;
	char *dot;
	char *resc;
	char *buf = NULL;
	char *p = buf;

	if (buf == NULL) {
		buf = malloc(DEFAULT_LEN + 3); /* open close {} and null char */
		if (!buf)
			return -1;
		tot_space = DEFAULT_LEN;
		space_left = tot_space;
	}

	p = buf;
	strcpy(p, "{");
	p++;

	for (i = 0; i < attr_list->attr_count; ++i) {
		space_needed = PBS_MAXATTRNAME + PBS_MAXATTRRESC + (type && attrs[i].attr_value?strlen(attrs[i].attr_value):0) + sizeof(int) + 25;
		if (space_needed > space_left) {
			if (space_needed > DEFAULT_LEN)
				tot_space += space_needed * 2;
			else
				tot_space += DEFAULT_LEN;
			
			p_offset = p - buf; /* store current work offset, since buff address would change */
			p = realloc(buf, tot_space);
			if (!p) {
				free(buf);
				return -1;
			}

			/* point p back */
			buf = p;
			p = buf + p_offset;
		}

		if (attrs[i].attr_resc[0] != '\0') {
			dot = ".";
			resc = attrs[i].attr_resc;
		} else {
			dot = "";
			resc = "";
		}

		if (type) {
			used = sprintf(p, "%s\"%s%s%s\": {\"attr_value\": \"%s\", \"attr_flags\": %d}", (i != 0)? ", ":"", attrs[i].attr_name, dot, resc, attrs[i].attr_value, attrs[i].attr_flags);
		} else {
			used = sprintf(p, "%s\"%s\"", (i != 0)? ", ":"", attrs[i].attr_name);
		}
		space_left -= used;
		p += used;
	}
	sprintf(p, "}"); /* apply final close brace */

	*outbuf = buf; 
	return 0;
}

int
convert_db_attr_list_to_json(char **outbuf, pbs_db_attr_list_t *attr_list)
{
	return convert_db_attr_list_to_json_inner(outbuf, attr_list, 1);
}

int
convert_db_attr_list_to_keys_array(char **outbuf, pbs_db_attr_list_t *attr_list)
{
	return convert_db_attr_list_to_json_inner(outbuf, attr_list, 0);
}

/**
 * @brief
 *	Frees attribute list memory
 *
 * @param[in]	attr_list - List of pbs_db_attr_list_t objects
 *
 * @return      None
 *
 */
void
free_db_attr_list(pbs_db_attr_list_t *attr_list)
{
	if (attr_list->attributes != NULL) {
		if (attr_list->attr_count > 0) {
			int i;
			for (i=0; i < attr_list->attr_count; i++) {
				free(attr_list->attributes[i].attr_value);
			}
		}
		free(attr_list->attributes);
		attr_list->attributes = NULL;
	}
}
