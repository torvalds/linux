/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor inet fine grained mediation
 *
 * Copyright 2024 Canonical Ltd.
 *
 */

#include <net/tcp_states.h>

#include "include/audit.h"
#include "include/af_inet.h"
#include "include/apparmor.h"
#include "include/file.h"
#include "include/label.h"
#include "include/net.h"
#include "include/path.h"
#include "include/policy.h"
#include "include/cred.h"


/* no kernel_t early bailout */
/* NOTE: already lifted label_mediates into lsm.c */
int aa_inet_create_perm(struct aa_label *label, int family, int type,
			int protocol)
{
	return aa_af_perm(current_cred(), label, OP_CREATE,
			  AA_MAY_CREATE, family, type,
			  protocol);
}

int aa_inet_bind_perm(struct socket *sock, struct sockaddr *addr,
		      int addrlen)
{
	return aa_sk_perm(OP_BIND, AA_MAY_BIND, sock->sk);
}

int aa_inet_connect_perm(struct socket *sock, struct sockaddr *addr,
			 int addrlen)
{
	return aa_sk_perm(OP_CONNECT, AA_MAY_CONNECT, sock->sk);
}

int aa_inet_listen_perm(struct socket *sock, int backlog)
{
	return aa_sk_perm(OP_LISTEN, AA_MAY_LISTEN, sock->sk);
}

/* ability of sock to connect, not peer address binding */
int aa_inet_accept_perm(struct socket *sock, struct socket *newsock)
{
	return aa_sk_perm(OP_ACCEPT, AA_MAY_ACCEPT, sock->sk);
}

/* sendmsg, recvmsg. */
int aa_inet_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size)
{
	return aa_sk_perm(op, request, sock->sk);
}

/* getopt, setopt */
int aa_inet_opt_perm(const char *op, u32 request, struct socket *sock,
		     int level, int optname)
{
	return aa_sk_perm(op, request, sock->sk);
}

/* revaliation, get/set attr/getsockname/peername */
int aa_inet_sock_perm(const char *op, u32 request, struct socket *sock)
{
	return aa_sk_perm(op, request, sock->sk);
}

int aa_inet_file_perm(const struct cred *subj_cred, struct aa_label *label,
		      const char *op, u32 request, struct socket *sock)
{
	return aa_label_sk_perm(subj_cred, label, op, request, sock->sk);
}
