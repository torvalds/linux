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

#include "include/af_unix.h"
#include "include/apparmor.h"
#include "include/audit.h"
#include "include/context.h"
#include "include/label.h"
#include "include/net.h"
#include "include/policy.h"

#include "net_names.h"


struct aa_sfs_entry aa_sfs_entry_network[] = {
	AA_SFS_FILE_STRING("af_mask",	AA_SFS_AF_MASK),
	AA_SFS_FILE_BOOLEAN("af_unix",	1),
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

static void audit_unix_addr(struct audit_buffer *ab, const char *str,
			    struct sockaddr_un *addr, int addrlen)
{
	int len = unix_addr_len(addrlen);

	if (!addr || len <= 0) {
		audit_log_format(ab, " %s=none", str);
	} else if (addr->sun_path[0]) {
		audit_log_format(ab, " %s=", str);
		audit_log_untrustedstring(ab, addr->sun_path);
	} else {
		audit_log_format(ab, " %s=\"@", str);
		if (audit_string_contains_control(&addr->sun_path[1], len - 1))
			audit_log_n_hex(ab, &addr->sun_path[1], len - 1);
		else
			audit_log_format(ab, "%.*s", len - 1,
					 &addr->sun_path[1]);
		audit_log_format(ab, "\"");
	}
}

static void audit_unix_sk_addr(struct audit_buffer *ab, const char *str,
			       struct sock *sk)
{
	struct unix_sock *u = unix_sk(sk);
	if (u && u->addr)
		audit_unix_addr(ab, str, u->addr->name, u->addr->len);
	else
		audit_unix_addr(ab, str, NULL, 0);
}

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
	if (sa->u.net->family == AF_UNIX) {
		if ((aad(sa)->request & ~NET_PEER_MASK) && aad(sa)->net.addr)
			audit_unix_addr(ab, "addr",
					unix_addr(aad(sa)->net.addr),
					aad(sa)->net.addrlen);
		else
			audit_unix_sk_addr(ab, "addr", sa->u.net->sk);
		if (aad(sa)->request & NET_PEER_MASK) {
			if (aad(sa)->net.addr)
				audit_unix_addr(ab, "peer_addr",
						unix_addr(aad(sa)->net.addr),
						aad(sa)->net.addrlen);
			else
				audit_unix_sk_addr(ab, "peer_addr",
						   aad(sa)->net.peer_sk);
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

#define af_select(FAMILY, FN, DEF_FN)		\
({						\
	int __e;				\
	switch ((FAMILY)) {			\
	case AF_UNIX:				\
		__e = aa_unix_ ## FN;		\
		break;				\
	default:				\
		__e = DEF_FN;			\
	}					\
	__e;					\
})

/* TODO: push into lsm.c ???? */

/* revaliation, get/set attr, shutdown */
int aa_sock_perm(const char *op, u32 request, struct socket *sock)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 sock_perm(op, request, sock),
			 aa_sk_perm(op, request, sock->sk));
}

int aa_sock_create_perm(struct aa_label *label, int family, int type,
			int protocol)
{
	AA_BUG(!label);
	/* TODO: .... */
	AA_BUG(in_interrupt());

	return af_select(family,
			 create_perm(label, family, type, protocol),
			 aa_af_perm(label, OP_CREATE, AA_MAY_CREATE, family,
				    type, protocol));
}

int aa_sock_bind_perm(struct socket *sock, struct sockaddr *address,
		      int addrlen)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!address);
	/* TODO: .... */
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 bind_perm(sock, address, addrlen),
			 aa_sk_perm(OP_BIND, AA_MAY_BIND, sock->sk));
}

int aa_sock_connect_perm(struct socket *sock, struct sockaddr *address,
			 int addrlen)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!address);
	/* TODO: .... */
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 connect_perm(sock, address, addrlen),
			 aa_sk_perm(OP_CONNECT, AA_MAY_CONNECT, sock->sk));
}

int aa_sock_listen_perm(struct socket *sock, int backlog)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	/* TODO: .... */
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 listen_perm(sock, backlog),
			 aa_sk_perm(OP_LISTEN, AA_MAY_LISTEN, sock->sk));
}

/* ability of sock to connect, not peer address binding */
int aa_sock_accept_perm(struct socket *sock, struct socket *newsock)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!newsock);
	/* TODO: .... */
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 accept_perm(sock, newsock),
			 aa_sk_perm(OP_ACCEPT, AA_MAY_ACCEPT, sock->sk));
}

/* sendmsg, recvmsg */
int aa_sock_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(!msg);
	/* TODO: .... */
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 msg_perm(op, request, sock, msg, size),
			 aa_sk_perm(op, request, sock->sk));
}

/* revaliation, get/set attr, opt */
int aa_sock_opt_perm(const char *op, u32 request, struct socket *sock, int level,
		     int optname)
{
	AA_BUG(!sock);
	AA_BUG(!sock->sk);
	AA_BUG(in_interrupt());

	return af_select(sock->sk->sk_family,
			 opt_perm(op, request, sock, level, optname),
			 aa_sk_perm(op, request, sock->sk));
}

int aa_sock_file_perm(struct aa_label *label, const char *op, u32 request,
		      struct socket *sock)
{
	AA_BUG(!label);
	AA_BUG(!sock);
	AA_BUG(!sock->sk);

	return af_select(sock->sk->sk_family,
			 file_perm(label, op, request, sock),
			 aa_label_sk_perm(label, op, request, sock->sk));
}
