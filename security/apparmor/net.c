/*
 * AppArmor security module
 *
 * This file contains AppArmor network mediation
 *
 * Copyright (C) 1998-2008 Novell/SUSE
 * Copyright 2009-2017 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */

#include "include/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/label.h"
#include "include/net.h"
#include "include/policy.h"

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

	audit_log_format(ab, " family=");
	if (address_family_names[sa->u.net->family])
		audit_log_string(ab, address_family_names[sa->u.net->family]);
	else
		audit_log_format(ab, "\"unknown(%d)\"", sa->u.net->family);
	audit_log_format(ab, " sock_type=");
	if (sock_type_names[aad(sa)->net.type])
		audit_log_string(ab, sock_type_names[aad(sa)->net.type]);
	else
		audit_log_format(ab, "\"unknown(%d)\"", aad(sa)->net.type);
	audit_log_format(ab, " protocol=%d", aad(sa)->net.protocol);

	if (aad(sa)->request & NET_PERMS_MASK) {
		audit_log_format(ab, " requested_mask=");
		aa_audit_perm_mask(ab, aad(sa)->request, NULL, 0,
				   net_mask_names, NET_PERMS_MASK);

		if (aad(sa)->denied & NET_PERMS_MASK) {
			audit_log_format(ab, " denied_mask=");
			aa_audit_perm_mask(ab, aad(sa)->denied, NULL, 0,
					   net_mask_names, NET_PERMS_MASK);
		}
	}
	if (aad(sa)->peer) {
		audit_log_format(ab, " peer=");
		aa_label_xaudit(ab, labels_ns(aad(sa)->label), aad(sa)->peer,
				FLAGS_NONE, GFP_ATOMIC);
	}
}


/* Generic af perm */
int aa_profile_af_perm(struct aa_profile *profile, struct common_audit_data *sa,
		       u32 request, u16 family, int type)
{
	struct aa_perms perms = { };

	AA_BUG(family >= AF_MAX);
	AA_BUG(type < 0 || type >= SOCK_MAX);

	if (profile_unconfined(profile))
		return 0;

	perms.allow = (profile->net.allow[family] & (1 << type)) ?
		ALL_PERMS_MASK : 0;
	perms.audit = (profile->net.audit[family] & (1 << type)) ?
		ALL_PERMS_MASK : 0;
	perms.quiet = (profile->net.quiet[family] & (1 << type)) ?
		ALL_PERMS_MASK : 0;
	aa_apply_modes_to_perms(profile, &perms);

	return aa_check_perms(profile, &perms, request, sa, audit_net_cb);
}

int aa_af_perm(struct aa_label *label, const char *op, u32 request, u16 family,
	       int type, int protocol)
{
	struct aa_profile *profile;
	DEFINE_AUDIT_NET(sa, op, NULL, family, type, protocol);

	return fn_for_each_confined(label, profile,
			aa_profile_af_perm(profile, &sa, request, family,
					   type));
}

static int aa_label_sk_perm(struct aa_label *label, const char *op, u32 request,
			    struct sock *sk)
{
	struct aa_profile *profile;
	DEFINE_AUDIT_SK(sa, op, sk);

	AA_BUG(!label);
	AA_BUG(!sk);

	if (unconfined(label))
		return 0;

	return fn_for_each_confined(label, profile,
			aa_profile_af_sk_perm(profile, &sa, request, sk));
}

int aa_sk_perm(const char *op, u32 request, struct sock *sk)
{
	struct aa_label *label;
	int error;

	AA_BUG(!sk);
	AA_BUG(in_interrupt());

	/* TODO: switch to begin_current_label ???? */
	label = begin_current_label_crit_section();
	error = aa_label_sk_perm(label, op, request, sk);
	end_current_label_crit_section(label);

	return error;
}


int aa_sock_file_perm(struct aa_label *label, const char *op, u32 request,
		      struct socket *sock)
{
	AA_BUG(!label);
	AA_BUG(!sock);
	AA_BUG(!sock->sk);

	return aa_label_sk_perm(label, op, request, sock->sk);
}
