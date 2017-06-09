/*
 * AppArmor security module
 *
 * This file contains AppArmor ipc mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include <linux/gfp.h>
#include <linux/ptrace.h>

#include "include/audit.h"
#include "include/capability.h"
#include "include/context.h"
#include "include/policy.h"
#include "include/ipc.h"

/* call back to audit ptrace fields */
static void audit_ptrace_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;
	audit_log_format(ab, " peer=");
	aa_label_xaudit(ab, labels_ns(aad(sa)->label), aad(sa)->peer,
			FLAGS_NONE, GFP_ATOMIC);
}

/**
 * aa_audit_ptrace - do auditing for ptrace
 * @profile: profile being enforced  (NOT NULL)
 * @target: profile being traced (NOT NULL)
 * @error: error condition
 *
 * Returns: %0 or error code
 */
static int aa_audit_ptrace(struct aa_profile *profile,
			   struct aa_profile *target, int error)
{
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, OP_PTRACE);

	aad(&sa)->peer = &target->label;
	aad(&sa)->error = error;

	return aa_audit(AUDIT_APPARMOR_AUTO, profile, &sa, audit_ptrace_cb);
}

/**
 * aa_may_ptrace - test if tracer task can trace the tracee
 * @tracer: profile of the task doing the tracing  (NOT NULL)
 * @tracee: task to be traced
 * @mode: whether PTRACE_MODE_READ || PTRACE_MODE_ATTACH
 *
 * Returns: %0 else error code if permission denied or error
 */
int aa_may_ptrace(struct aa_profile *tracer, struct aa_profile *tracee,
		  unsigned int mode)
{
	/* TODO: currently only based on capability, not extended ptrace
	 *       rules,
	 *       Test mode for PTRACE_MODE_READ || PTRACE_MODE_ATTACH
	 */

	if (profile_unconfined(tracer) || tracer == tracee)
		return 0;
	/* log this capability request */
	return aa_capable(&tracer->label, CAP_SYS_PTRACE, 1);
}

/**
 * aa_ptrace - do ptrace permission check and auditing
 * @tracer: task doing the tracing (NOT NULL)
 * @tracee: task being traced (NOT NULL)
 * @mode: ptrace mode either PTRACE_MODE_READ || PTRACE_MODE_ATTACH
 *
 * Returns: %0 else error code if permission denied or error
 */
int aa_ptrace(struct task_struct *tracer, struct task_struct *tracee,
	      unsigned int mode)
{
	/*
	 * tracer can ptrace tracee when
	 * - tracer is unconfined ||
	 *   - tracer is in complain mode
	 *   - tracer has rules allowing it to trace tracee currently this is:
	 *       - confined by the same profile ||
	 *       - tracer profile has CAP_SYS_PTRACE
	 */

	struct aa_label *tracer_l = aa_get_task_label(tracer);
	int error = 0;

	if (!unconfined(tracer_l)) {
		struct aa_label *tracee_l = aa_get_task_label(tracee);

		error = aa_may_ptrace(labels_profile(tracer_l),
				      labels_profile(tracee_l),
				      mode);
		error = aa_audit_ptrace(labels_profile(tracer_l),
					labels_profile(tracee_l),
					error);

		aa_put_label(tracee_l);
	}
	aa_put_label(tracer_l);

	return error;
}
