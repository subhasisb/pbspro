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

#ifndef	_PBS_SCHED_H
#define	_PBS_SCHED_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "pbs_config.h"
#include "pbs_ifl.h"
#include "libpbs.h"
#include "list_link.h"
#include "attribute.h"
#include "resource.h"
#include "pbs_ifl.h"
#include "server_limits.h"
#include "sched_cmds.h"
#include "work_task.h"
#include "net_connect.h"
#include "resv_node.h"
#include "queue.h"
#include "batch_request.h"
#include "job.h"
#include "reservation.h"

#define PBS_SCHED_CYCLE_LEN_DEFAULT 1200

/* Default value of preempt_queue_prio */
#define PBS_PREEMPT_QUEUE_PRIO_DEFAULT		150

#define SC_STATUS_LEN 	10

#define	SCHED_CYCLE_END 0
#define LISTEN_BACKLOG 100
#define TCP_TIMEOUT 50000

/*
 * Attributes for the server's sched object
 * Must be the same order as listed in sched_attr_def (master_sched_attr_def.xml)
 */
enum sched_atr {
	SCHED_ATR_SchedHost,
	SCHED_ATR_version,
	SCHED_ATR_sched_cycle_len,
	SCHED_ATR_dont_span_psets,
	SCHED_ATR_only_explicit_psets,
	SCHED_ATR_sched_preempt_enforce_resumption,
	SCHED_ATR_preempt_targets_enable,
	SCHED_ATR_job_sort_formula_threshold,
	SCHED_ATR_throughput_mode,
	SCHED_ATR_job_run_wait,
	SCHED_ATR_opt_backfill_fuzzy,
	SCHED_ATR_sched_port,
	SCHED_ATR_partition,
	SCHED_ATR_sched_priv,
	SCHED_ATR_sched_log,
	SCHED_ATR_scheduling,
	SCHED_ATR_schediteration,
	SCHED_ATR_sched_user,
	SCHED_ATR_sched_comment,
	SCHED_ATR_sched_state,
	SCHED_ATR_preempt_queue_prio,
	SCHED_ATR_preempt_prio,
	SCHED_ATR_preempt_order,
	SCHED_ATR_preempt_sort,
	SCHED_ATR_log_events,
	SCHED_ATR_job_sort_formula,
	SCHED_ATR_server_dyn_res_alarm,
	SCHED_ATR_attr_update_period,
#include "site_sched_attr_enum.h"
	/* This must be last */
	SCHED_ATR_LAST
};
extern attribute_def sched_attr_def[];

typedef struct pbs_sched {
	pbs_list_link	sc_link;		/* forward/backward links */
	/* 0 - primary socket, 1 - secondary socket */
	int scheduler_sock[2];
	int svr_do_schedule;
	int svr_do_sched_high;
	pbs_net_t pbs_scheduler_addr;
	unsigned int pbs_scheduler_port;
	time_t sch_next_schedule;		/* when to next run scheduler cycle */
	char sc_name[PBS_MAXSCHEDNAME + 1];
	struct preempt_ordering preempt_order[PREEMPT_ORDER_MAX + 1];
	int sched_cycle_started;
	/* sched object's attributes  */
	attribute sch_attr[SCHED_ATR_LAST];
	short newobj;
} pbs_sched;


extern pbs_sched *dflt_scheduler;
extern	pbs_list_head	svr_allscheds;
extern void set_scheduler_flag(int flag, pbs_sched *psched);
extern int find_assoc_sched_jid(char *jid, pbs_sched **target_sched);
extern int find_assoc_sched_pque(pbs_queue *pq, pbs_sched **target_sched);
extern pbs_sched *find_sched_from_sock(int sock);
extern pbs_sched *find_sched(char *sched_name);
extern int validate_job_formula(attribute *pattr, void *pobject, int actmode);
extern pbs_sched *find_sched_from_partition(char *partition);
extern int get_sched_cmd(int sock, int *val, char **identifier);
extern int recv_cycle_end(int sock);
extern void handle_deferred_cycle_close();

#ifdef	__cplusplus
}
#endif
#endif	/* _PBS_SCHED_H */
