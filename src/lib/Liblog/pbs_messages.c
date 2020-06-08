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

#include <pbs_config.h>   /* the master config generated by configure */

#include "portability.h"
#include "pbs_error.h"

#include <stdlib.h>

/**
 * @file	pbs_messages.c
 * @brief
 * Messages issued by the server.  They are kept here in one place
 * to make translation a bit easier.
 * @warning
 *  there are places where a message and other info is stuffed
 *	into a buffer, keep the messages short!
 *
 * This first set of messages are recorded by the server or mom to the log.
 */

char *msg_abt_err	= "Unable to abort Job %s which was in substate %d";
char *msg_badexit	= "Abnormal exit status 0x%x: ";
char *msg_badwait	= "Invalid time in work task for waiting, job = %s";
char *msg_deletejob	= "Job to be deleted";
char *msg_delrunjobsig  = "Job sent signal %s on delete";
char *msg_err_malloc	= "malloc failed";
char *msg_err_noqueue	= "Unable to requeue job,";
char *msg_err_noqueue1	= "queue is not defined";
char *msg_err_purgejob	= "Unlink of job file failed";
char *msg_err_purgejob_db	= "Removal of job from datastore failed";
char *msg_err_unlink	= "Unlink of %s file %s failed";
char *msg_illregister	= "Illegal op in register request received for job %s";
char *msg_init_abt	= "Job aborted on PBS Server initialization";
char *msg_init_baddb	= "Unable to read server database";
char *msg_init_badjob	= "Recover of job %s failed";
char *msg_init_chdir	= "unable to change to directory %s";
char *msg_init_expctq   = "Expected %d, recovered %d queues";
char *msg_init_exptjobs = "Recovered %d jobs";
char *msg_init_nojobs   = "No jobs to open";
char *msg_init_noqueues = "No queues to open";
char *msg_init_noresvs  = "No resvs to open";
char *msg_init_norerun	= "Unable to rerun job at Server initialization";
char *msg_init_queued	= "Requeued in queue: ";
char *msg_init_recovque	= "Recovered queue %s";
char *msg_init_recovresv = "Recovered reservation %s";
char *msg_init_resvNOq  = "Queue %s for reservation %s missing";
char *msg_init_substate = "Requeueing job, substate: %d ";
char *msg_init_unkstate = "Unable to recover job in strange substate: %d";
char *msg_isodedecode	= "Decode of request failed";
char *msg_issuebad	= "attempt to issue invalid request of type %d";
char *msg_job_abort	= "Aborted by PBS Server ";
char *msg_jobholdset	= "Holds %s set at request of %s@%s";
char *msg_jobholdrel	= "Holds %s released at request of %s@%s";
char *msg_job_end	= "Execution terminated";
char *msg_job_end_stat	= "Exit_status=%d";
char *msg_job_end_sig	= "Terminated on signal %d";
char *msg_jobmod	= "Job Modified";
char *msg_jobnew	= "Job Queued at request of %s@%s, owner = %s, job name = %s, queue = %s";
char *msg_jobrerun	= "Job Rerun";
char *msg_jobrun	= "Job Run";
char *msg_job_start	= "Begun execution";
char *msg_job_stageinfail = "File stage in failed, see below.\nJob will be retried later, please investigate and correct problem.";
char *msg_leftrunning	= "job running on at Server shutdown";
char *msg_manager	= "%s at request of %s@%s";
char *msg_man_cre	= "created";
char *msg_man_del	= "deleted";
char *msg_man_set	= "attributes set: ";
char *msg_man_uns	= "attributes unset: ";
char *msg_messagejob	= "Message request to job status %d";
char *msg_mombadhold	= "MOM rejected hold request: %d";
char *msg_mombadmodify	= "MOM rejected modify request, error: %d";
char *msg_momsetlim	= "Job start failed.  Can't set \"%s\" limit: %s.\n";
char *msg_momnoexec1	= "Job cannot be executed\nSee Administrator for help";
char *msg_momnoexec2	= "Job cannot be executed\nSee job standard error file";
char *msg_movejob	= "Job moved to ";
char *msg_norelytomom	= "Server could not connect to MOM";
char *msg_obitnojob 	= "Job Obit notice received has error %d";
char *msg_obitnocpy	= "Post job file processing error; job %s on host %s";
char *msg_obitnodel	= "Unable to delete files for job %s, on host %s";
char *msg_on_shutdown	= " on Server shutdown";
char *msg_orighost 	= "Job missing PBS_O_HOST value";
char *msg_permlog	= "Unauthorized Request, request type: %d, Object: %s, Name: %s, request from: %s@%s";
char *msg_postmomnojob	= "Job not found after hold reply from MOM";
char *msg_request	= "Type %d request received from %s@%s, sock=%d";
char *msg_regrej	= "Dependency request for job rejected by ";
char *msg_registerdel	= "Job deleted as result of dependency on job %s";
char *msg_registerrel	= "Dependency on job %s released.";
char *msg_routexceed	= "Route queue lifetime exceeded";
char *msg_script_open	= "Unable to open script file";
char *msg_script_write	= "Unable to write script file";
char *msg_hookfile_open	= "Unable to open hook-related file";
char *msg_hookfile_write = "Unable to write hook-related file";
char *msg_shutdown_op	= "Shutdown request from %s@%s ";
char *msg_shutdown_start = "Starting to shutdown the server, type is ";
char *msg_startup1	= "Version %s, started, initialization type = %d";
char *msg_startup2	= "Server pid = %d ready;  using ports Server:%d Scheduler:%d MOM:%d RM:%d";
char *msg_startup3	= "%s %s: %s mode and %s, \ndo you wish to continue y/(n)?";
char *msg_svdbopen	= "Unable to open server data base";
char *msg_svdbnosv	= "Unable to save server data base ";
char *msg_svrdown	= "Server shutdown completed";
char *msg_resvQcreateFail = "failed to create queue for reservation";
char *msg_genBatchReq = "batch request generation failed";
char *msg_mgrBatchReq = "mgr batch request failed";
char *msg_deleteresv = "delete reservation";
char *msg_deleteresvJ = "delete reservation-job";
char *msg_noDeljobfromResv = "failed to delete job %s from reservation %s";
char *msg_purgeResvlink	= "Unlink of reservation file failed";
char *msg_purgeResvDb	= "Removal of reservation failed";
char *msg_purgeResvFail	= "Failed to purge reservation";
char *msg_internalReqFail = "An internally generated request failed";
char *msg_qEnabStartFail = "Failed to start/enable reservation queue";
char *msg_NotResv = "not a reservation";
char *msg_resv_abort = "Reservation removed";
char *msg_resv_start = "Reservation period starting";
char *msg_resv_end = "Reservation period ended";
char *msg_resv_confirm = "Reservation transitioned from state UNCONFIRMED to CONFIRMED";
char *msg_signal_job   = "job signaled with %s by %s@%s";
char *msg_license_min_badval = "pbs_license_min is < 0, or > pbs_license_max";
char *msg_license_max_badval = "pbs_license_max is < 0, or < pbs_license_min";
char *msg_license_linger_badval = "pbs_license_linger_time is <= 0";
char *msg_license_server_down = "Not running any new jobs. PBS license server is down.";
char *msg_license_bad_action = "Action not allowed with license server scheme.";
char *msg_prov_script_notfound = "Provision hook script not found";
char *msg_jobscript_max_size= "jobscript size exceeded the jobscript_max_size";
char *msg_badjobscript_max_size= "jobscript max size exceeds 2GB";
char *msg_new_inventory_mom = "Setting inventory_mom for vnode_pool %d to %s";
char *msg_auth_request = "Type %d request is authenticated. The credential id is %s@%s, host %s, sock=%d";

/*
 * This next set of messages are returned to the client on an error.
 * They may also be logged.
 */


char *msg_unkjobid	= "Unknown Job Id";
char *msg_noattr	= "Undefined attribute ";
char *msg_attrro	= "Cannot set attribute, read only or insufficient permission ";
char *msg_ivalreq	= "Invalid request";
char *msg_unkreq	= "Unknown request";
char *msg_perm		= "Unauthorized Request ";
char *msg_reqbadhost	= "Access from host not allowed, or unknown host";
char *msg_jobexist	= "Job with requested ID already exists";
char *msg_system	= "System error: ";
char *msg_internal	= "PBS server internal error";
char *msg_regroute	= "Dependent parent job currently in routing queue";
char *msg_unksig	= "Unknown/illegal signal name";
char *msg_badatval	= "Illegal attribute or resource value";
char *msg_badnodeatval	= "Illegal value for node";
char *msg_nodenamebig	= "Node name is too big";
char *msg_jobnamebig	= "name is too long";
char *msg_mutualex	= "Mutually exclusive values for ";
char *msg_modatrrun	= "Cannot modify attribute while job running ";
char *msg_badstate	= "Request invalid for state of job";
char *msg_unkque	= "Unknown queue";
char *msg_unknode	= "Unknown node ";
char *msg_unknodeatr	= "Unknown node-attribute ";
char *msg_nonodes	= "Server has no node list";
char *msg_badcred	= "Invalid credential";
char *msg_expired	= "Expired credential";
char *msg_qunoenb	= "Queue is not enabled";
char *msg_qacess	= "Access to queue is denied";
char *msg_nodestale	= "Cannot change state of stale node";

#ifdef WIN32
char *msg_baduser	= "Bad UID for job execution - could be an administrator-type account currently not allowed to run jobs (can be configured)";
#else
char *msg_baduser	= "Bad UID for job execution";
#endif

char *msg_bad_password  = "job has bad password";
char *msg_badgrp	= "Bad GID for job execution";
char *msg_badRuser	= "Bad effective UID for reservation";
char *msg_badRgrp	= "Bad effective GID for reservation";
char *msg_hopcount	= "Job routing over too many hops";
char *msg_queexist	= "Queue already exists";
char *msg_attrtype	= "Warning: type of queue %s incompatible with attribute %s";
char *msg_attrtype2	= "Incompatible type";
char *msg_objbusy	= "Cannot delete busy object";
char *msg_quenbig	= "Queue name too long";
char *msg_nosupport	= "No support for requested service";
char *msg_quenoen	= "Cannot enable queue, incomplete definition";
char *msg_needquetype   = "Queue type must be set";
char *msg_protocol	= "Batch protocol error";
char *msg_noconnects	= "No free connections";
char *msg_noserver	= "No server specified";
char *msg_unkresc	= "Unknown resource";
char *msg_excqresc	= "Job violates queue and/or server resource limits";
char *msg_quenodflt	= "No default queue specified";
char *msg_jobnorerun 	= "job is not rerunnable";
char *msg_routebad	= "Job rejected by all possible destinations";
char *msg_momreject	= "Execution server rejected request";
char *msg_nosyncmstr	= "No master found for sync job set";
char *msg_sched_called 	= "Scheduler sent command %d";
char *msg_sched_nocall  = "Could not contact Scheduler";
char *msg_stageinfail	= "Stage In of files failed";
char *msg_rescunav	= "Resource temporarily unavailable";
char *msg_maxqueued	= "Maximum number of jobs already in queue";
char *msg_chkpointbusy	= "Checkpoint busy, may retry";
char *msg_exceedlmt	= "Resource limit exceeds allowable";
char *msg_badacct	= "Invalid Account";
char *msg_baddepend	= "Invalid Job Dependency";
char *msg_duplist	= "Duplicate entry in list ";
char *msg_svrshut	= "Request not allowed: Server shutting down";
char *msg_execthere	= "Cannot execute at specified host because of checkpoint or stagein files";
char *msg_gmoderr	= "Modification failed for ";
char *msg_notsnode	= "No time-share node available";
char *msg_resvNowall	= "Reservation needs walltime";
char *msg_jobNotresv	= "not a reservation job";
char *msg_resvToolate	= "too late for reservation";
char *msg_resvsyserr	= "internal reservation-system error";

char *msg_Resv_Cancel   = "Attempting to cancel reservation";
char *msg_unkresvID	= "Unknown Reservation Id";
char *msg_resvExist	= "Reservation with requested ID already exists";
char *msg_resvfromresvjob	= "Reservation may not be created from a job already within a reservation";
char *msg_resvfromarrjob	= "Reservation may not be created from an array job";
char *msg_resvFail	= "reservation failure";
char *msg_delProgress	= "Delete already in progress";
char *msg_BadTspec	= "Bad time specification(s)";
char *msg_BadNodespec	= "node(s) specification error";
char *msg_licensecpu	= "Exceeded number of licensed cpus";
char *msg_licenseinv	= "PBS license is invalid";
char *msg_resvauth_H	= "Host machine not authorized to submit reservations";
char *msg_resvauth_G	= "Requestor's group not authorized to submit reservations";
char *msg_resvauth_U	= "Requestor not authorized to make reservations";
char *msg_licenseunav 	= "Floating License unavailable";
char *msg_rescnotstr    = "Resource is not of type string or array_of_strings";
char *msg_maxarraysize = "Array job exceeds server or queue size limit";
char *msg_invalselectresc = "Resource invalid in \"select\" specification";
char *msg_invaljobresc = "\"-lresource=\" cannot be used with \"select\" or \"place\", resource is";
char *msg_invalnodeplace = "Cannot be used with select or place";
char *msg_placenoselect  = "Cannot have \"place\" without \"select\"";
char *msg_indirecthop    = "invalid multi-level indirect reference for resource";
char *msg_indirectbadtgt = "indirect target undefined on node for resource";
char *msg_dupresc        = "duplicated resource within a section of a select specification, resource is";
char *msg_connfull = "Server connection table full";
char *msg_bad_formula = "Invalid Formula Format";
char *msg_bad_formula_kw = "Formula contains invalid keyword";
char *msg_bad_formula_type =  "Formula contains a resource of an invalid type";
char *msg_hook_error =  "hook error";
char *msg_eligibletimeset_error =  "Cannot set attribute when eligible_time_enable is OFF";
char *msg_historyjobid		=  "Job has finished";
char *msg_job_history_notset	=  "PBS is not configured to maintain job history";
char *msg_job_history_delete    =  "Deleting job history upon request from %s@%s";
char *msg_also_deleted_job_history = "Also deleted job history";
char *msg_nohistarrayjob        = "Request invalid for finished array subjob";
char *msg_valueoutofrange       =  "attribute value is out of range";
char *msg_jobinresv_conflict 	=  "job and reservation have conflicting specification";
char *msg_max_no_minwt 		=  "Cannot have \"max_walltime\" without \"min_walltime\"";
char *msg_min_gt_maxwt 		=  "\"min_walltime\" can not be greater than \"max_walltime\"";
char *msg_nostf_resv 		=  "\"min_walltime\" and \"max_walltime\" are not valid resources for a reservation";
char *msg_nostf_jobarray 	=  "\"min_walltime\" and \"max_walltime\" are not valid resources for a job array";
char *msg_nolimit_resc 	=  "Resource limits can not be set for the resource";
char *msg_save_err		= "Failed to save job/resv, refer server logs for details";
char *msg_mom_incomplete_hook = "vnode's parent mom has a pending copy hook or delete hook request";
char *msg_mom_reject_root_scripts = "mom not accepting remote hook files or root job scripts";
char *msg_hook_reject = "hook rejected request";
char *msg_hook_reject_rerunjob = "hook rejected request, requiring job to be rerun";
char *msg_hook_reject_deletejob = "hook rejected request, requiring job to be deleted";
char *msg_ival_obj_name = "Invalid object name";
char *msg_wrong_resume =	"Job can not be resumed with the requested resume signal";

/* Provisioning specific */
char *msg_provheadnode_error    = "Cannot set provisioning attribute on host running PBS server and scheduler";
char *msg_cantmodify_ndprov     = "Cannot modify attribute while vnode is provisioning";
char *msg_nostatechange_ndprov  = "Cannot change state of provisioning vnode";
char *msg_cantdel_ndprov        = "Cannot delete vnode if vnode is provisioning";
char *msg_node_bad_current_aoe  = "Current AOE does not match with resources_available.aoe";
char *msg_invld_aoechunk        = "Invalid provisioning request in chunk(s)";

/* Standing reservation specific */
char *msg_bad_rrule_yearly	= "YEARLY recurrence duration cannot exceed 1 year";
char *msg_bad_rrule_monthly	= "MONTHLY recurrence duration cannot exceed 1 month";
char *msg_bad_rrule_weekly	= "WEEKLY recurrence duration cannot exceed 1 week";
char *msg_bad_rrule_daily	= "DAILY recurrence duration cannot exceed 24 hours";
char *msg_bad_rrule_hourly	= "HOURLY recurrence duration cannot exceed 1 hour";
char *msg_bad_rrule_minutely	= "MINUTELY recurrence duration cannot exceed 1 minute";
char *msg_bad_rrule_secondly	= "SECONDLY recurrence duration cannot exceed 1 second";
char *msg_bad_rrule_syntax	= "Undefined iCalendar syntax";
char *msg_bad_rrule_syntax2	= "Undefined iCalendar syntax. A valid COUNT or UNTIL is required";
char *msg_bad_ical_tz		= "Unrecognized PBS_TZID environment variable";

/* following set of messages are for entity limit controls */
char *msg_mixedquerunlimits	= "Cannot mix old and new style queue/run limit enforcement types";
char *msg_et_qct  = "Maximum number of jobs already in queue %s";
char *msg_et_sct  = "Maximum number of jobs already in complex";
char *msg_et_ggq  = "would exceed queue %s's per-group limit";
char *msg_et_ggs  = "would exceed complex's per-group limit";
char *msg_et_gpq  = "would exceed queue %s's per-project limit";
char *msg_et_gps  = "would exceed complex's per-project limit";

char *msg_et_guq  = "would exceed queue %s's per-user limit";
char *msg_et_gus  = "would exceed complex's per-user limit";
char *msg_et_sgq  = "Maximum number of jobs for group %s already in queue %s";
char *msg_et_sgs  = "Maximum number of jobs for group %s already in complex";
char *msg_et_spq  = "Maximum number of jobs for project %s already in queue %s";
char *msg_et_sps  = "Maximum number of jobs for project %s already in complex";
char *msg_et_suq  = "Maximum number of jobs for user %s already in queue %s";
char *msg_et_sus  = "Maximum number of jobs for user %s already in complex";
char *msg_et_raq  = "would exceed limit on resource %s in queue %s";
char *msg_et_ras  = "would exceed limit on resource %s in complex";
char *msg_et_rggq = "would exceed per-group limit on resource %s in queue %s";
char *msg_et_rggs = "would exceed per-group limit on resource %s in complex";
char *msg_et_rgpq = "would exceed per-project limit on resource %s in queue %s";
char *msg_et_rgps = "would exceed per-project limit on resource %s in complex";
char *msg_et_rguq = "would exceed per-user limit on resource %s in queue %s";
char *msg_et_rgus = "would exceed per-user limit on resource %s in complex";
char *msg_et_rsgq = "would exceed group %s's limit on resource %s in queue %s";
char *msg_et_rsgs = "would exceed group %s's limit on resource %s in complex";
char *msg_et_rspq = "would exceed project %s's limit on resource %s in queue %s";
char *msg_et_rsps = "would exceed project %s's limit on resource %s in complex";
char *msg_et_rsuq = "would exceed user %s's limit on resource %s in queue %s";
char *msg_et_rsus = "would exceed user %s's limit on resource %s in complex";

char *msg_et_qct_q  = "Maximum number of jobs in 'Q' state already in queue %s";
char *msg_et_sct_q  = "Maximum number of jobs in 'Q' state already in complex";
char *msg_et_ggq_q  = "would exceed queue %s's per-group limit of jobs in 'Q' state";
char *msg_et_ggs_q  = "would exceed complex's per-group limit of jobs in 'Q' state";
char *msg_et_gpq_q  = "would exceed queue %s's per-project limit of jobs in 'Q' state";
char *msg_et_gps_q  = "would exceed complex's per-project limit of jobs in 'Q' state";

char *msg_et_guq_q  = "would exceed queue %s's per-user limit of jobs in 'Q' state";
char *msg_et_gus_q  = "would exceed complex's per-user limit of jobs in 'Q' state";
char *msg_et_sgq_q  = "Maximum number of jobs in 'Q' state for group %s already in queue %s";
char *msg_et_sgs_q  = "Maximum number of jobs in 'Q' state for group %s already in complex";
char *msg_et_spq_q  = "Maximum number of jobs in 'Q' state for project %s already in queue %s";
char *msg_et_sps_q  = "Maximum number of jobs in 'Q' state for project %s already in complex";
char *msg_et_suq_q  = "Maximum number of jobs in 'Q' state for user %s already in queue %s";
char *msg_et_sus_q  = "Maximum number of jobs in 'Q' state for user %s already in complex";
char *msg_et_raq_q  = "would exceed limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_ras_q  = "would exceed limit on resource %s in complex for jobs in 'Q' state";
char *msg_et_rggq_q = "would exceed per-group limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_rggs_q = "would exceed per-group limit on resource %s in complex for jobs in 'Q' state";
char *msg_et_rgpq_q = "would exceed per-project limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_rgps_q = "would exceed per-project limit on resource %s in complex for jobs in 'Q' state";
char *msg_et_rguq_q = "would exceed per-user limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_rgus_q = "would exceed per-user limit on resource %s in complex for jobs in 'Q' state";
char *msg_et_rsgq_q = "would exceed group %s's limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_rsgs_q = "would exceed group %s's limit on resource %s in complex for jobs in 'Q' state";
char *msg_et_rspq_q = "would exceed project %s's limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_rsps_q = "would exceed project %s's limit on resource %s in complex for jobs in 'Q' state";
char *msg_et_rsuq_q = "would exceed user %s's limit on resource %s in queue %s for jobs in 'Q' state";
char *msg_et_rsus_q = "would exceed user %s's limit on resource %s in complex for jobs in 'Q' state";

char *msg_force_qsub_update =  "force a qsub update";
char *msg_noloopbackif = "Local host does not have loopback interface configured or pingable.";

char *msg_defproject = "%s = %s is also the default project assigned to jobs with unset project attribute";
char *msg_norunalteredjob = "Cannot run job which was altered/moved during current scheduling cycle.";

/* resource limit setup specific */
char *msg_corelimit = "invalid value for PBS_CORE_LIMIT in pbs.conf, continuing with default core limit. To use PBS_CORE_LIMIT update pbs.conf with correct value";

char *msg_resc_busy = "Resource busy";

char *msg_job_moved = "Job moved to remote server";
char *msg_init_recovsched = "Recovered scheduler %s";
char *msg_sched_exist = "Scheduler already exists";
char *msg_sched_name_big = "Scheduler name is too long";
char *msg_unknown_sched = "Unknown Scheduler";
char *msg_no_del_sched = "Can not delete Scheduler";
char *msg_sched_priv_exists = "Another scheduler also has same value for its sched_priv directory";
char *msg_sched_logs_exists = "Another scheduler also has same value for its sched_log directory";
char *msg_route_que_no_partition = "Cannot assign a partition to route queue";
char *msg_cannot_set_route_que = "Route queues are incompatible with the partition attribute";
char *msg_queue_not_in_partition = "Queue %s is not part of partition for node";
char *msg_partition_not_in_queue = "Partition %s is not part of queue for node";
char *msg_invalid_partion_in_queue = "Invalid partition in queue";
char *msg_sched_op_not_permitted = "Operation is not permitted on default scheduler";
char *msg_sched_part_already_used = "Partition is already associated with other scheduler";
char *msg_invalid_max_job_sequence_id = "Cannot set max_job_sequence_id < 9999999, or > 999999999999";
char *msg_jsf_incompatible = "Server's job_sort_formula value is incompatible with sched's";

char *msg_resv_not_empty = "Reservation not empty";
char *msg_stdg_resv_occr_conflict = "Requested time(s) will interfere with a later occurrence";
char *msg_alps_switch_err = "Switching ALPS reservation failed";

char *msg_softwt_stf = "soft_walltime is not supported with Shrink to Fit jobs";
char *msg_node_busy = "Node is busy";
char *msg_default_partition = "Default partition name is not allowed";
char *msg_depend_runone = "Job deleted, a dependent job ran";
char *msg_histdepend = "Finished job did not satisfy dependency";
char *msg_noconnection = "cannot connect to server";
/*
 * The following table connects error numbers with text
 * to be returned to the client.  Each is guaranteed to be pure text.
 * There are no printf formatting strings imbedded.
 */

struct pbs_err_to_txt pbs_err_to_txt[] = {
	{ PBSE_UNKJOBID, &msg_unkjobid },
	{ PBSE_NOATTR, &msg_noattr },
	{ PBSE_ATTRRO, &msg_attrro },
	{ PBSE_IVALREQ, &msg_ivalreq },
	{ PBSE_UNKREQ, &msg_unkreq },
	{ PBSE_PERM, &msg_perm },
	{ PBSE_BADHOST, &msg_reqbadhost },
	{ PBSE_JOBEXIST, &msg_jobexist },
	{ PBSE_SYSTEM, &msg_system },
	{ PBSE_INTERNAL, &msg_internal },
	{ PBSE_REGROUTE, &msg_regroute },
	{ PBSE_UNKSIG, &msg_unksig },
	{ PBSE_BADATVAL, &msg_badatval },
	{ PBSE_BADNDATVAL, &msg_badnodeatval },
	{ PBSE_NODENBIG, &msg_nodenamebig },
	{ PBSE_MUTUALEX, &msg_mutualex },
	{ PBSE_MODATRRUN, &msg_modatrrun },
	{ PBSE_BADSTATE, &msg_badstate },
	{ PBSE_UNKQUE, &msg_unkque },
	{ PBSE_UNKNODE, &msg_unknode },
	{ PBSE_UNKNODEATR, &msg_unknodeatr },
	{ PBSE_NONODES, &msg_nonodes },
	{ PBSE_BADCRED, &msg_badcred },
	{ PBSE_EXPIRED, &msg_expired },
	{ PBSE_QUNOENB, &msg_qunoenb },
	{ PBSE_QACESS, &msg_qacess },
	{ PBSE_BADUSER, &msg_baduser },
	{ PBSE_R_UID, &msg_badRuser },
	{ PBSE_HOPCOUNT, &msg_hopcount },
	{ PBSE_QUEEXIST, &msg_queexist },
	{ PBSE_OBJBUSY, &msg_objbusy },
	{ PBSE_QUENBIG, &msg_quenbig },
	{ PBSE_NOSUP, &msg_nosupport },
	{ PBSE_QUENOEN, &msg_quenoen },
	{ PBSE_PROTOCOL, &msg_protocol },
	{ PBSE_NOCONNECTS, &msg_noconnects },
	{ PBSE_NOSERVER, &msg_noserver },
	{ PBSE_UNKRESC, &msg_unkresc },
	{ PBSE_EXCQRESC, &msg_excqresc },
	{ PBSE_QUENODFLT, &msg_quenodflt },
	{ PBSE_NORERUN, &msg_jobnorerun },
	{ PBSE_ROUTEREJ, &msg_routebad },
	{ PBSE_MOMREJECT, &msg_momreject },
	{ PBSE_NOSYNCMSTR, &msg_nosyncmstr },
	{ PBSE_STAGEIN, &msg_stageinfail },
	{ PBSE_RESCUNAV, &msg_rescunav },
	{ PBSE_BADGRP,  &msg_badgrp },
	{ PBSE_R_GID,  &msg_badRgrp },
	{ PBSE_MAXQUED, &msg_maxqueued },
	{ PBSE_CKPBSY, &msg_chkpointbusy },
	{ PBSE_EXLIMIT, &msg_exceedlmt },
	{ PBSE_BADACCT, &msg_badacct },
	{ PBSE_BADDEPEND, &msg_baddepend },
	{ PBSE_DUPLIST, &msg_duplist },
	{ PBSE_EXECTHERE, &msg_execthere },
	{ PBSE_SVRDOWN, &msg_svrshut },
	{ PBSE_ATTRTYPE, &msg_attrtype2 },
	{ PBSE_GMODERR, &msg_gmoderr },
	{ PBSE_NORELYMOM, &msg_norelytomom },
	{ PBSE_NOTSNODE, &msg_notsnode },
	{ PBSE_RESV_NO_WALLTIME, &msg_resvNowall },
	{ PBSE_JOBNOTRESV, &msg_jobNotresv },
	{ PBSE_TOOLATE, &msg_resvToolate },
	{ PBSE_IRESVE, &msg_resvsyserr },
	{ PBSE_RESVEXIST, &msg_resvExist },
	{ PBSE_RESV_FROM_RESVJOB, &msg_resvfromresvjob },
	{ PBSE_RESV_FROM_ARRJOB, &msg_resvfromarrjob },
	{ PBSE_resvFail, &msg_resvFail },
	{ PBSE_genBatchReq, &msg_genBatchReq },
	{ PBSE_mgrBatchReq, &msg_mgrBatchReq },
	{ PBSE_UNKRESVID, &msg_unkresvID },
	{ PBSE_delProgress, &msg_delProgress },
	{ PBSE_BADTSPEC, &msg_BadTspec},
	{ PBSE_NOTRESV, &msg_NotResv},
	{ PBSE_BADNODESPEC, &msg_BadNodespec},
	{ PBSE_LICENSECPU, &msg_licensecpu },
	{ PBSE_LICENSEINV, &msg_licenseinv },
	{ PBSE_RESVAUTH_H, &msg_resvauth_H },
	{ PBSE_RESVAUTH_G, &msg_resvauth_G },
	{ PBSE_RESVAUTH_U, &msg_resvauth_U },
	{ PBSE_LICENSEUNAV, &msg_licenseunav },
	{ PBSE_RESCNOTSTR, &msg_rescnotstr },
	{ PBSE_MaxArraySize, &msg_maxarraysize },
	{ PBSE_NOSCHEDULER, &msg_sched_nocall },
	{ PBSE_INVALSELECTRESC, &msg_invalselectresc },
	{ PBSE_INVALJOBRESC, &msg_invaljobresc },
	{ PBSE_INVALNODEPLACE, &msg_invalnodeplace },
	{ PBSE_PLACENOSELECT, &msg_placenoselect },
	{ PBSE_INDIRECTHOP, &msg_indirecthop },
	{ PBSE_INDIRECTBT, &msg_indirectbadtgt },
	{ PBSE_NODESTALE,  &msg_nodestale  },
	{ PBSE_DUPRESC, &msg_dupresc },
	{ PBSE_CONNFULL, &msg_connfull },
	{ PBSE_LICENSE_MIN_BADVAL, &msg_license_min_badval },
	{ PBSE_LICENSE_MAX_BADVAL, &msg_license_max_badval },
	{ PBSE_LICENSE_LINGER_BADVAL, &msg_license_linger_badval },
	{ PBSE_LICENSE_SERVER_DOWN, &msg_license_server_down },
	{ PBSE_LICENSE_BAD_ACTION, &msg_license_bad_action},
	{ PBSE_BAD_FORMULA, &msg_bad_formula},
	{ PBSE_BAD_FORMULA_KW, &msg_bad_formula_kw},
	{ PBSE_BAD_FORMULA_TYPE, &msg_bad_formula_type},
	{ PBSE_BAD_RRULE_YEARLY, &msg_bad_rrule_yearly},
	{ PBSE_BAD_RRULE_MONTHLY, &msg_bad_rrule_monthly},
	{ PBSE_BAD_RRULE_WEEKLY, &msg_bad_rrule_weekly},
	{ PBSE_BAD_RRULE_DAILY, &msg_bad_rrule_daily},
	{ PBSE_BAD_RRULE_HOURLY, &msg_bad_rrule_hourly},
	{ PBSE_BAD_RRULE_MINUTELY, &msg_bad_rrule_minutely},
	{ PBSE_BAD_RRULE_SECONDLY, &msg_bad_rrule_secondly},
	{ PBSE_BAD_RRULE_SYNTAX, &msg_bad_rrule_syntax},
	{ PBSE_BAD_RRULE_SYNTAX2, &msg_bad_rrule_syntax2},
	{ PBSE_BAD_ICAL_TZ, &msg_bad_ical_tz},
	{ PBSE_HOOKERROR, &msg_hook_error},
	{ PBSE_NEEDQUET, &msg_needquetype},
	{ PBSE_ETEERROR, &msg_eligibletimeset_error},
	{ PBSE_HISTJOBID, &msg_historyjobid},
	{ PBSE_JOBHISTNOTSET, &msg_job_history_notset},
	{ PBSE_MIXENTLIMS, &msg_mixedquerunlimits},
	{ PBSE_ATVALERANGE, &msg_valueoutofrange},
	{ PBSE_PROV_HEADERROR, &msg_provheadnode_error},
	{ PBSE_NODEPROV_NOACTION, &msg_cantmodify_ndprov},
	{ PBSE_NODEPROV, &msg_nostatechange_ndprov},
	{ PBSE_NODEPROV_NODEL, &msg_cantdel_ndprov},
	{ PBSE_NODE_BAD_CURRENT_AOE, &msg_node_bad_current_aoe},
	{ PBSE_NOLOOPBACKIF, &msg_noloopbackif },
	{ PBSE_IVAL_AOECHUNK, &msg_invld_aoechunk},
	{PBSE_JOBINRESV_CONFLICT, &msg_jobinresv_conflict},
	{ PBSE_MAX_NO_MINWT, &msg_max_no_minwt},
	{ PBSE_MIN_GT_MAXWT, &msg_min_gt_maxwt},
	{ PBSE_NOSTF_RESV, &msg_nostf_resv},
	{ PBSE_NOSTF_JOBARRAY, &msg_nostf_jobarray},
	{ PBSE_NOLIMIT_RESOURCE, &msg_nolimit_resc},
	{PBSE_NORUNALTEREDJOB, &msg_norunalteredjob},
	{PBSE_NOHISTARRAYSUBJOB, &msg_nohistarrayjob},
	{PBSE_FORCE_QSUB_UPDATE, &msg_force_qsub_update},
	{PBSE_SAVE_ERR, &msg_save_err},
	{PBSE_MOM_INCOMPLETE_HOOK, &msg_mom_incomplete_hook},
	{PBSE_MOM_REJECT_ROOT_SCRIPTS, &msg_mom_reject_root_scripts},
	{PBSE_HOOK_REJECT, &msg_hook_reject},
	{PBSE_HOOK_REJECT_RERUNJOB, &msg_hook_reject_rerunjob},
	{PBSE_HOOK_REJECT_DELETEJOB, &msg_hook_reject_deletejob},
	{PBSE_IVAL_OBJ_NAME, &msg_ival_obj_name},
	{ PBSE_JOBNBIG, &msg_jobnamebig},
	{PBSE_RESCBUSY, &msg_resc_busy},
	{PBSE_JOB_MOVED, &msg_job_moved},
	{PBSE_JOBSCRIPTMAXSIZE, &msg_jobscript_max_size},
	{PBSE_BADJOBSCRIPTMAXSIZE,&msg_badjobscript_max_size},
	{PBSE_WRONG_RESUME, &msg_wrong_resume},
	{PBSE_SCHEDEXIST, &msg_sched_exist},
	{PBSE_SCHED_NAME_BIG, &msg_sched_name_big},
	{PBSE_UNKSCHED, &msg_unknown_sched},
	{PBSE_SCHED_NO_DEL, &msg_no_del_sched},
	{PBSE_SCHED_PRIV_EXIST, &msg_sched_priv_exists},
	{PBSE_SCHED_LOG_EXIST, &msg_sched_logs_exists},
	{PBSE_ROUTE_QUE_NO_PARTITION, &msg_route_que_no_partition},
	{PBSE_CANNOT_SET_ROUTE_QUE, &msg_cannot_set_route_que},
	{PBSE_QUE_NOT_IN_PARTITION, &msg_queue_not_in_partition},
	{PBSE_PARTITION_NOT_IN_QUE, &msg_partition_not_in_queue},
	{PBSE_INVALID_PARTITION_QUE, &msg_invalid_partion_in_queue},
	{PBSE_RESV_NOT_EMPTY, &msg_resv_not_empty},
	{PBSE_STDG_RESV_OCCR_CONFLICT, &msg_stdg_resv_occr_conflict},
	{PBSE_ALPS_SWITCH_ERR, &msg_alps_switch_err},
	{PBSE_SOFTWT_STF, &msg_softwt_stf},
	{PBSE_SCHED_OP_NOT_PERMITTED, &msg_sched_op_not_permitted},
	{PBSE_SCHED_PARTITION_ALREADY_EXISTS, &msg_sched_part_already_used},
	{PBSE_INVALID_MAX_JOB_SEQUENCE_ID, &msg_invalid_max_job_sequence_id},
	{PBSE_SVR_SCHED_JSF_INCOMPAT, &msg_jsf_incompatible},
	{PBSE_NODE_BUSY, &msg_node_busy},
	{PBSE_DEFAULT_PARTITION, &msg_default_partition},
	{PBSE_HISTDEPEND, &msg_histdepend},
	{PBSE_NOCONNECTION, &msg_noconnection},
	{ 0, NULL }		/* MUST be the last entry */
};


/**
 * @brief
 * 	pbse_to_txt() - return a text message for an PBS error number
 *	if it exists
 *
 * @param[in] err - error number whose appropriate text message to be returned
 *
 * @return	string
 * @retval	text error msg	if such error exists
 * @retval	NULL		ni such error num
 *
 */

char *
pbse_to_txt(int err)
{
	int i = 0;

	while (pbs_err_to_txt[i].err_no && (pbs_err_to_txt[i].err_no != err))
		++i;
	if (pbs_err_to_txt[i].err_txt != NULL)
		return (*pbs_err_to_txt[i].err_txt);
	else
		return NULL;
}
