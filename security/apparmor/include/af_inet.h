/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * AppArmor security module
 *
 * This file contains AppArmor inet/inet6 fine grained mediation
 *
 * Copyright 2024 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */
#ifndef __AA_AF_INET_H
#define __AA_AF_INET_H

#include "label.h"

int aa_inet_sock_perm(const char *op, u32 request, struct socket *sock);
int aa_inet_create_perm(struct aa_label *label, int family, int type,
			int protocol);
int aa_inet_bind_perm(struct socket *sock, struct sockaddr *address,
		      int addrlen);
int aa_inet_connect_perm(struct socket *sock, struct sockaddr *address,
			 int addrlen);
int aa_inet_listen_perm(struct socket *sock, int backlog);
int aa_inet_accept_perm(struct socket *sock, struct socket *newsock);
int aa_inet_msg_perm(const char *op, u32 request, struct socket *sock,
		     struct msghdr *msg, int size);
int aa_inet_opt_perm(const char *op, u32 request, struct socket *sock, int level,
		     int optname);
int aa_inet_file_perm(const struct cred *subj_cred,
		      struct aa_label *label, const char *op, u32 request,
		      struct socket *sock);

#endif /* __AA_AF_INET_H */
