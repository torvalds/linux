/*
 * AppArmor security module
 *
 * This file contains AppArmor auditing functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/audit.h>
#include <linux/socket.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/policy.h"

const char *op_table[] = {
	"null",

	"sysctl",
	"capable",

	"unlink",
	"mkdir",
	"rmdir",
	"mknod",
	"truncate",
	"link",
	"symlink",
	"rename_src",
	"rename_dest",
	"chmod",
	"chown",
	"getattr",
	"open",

	"file_perm",
	"file_lock",
	"file_mmap",
	"file_mprotect",

	"create",
	"post_create",
	"bind",
	"connect",
	"listen",
	"accept",
	"sendmsg",
	"recvmsg",
	"getsockname",
	"getpeername",
	"getsockopt",
	"setsockopt",
	"socket_shutdown",

	"ptrace",

	"exec",
	"change_hat",
	"change_profile",
	"change_onexec",

	"setprocattr",
	"setrlimit",

	"profile_replace",
	"profile_load",
	"profile_remove"
};

const char *audit_mode_names[] = {
	"normal",
	"quiet_denied",
	"quiet",
	"noquiet",
	"all"
};

static char *aa_audit_type[] = {
	"AUDIT",
	"ALLOWED",
	"DENIED",
	"HINT",
	"STATUS",
	"ERROR",
	"KILLED"
};

/*
 * Currently AppArmor auditing is fed straight into the audit framework.
 *
 * TODO:
 * netlink interface for complain mode
 * user auditing, - send user auditing to netlink interface
 * system control of whether user audit messages go to system log
 */

/**
 * audit_base - core AppArmor function.
 * @ab: audit buffer to fill (NOT NULL)
 * @ca: audit structure containing data to audit (NOT NULL)
 *
 * Record common AppArmor audit data from @sa
 */
static void audit_pre(struct audit_buffer *ab, void *ca)
{
	struct common_audit_data *sa = ca;
	struct task_struct *tsk = sa->tsk ? sa->tsk : current;

	if (aa_g_audit_header) {
		audit_log_format(ab, "apparmor=");
		audit_log_string(ab, aa_audit_type[sa->aad.type]);
	}

	if (sa->aad.op) {
		audit_log_format(ab, " operation=");
		audit_log_string(ab, op_table[sa->aad.op]);
	}

	if (sa->aad.info) {
		audit_log_format(ab, " info=");
		audit_log_string(ab, sa->aad.info);
		if (sa->aad.error)
			audit_log_format(ab, " error=%d", sa->aad.error);
	}

	if (sa->aad.profile) {
		struct aa_profile *profile = sa->aad.profile;
		pid_t pid;
		rcu_read_lock();
		pid = tsk->real_parent->pid;
		rcu_read_unlock();
		audit_log_format(ab, " parent=%d", pid);
		if (profile->ns != root_ns) {
			audit_log_format(ab, " namespace=");
			audit_log_untrustedstring(ab, profile->ns->base.hname);
		}
		audit_log_format(ab, " profile=");
		audit_log_untrustedstring(ab, profile->base.hname);
	}

	if (sa->aad.name) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, sa->aad.name);
	}
}

/**
 * aa_audit_msg - Log a message to the audit subsystem
 * @sa: audit event structure (NOT NULL)
 * @cb: optional callback fn for type specific fields (MAYBE NULL)
 */
void aa_audit_msg(int type, struct common_audit_data *sa,
		  void (*cb) (struct audit_buffer *, void *))
{
	sa->aad.type = type;
	sa->lsm_pre_audit = audit_pre;
	sa->lsm_post_audit = cb;
	common_lsm_audit(sa);
}

/**
 * aa_audit - Log a profile based audit event to the audit subsystem
 * @type: audit type for the message
 * @profile: profile to check against (NOT NULL)
 * @gfp: allocation flags to use
 * @sa: audit event (NOT NULL)
 * @cb: optional callback fn for type specific fields (MAYBE NULL)
 *
 * Handle default message switching based off of audit mode flags
 *
 * Returns: error on failure
 */
int aa_audit(int type, struct aa_profile *profile, gfp_t gfp,
	     struct common_audit_data *sa,
	     void (*cb) (struct audit_buffer *, void *))
{
	BUG_ON(!profile);

	if (type == AUDIT_APPARMOR_AUTO) {
		if (likely(!sa->aad.error)) {
			if (AUDIT_MODE(profile) != AUDIT_ALL)
				return 0;
			type = AUDIT_APPARMOR_AUDIT;
		} else if (COMPLAIN_MODE(profile))
			type = AUDIT_APPARMOR_ALLOWED;
		else
			type = AUDIT_APPARMOR_DENIED;
	}
	if (AUDIT_MODE(profile) == AUDIT_QUIET ||
	    (type == AUDIT_APPARMOR_DENIED &&
	     AUDIT_MODE(profile) == AUDIT_QUIET))
		return sa->aad.error;

	if (KILL_MODE(profile) && type == AUDIT_APPARMOR_DENIED)
		type = AUDIT_APPARMOR_KILL;

	if (!unconfined(profile))
		sa->aad.profile = profile;

	aa_audit_msg(type, sa, cb);

	if (sa->aad.type == AUDIT_APPARMOR_KILL)
		(void)send_sig_info(SIGKILL, NULL, sa->tsk ? sa->tsk : current);

	if (sa->aad.type == AUDIT_APPARMOR_ALLOWED)
		return complain_error(sa->aad.error);

	return sa->aad.error;
}
