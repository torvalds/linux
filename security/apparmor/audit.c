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
#include "include/policy_ns.h"


const char *const audit_mode_names[] = {
	"normal",
	"quiet_denied",
	"quiet",
	"noquiet",
	"all"
};

static const char *const aa_audit_type[] = {
	"AUDIT",
	"ALLOWED",
	"DENIED",
	"HINT",
	"STATUS",
	"ERROR",
	"KILLED",
	"AUTO"
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

	if (aa_g_audit_header) {
		audit_log_format(ab, "apparmor=");
		audit_log_string(ab, aa_audit_type[aad(sa)->type]);
	}

	if (aad(sa)->op) {
		audit_log_format(ab, " operation=");
		audit_log_string(ab, aad(sa)->op);
	}

	if (aad(sa)->info) {
		audit_log_format(ab, " info=");
		audit_log_string(ab, aad(sa)->info);
		if (aad(sa)->error)
			audit_log_format(ab, " error=%d", aad(sa)->error);
	}

	if (aad(sa)->profile) {
		struct aa_profile *profile = aad(sa)->profile;
		if (profile->ns != root_ns) {
			audit_log_format(ab, " namespace=");
			audit_log_untrustedstring(ab, profile->ns->base.hname);
		}
		audit_log_format(ab, " profile=");
		audit_log_untrustedstring(ab, profile->base.hname);
	}

	if (aad(sa)->name) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, aad(sa)->name);
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
	aad(sa)->type = type;
	common_lsm_audit(sa, audit_pre, cb);
}

/**
 * aa_audit - Log a profile based audit event to the audit subsystem
 * @type: audit type for the message
 * @profile: profile to check against (NOT NULL)
 * @sa: audit event (NOT NULL)
 * @cb: optional callback fn for type specific fields (MAYBE NULL)
 *
 * Handle default message switching based off of audit mode flags
 *
 * Returns: error on failure
 */
int aa_audit(int type, struct aa_profile *profile, struct common_audit_data *sa,
	     void (*cb) (struct audit_buffer *, void *))
{
	AA_BUG(!profile);

	if (type == AUDIT_APPARMOR_AUTO) {
		if (likely(!aad(sa)->error)) {
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
		return aad(sa)->error;

	if (KILL_MODE(profile) && type == AUDIT_APPARMOR_DENIED)
		type = AUDIT_APPARMOR_KILL;

	if (!unconfined(profile))
		aad(sa)->profile = profile;

	aa_audit_msg(type, sa, cb);

	if (aad(sa)->type == AUDIT_APPARMOR_KILL)
		(void)send_sig_info(SIGKILL, NULL,
			sa->type == LSM_AUDIT_DATA_TASK && sa->u.tsk ?
				    sa->u.tsk : current);

	if (aad(sa)->type == AUDIT_APPARMOR_ALLOWED)
		return complain_error(aad(sa)->error);

	return aad(sa)->error;
}
