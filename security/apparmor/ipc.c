/*
 * AppArmor security module
 *
 * This file contains AppArmor ipc mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
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

static int cross_ptrace_perm(struct aa_profile *tracer,
			     struct aa_profile *tracee, u32 request,
			     struct common_audit_data *sa)
{
	/* policy uses the old style capability check for ptrace */
	if (profile_unconfined(tracer) || tracer == tracee)
		return 0;

	aad(sa)->label = &tracer->label;
	aad(sa)->peer = &tracee->label;
	aad(sa)->request = 0;
	aad(sa)->error = aa_capable(&tracer->label, CAP_SYS_PTRACE, 1);

	return aa_audit(AUDIT_APPARMOR_AUTO, tracer, sa, audit_ptrace_cb);
}

/**
 * aa_may_ptrace - test if tracer task can trace the tracee
 * @tracer: label of the task doing the tracing  (NOT NULL)
 * @tracee: task label to be traced
 * @request: permission request
 *
 * Returns: %0 else error code if permission denied or error
 */
int aa_may_ptrace(struct aa_label *tracer, struct aa_label *tracee,
		  u32 request)
{
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, OP_PTRACE);

	return xcheck_labels_profiles(tracer, tracee, cross_ptrace_perm,
				      request, &sa);
}


