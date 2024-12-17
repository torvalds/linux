// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor auditing functions
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2010 Canonical Ltd.
 */

#include <linux/audit.h>
#include <linux/socket.h>

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/policy.h"
#include "include/policy_ns.h"
#include "include/secid.h"

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

static const char *const aa_class_names[] = {
	"none",
	"unknown",
	"file",
	"cap",
	"net",
	"rlimits",
	"domain",
	"mount",
	"unknown",
	"ptrace",
	"signal",
	"xmatch",
	"unknown",
	"unknown",
	"net",
	"unknown",
	"label",
	"posix_mqueue",
	"io_uring",
	"module",
	"lsm",
	"namespace",
	"io_uring",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"unknown",
	"X",
	"dbus",
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
 * audit_pre() - core AppArmor function.
 * @ab: audit buffer to fill (NOT NULL)
 * @va: audit structure containing data to audit (NOT NULL)
 *
 * Record common AppArmor audit data from @va
 */
static void audit_pre(struct audit_buffer *ab, void *va)
{
	struct apparmor_audit_data *ad = aad_of_va(va);

	if (aa_g_audit_header) {
		audit_log_format(ab, "apparmor=\"%s\"",
				 aa_audit_type[ad->type]);
	}

	if (ad->op)
		audit_log_format(ab, " operation=\"%s\"", ad->op);

	if (ad->class)
		audit_log_format(ab, " class=\"%s\"",
				 ad->class <= AA_CLASS_LAST ?
				 aa_class_names[ad->class] :
				 "unknown");

	if (ad->info) {
		audit_log_format(ab, " info=\"%s\"", ad->info);
		if (ad->error)
			audit_log_format(ab, " error=%d", ad->error);
	}

	if (ad->subj_label) {
		struct aa_label *label = ad->subj_label;

		if (label_isprofile(label)) {
			struct aa_profile *profile = labels_profile(label);

			if (profile->ns != root_ns) {
				audit_log_format(ab, " namespace=");
				audit_log_untrustedstring(ab,
						       profile->ns->base.hname);
			}
			audit_log_format(ab, " profile=");
			audit_log_untrustedstring(ab, profile->base.hname);
		} else {
			audit_log_format(ab, " label=");
			aa_label_xaudit(ab, root_ns, label, FLAG_VIEW_SUBNS,
					GFP_ATOMIC);
		}
	}

	if (ad->name) {
		audit_log_format(ab, " name=");
		audit_log_untrustedstring(ab, ad->name);
	}
}

/**
 * aa_audit_msg - Log a message to the audit subsystem
 * @type: audit type for the message
 * @ad: audit event structure (NOT NULL)
 * @cb: optional callback fn for type specific fields (MAYBE NULL)
 */
void aa_audit_msg(int type, struct apparmor_audit_data *ad,
		  void (*cb) (struct audit_buffer *, void *))
{
	ad->type = type;
	common_lsm_audit(&ad->common, audit_pre, cb);
}

/**
 * aa_audit - Log a profile based audit event to the audit subsystem
 * @type: audit type for the message
 * @profile: profile to check against (NOT NULL)
 * @ad: audit event (NOT NULL)
 * @cb: optional callback fn for type specific fields (MAYBE NULL)
 *
 * Handle default message switching based off of audit mode flags
 *
 * Returns: error on failure
 */
int aa_audit(int type, struct aa_profile *profile,
	     struct apparmor_audit_data *ad,
	     void (*cb) (struct audit_buffer *, void *))
{
	AA_BUG(!profile);

	if (type == AUDIT_APPARMOR_AUTO) {
		if (likely(!ad->error)) {
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
	     AUDIT_MODE(profile) == AUDIT_QUIET_DENIED))
		return ad->error;

	if (KILL_MODE(profile) && type == AUDIT_APPARMOR_DENIED)
		type = AUDIT_APPARMOR_KILL;

	ad->subj_label = &profile->label;

	aa_audit_msg(type, ad, cb);

	if (ad->type == AUDIT_APPARMOR_KILL)
		(void)send_sig_info(SIGKILL, NULL,
			ad->common.type == LSM_AUDIT_DATA_TASK &&
			ad->common.u.tsk ? ad->common.u.tsk : current);

	if (ad->type == AUDIT_APPARMOR_ALLOWED)
		return complain_error(ad->error);

	return ad->error;
}

struct aa_audit_rule {
	struct aa_label *label;
};

void aa_audit_rule_free(void *vrule)
{
	struct aa_audit_rule *rule = vrule;

	if (rule) {
		if (!IS_ERR(rule->label))
			aa_put_label(rule->label);
		kfree(rule);
	}
}

int aa_audit_rule_init(u32 field, u32 op, char *rulestr, void **vrule, gfp_t gfp)
{
	struct aa_audit_rule *rule;

	switch (field) {
	case AUDIT_SUBJ_ROLE:
		if (op != Audit_equal && op != Audit_not_equal)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	rule = kzalloc(sizeof(struct aa_audit_rule), gfp);

	if (!rule)
		return -ENOMEM;

	/* Currently rules are treated as coming from the root ns */
	rule->label = aa_label_parse(&root_ns->unconfined->label, rulestr,
				     gfp, true, false);
	if (IS_ERR(rule->label)) {
		int err = PTR_ERR(rule->label);
		aa_audit_rule_free(rule);
		return err;
	}

	*vrule = rule;
	return 0;
}

int aa_audit_rule_known(struct audit_krule *rule)
{
	int i;

	for (i = 0; i < rule->field_count; i++) {
		struct audit_field *f = &rule->fields[i];

		switch (f->type) {
		case AUDIT_SUBJ_ROLE:
			return 1;
		}
	}

	return 0;
}

int aa_audit_rule_match(struct lsm_prop *prop, u32 field, u32 op, void *vrule)
{
	struct aa_audit_rule *rule = vrule;
	struct aa_label *label;
	int found = 0;

	label = prop->apparmor.label;

	if (!label)
		return -ENOENT;

	if (aa_label_is_subset(label, rule->label))
		found = 1;

	switch (field) {
	case AUDIT_SUBJ_ROLE:
		switch (op) {
		case Audit_equal:
			return found;
		case Audit_not_equal:
			return !found;
		}
	}
	return 0;
}
