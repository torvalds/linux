/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor af_unix fine grained mediation
 *
 * Copyright 2023 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */
#ifndef __AA_AF_UNIX_H

#include <net/af_unix.h>

#include "label.h"

#define unix_addr(A) ((struct sockaddr_un *)(A))
#define unix_addr_len(L) ((L) - sizeof(sa_family_t))
#define unix_peer(sk) (unix_sk(sk)->peer)
#define is_unix_addr_abstract_name(B) ((B)[0] == 0)
#define is_unix_addr_anon(A, L) ((A) && unix_addr_len(L) <= 0)
#define is_unix_addr_fs(A, L) (!is_unix_addr_anon(A, L) && \
			    !is_unix_addr_abstract_name(unix_addr(A)->sun_path))

#define is_unix_anonymous(U) (!unix_sk(U)->addr)
#define is_unix_fs(U) (!is_unix_anonymous(U) &&			\
		       unix_sk(U)->addr->name->sun_path[0])
#define is_unix_connected(S) ((S)->state == SS_CONNECTED)


struct sockaddr_un *aa_sunaddr(const struct unix_sock *u, int *addrlen);
int aa_unix_peer_perm(const struct cred *subj_cred,
		      struct aa_label *label, const char *op, u32 request,
		      struct sock *sk, struct sock *peer_sk,
		      struct aa_label *peer_label);
int aa_unix_sock_perm(const char *op, u32 request, struct socket *sock);
int aa_unix_create_perm(struct aa_label *label, int family, int type,
			int protocol);
int aa_unix_bind_perm(struct socket *sock, struct sockaddr *address,
		      int addrlen);
int aa_unix_connect_perm(struct socket *sock, struct sockaddr *address,
			 int addrlen);
int aa_unix_listen_perm(struct socket *sock, int backlog);
int aa_unix_accept_perm(struct socket *sock, struct socket *newsock);
int aa_unix_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size);
int aa_unix_opt_perm(const char *op, u32 request, struct socket *sock, int level,
		     int optname);
int aa_unix_file_perm(const struct cred *subj_cred, struct aa_label *label,
		      const char *op, u32 request, struct file *file);

#endif /* __AA_AF_UNIX_H */
