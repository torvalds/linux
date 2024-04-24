// SPDX-License-Identifier: GPL-2.0-only
/*
 * AppArmor security module
 *
 * This file contains AppArmor network mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 */

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/cred.h"
#include "include/label.h"
#include "include/net.h"
#include "include/policy.h"
#include "include/secid.h"

#include "net_names.h"


struct aa_sfs_entry aa_sfs_entry_network[] = {
	AA_SFS_FILE_STRING("af_mask",	AA_SFS_AF_MASK),
	{ }
};

static const char * const net_mask_names[] = {
	"unknown",
	"send",
	"receive",
	"unknown",

	"create",
	"shutdown",
	"connect",
	"unknown",

	"setattr",
	"getattr",
	"setcred",
	"getcred",

	"chmod",
	"chown",
	"chgrp",
	"lock",

	"mmap",
	"mprot",
	"unknown",
	"unknown",

	"accept",
	"bind",
	"listen",
	"unknown",

	"setopt",
	"getopt",
	"unknown",
	"unknown",

	"unknown",
	"unknown",
	"unknown",
	"unknown",
};


/* audit callback for net specific fields */
void audit_net_cb(struct audit_buffer *ab, void *va)
{
	struct common_audit_data *sa = va;
	struct apparmor_audit_data *ad = aad(sa);

	if (address_family_names[sa->u.net->family])
		audit_log_format(ab, " family=\"%s\"",
				 address_family_names[sa->u.net->family]);
	else
		audit_log_format(ab, " family=\"unknown(%d)\"",
				 sa->u.net->family);
	if (sock_type_names[ad->net.type])
		audit_log_format(ab, " sock_type=\"%s\"",
				 sock_type_names[ad->net.type]);
	else
		audit_log_format(ab, " sock_type=\"unknown(%d)\"",
				 ad->net.type);
	audit_log_format(ab, " protocol=%d", ad->net.protocol);

	if (ad->request & NET_PERMS_MASK) {
		audit_log_format(ab, " requested_mask=");
		aa_audit_perm_mask(ab, ad->request, NULL, 0,
				   net_mask_names, NET_PERMS_MASK);

		if (ad->denied & NET_PERMS_MASK) {
			audit_log_format(ab, " denied_mask=");
			aa_audit_perm_mask(ab, ad->denied, NULL, 0,
					   net_mask_names, NET_PERMS_MASK);
		}
	}
	if (ad->peer) {
		audit_log_format(ab, " peer=");
		aa_label_xaudit(ab, labels_ns(ad->subj_label), ad->peer,
				FLAGS_NONE, GFP_ATOMIC);
	}
}

/* standard permission lookup pattern - supports early bailout */
static int do_perms(struct aa_profile *profile, struct aa_policydb *policy,
		    unsigned int state, u32 request,
		    struct aa_perms *p, struct apparmor_audit_data *ad)
{
	struct aa_perms perms;

	AA_BUG(!profile);
	AA_BUG(!policy);


	if (state || !p)
		p = aa_lookup_perms(policy, state);
	perms = *p;
	aa_apply_modes_to_perms(profile, &perms);
	return aa_check_perms(profile, &perms, request, ad,
			      audit_net_cb);
}

/* only continue match if
 *   insufficient current perms at current state
 *   indicates there are more perms in later state
 * Returns: perms struct if early match
 */
static struct aa_perms *early_match(struct aa_policydb *policy,
				    aa_state_t state, u32 request)
{
	struct aa_perms *p;

	p = aa_lookup_perms(policy, state);
	if (((p->allow & request) != request) && (p->allow & AA_CONT_MATCH))
		return NULL;
	return p;
}

/* passing in state returned by PROFILE_MEDIATES_AF */
aa_state_t aa_match_to_prot(struct aa_policydb *policy, aa_state_t state,
			    u32 request, u16 family, int type, int protocol,
			    struct aa_perms **p, const char **info)
{
	__be16 buffer;

	buffer = cpu_to_be16(family);
	state = aa_dfa_match_len(policy->dfa, state, (char *) &buffer, 2);
	if (!state) {
		*info = "failed af match";
		return DFA_NOMATCH;
	}
	buffer = cpu_to_be16((u16)type);
	state = aa_dfa_match_len(policy->dfa, state, (char *) &buffer, 2);
	if (!state)
		*info = "failed type match";
	*p = early_match(policy, state, request);
	if (!*p) {
		buffer = cpu_to_be16((u16)protocol);
		state = aa_dfa_match_len(policy->dfa, state, (char *) &buffer,
					 2);
		if (!state)
			*info = "failed protocol match";
	}
	return state;
}

/* Generic af perm */
int aa_profile_af_perm(struct aa_profile *profile,
		       struct apparmor_audit_data *ad, u32 request, u16 family,
		       int type, int protocol)
{
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);
	struct aa_perms *p = NULL;
	aa_state_t state;

	AA_BUG(family >= AF_MAX);
	AA_BUG(type < 0 || type >= SOCK_MAX);
	AA_BUG(profile_unconfined(profile));

	state = RULE_MEDIATES(rules, AA_CLASS_NET);
	if (!state)
		return 0;

	state = aa_match_to_prot(rules->policy, state, request, family, type,
				 protocol, &p, &ad->info);
	return do_perms(profile, rules->policy, state, request, p, ad);
}

int aa_af_perm(const struct cred *subj_cred, struct aa_label *label,
	       const char *op, u32 request, u16 family, int type, int protocol)
{
	struct aa_profile *profile;
	DEFINE_AUDIT_NET(ad, op, NULL, family, type, protocol);

	return fn_for_each_confined(label, profile,
			aa_profile_af_perm(profile, &ad, request, family,
					   type, protocol));
}

static int aa_label_sk_perm(const struct cred *subj_cred,
			    struct aa_label *label,
			    const char *op, u32 request,
			    struct sock *sk)
{
	struct aa_sk_ctx *ctx = aa_sock(sk);
	int error = 0;

	AA_BUG(!label);
	AA_BUG(!sk);

	if (ctx->label != kernel_t && !unconfined(label)) {
		struct aa_profile *profile;
		DEFINE_AUDIT_SK(ad, op, sk);

		ad.subj_cred = subj_cred;
		error = fn_for_each_confined(label, profile,
			    aa_profile_af_sk_perm(profile, &ad, request, sk));
	}

	return error;
}

int aa_sk_perm(const char *op, u32 request, struct sock *sk)
{
	struct aa_label *label;
	int error;

	AA_BUG(!sk);
	AA_BUG(in_interrupt());

	/* TODO: switch to begin_current_label ???? */
	label = begin_current_label_crit_section();
	error = aa_label_sk_perm(current_cred(), label, op, request, sk);
	end_current_label_crit_section(label);

	return error;
}


int aa_sock_file_perm(const struct cred *subj_cred, struct aa_label *label,
		      const char *op, u32 request, struct socket *sock)
{
	AA_BUG(!label);
	AA_BUG(!sock);
	AA_BUG(!sock->sk);

	return aa_label_sk_perm(subj_cred, label, op, request, sock->sk);
}

#ifdef CONFIG_NETWORK_SECMARK
static int apparmor_secmark_init(struct aa_secmark *secmark)
{
	struct aa_label *label;

	if (secmark->label[0] == '*') {
		secmark->secid = AA_SECID_WILDCARD;
		return 0;
	}

	label = aa_label_strn_parse(&root_ns->unconfined->label,
				    secmark->label, strlen(secmark->label),
				    GFP_ATOMIC, false, false);

	if (IS_ERR(label))
		return PTR_ERR(label);

	secmark->secid = label->secid;

	return 0;
}

static int aa_secmark_perm(struct aa_profile *profile, u32 request, u32 secid,
			   struct apparmor_audit_data *ad)
{
	int i, ret;
	struct aa_perms perms = { };
	struct aa_ruleset *rules = list_first_entry(&profile->rules,
						    typeof(*rules), list);

	if (rules->secmark_count == 0)
		return 0;

	for (i = 0; i < rules->secmark_count; i++) {
		if (!rules->secmark[i].secid) {
			ret = apparmor_secmark_init(&rules->secmark[i]);
			if (ret)
				return ret;
		}

		if (rules->secmark[i].secid == secid ||
		    rules->secmark[i].secid == AA_SECID_WILDCARD) {
			if (rules->secmark[i].deny)
				perms.deny = ALL_PERMS_MASK;
			else
				perms.allow = ALL_PERMS_MASK;

			if (rules->secmark[i].audit)
				perms.audit = ALL_PERMS_MASK;
		}
	}

	aa_apply_modes_to_perms(profile, &perms);

	return aa_check_perms(profile, &perms, request, ad, audit_net_cb);
}

int apparmor_secmark_check(struct aa_label *label, char *op, u32 request,
			   u32 secid, const struct sock *sk)
{
	struct aa_profile *profile;
	DEFINE_AUDIT_SK(ad, op, sk);

	return fn_for_each_confined(label, profile,
				    aa_secmark_perm(profile, request, secid,
						    &ad));
}
#endif
