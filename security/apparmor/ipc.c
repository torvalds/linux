// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor ipc mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 */

#include <linux/gfp.h>

#include "include/audit.h"
#include "include/capability.h"
#include "include/cred.h"
#include "include/policy.h"
#include "include/ipc.h"
#include "include/sig_names.h"


static inline int map_signal_num(int sig)
{
	if (sig > SIGRTMAX)
		return SIGUNKNOWN;
	else if (sig >= SIGRTMIN)
		return sig - SIGRTMIN + SIGRT_BASE;
	else if (sig < MAXMAPPED_SIG)
		return sig_map[sig];
	return SIGUNKNOWN;
}

/**
 * audit_signal_mask - convert mask to permission string
 * @mask: permission mask to convert
 *
 * Returns: pointer to static string
 */
static const char *audit_signal_mask(u32 mask)
{
	if (mask & MAY_READ)
		return "receive";
	if (mask & MAY_WRITE)
		return "send";
	return "";
}

/**
 * audit_signal_cb() - call back for signal specific audit fields
 * @ab: audit_buffer  (NOT NULL)
 * @va: audit struct to audit values of  (NOT NULL)
 */
static void audit_signal_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;

	if (aad(sa)->request & AA_SIGNAL_PERM_MASK) {
		audit_log_format(ab, " requested_mask=\"%s\"",
				 audit_signal_mask(aad(sa)->request));
		if (aad(sa)->denied & AA_SIGNAL_PERM_MASK) {
			audit_log_format(ab, " denied_mask=\"%s\"",
					 audit_signal_mask(aad(sa)->denied));
		}
	}
	if (aad(sa)->signal == SIGUNKNOWN)
		audit_log_format(ab, "signal=unknown(%d)",
				 aad(sa)->unmappedsig);
	else if (aad(sa)->signal < MAXMAPPED_SIGNAME)
		audit_log_format(ab, " signal=%s", sig_names[aad(sa)->signal]);
	else
		audit_log_format(ab, " signal=rtmin+%d",
				 aad(sa)->signal - SIGRT_BASE);
	audit_log_format(ab, " peer=");
	aa_label_xaudit(ab, labels_ns(aad(sa)->label), aad(sa)->peer,
			FLAGS_NONE, GFP_ATOMIC);
}

static int profile_signal_perm(struct aa_profile *profile,
			       struct aa_label *peer, u32 request,
			       struct common_audit_data *sa)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	struct aa_perms perms;
	aa_state_t state;

	if (profile_unconfined(profile) ||
	    !ANY_RULE_MEDIATES(&profile->rules, AA_CLASS_SIGNAL))
		return 0;

	aad(sa)->peer = peer;
	/* TODO: secondary cache check <profile, profile, perm> */
	state = aa_dfa_next(rules->policy.dfa,
			    rules->policy.start[AA_CLASS_SIGNAL],
			    aad(sa)->signal);
	aa_label_match(profile, rules, peer, state, false, request, &perms);
	aa_apply_modes_to_perms(profile, &perms);
	return aa_check_perms(profile, &perms, request, sa, audit_signal_cb);
}

int aa_may_signal(struct aa_label *sender, struct aa_label *target, int sig)
{
	struct aa_profile *profile;
	DEFINE_AUDIT_DATA(sa, LSM_AUDIT_DATA_NONE, AA_CLASS_SIGNAL, OP_SIGNAL);

	aad(&sa)->signal = map_signal_num(sig);
	aad(&sa)->unmappedsig = sig;
	return xcheck_labels(sender, target, profile,
			profile_signal_perm(profile, target, MAY_WRITE, &sa),
			profile_signal_perm(profile, sender, MAY_READ, &sa));
}
