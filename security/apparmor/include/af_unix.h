/*
 * AppArmor security module
 *
 * This file contains AppArmor af_unix fine grained mediation
 *
 * Copyright 2014 Canonical Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 */
#ifndef __AA_AF_UNIX_H

#include <net/af_unix.h>

#include "label.h"
//#include "include/net.h"

#define unix_addr_len(L) ((L) - sizeof(sa_family_t))
#define unix_abstract_name_len(L) (unix_addr_len(L) - 1)
#define unix_abstract_len(U) (unix_abstract_name_len((U)->addr->len))
#define addr_unix_abstract_name(B) ((B)[0] == 0)
#define addr_unix_anonymous(U) (addr_unix_len(U) <= 0)
#define addr_unix_abstract(U) (!addr_unix_anonymous(U) && addr_unix_abstract_name((U)->addr))
//#define unix_addr_fs(U) (!unix_addr_anonymous(U) && !unix_addr_abstract_name((U)->addr))

#define unix_addr(A) ((struct sockaddr_un *)(A))
#define unix_addr_anon(A, L) ((A) && unix_addr_len(L) <= 0)
#define unix_addr_fs(A, L) (!unix_addr_anon(A, L) && !addr_unix_abstract_name(unix_addr(A)->sun_path))

#define UNIX_ANONYMOUS(U) (!unix_sk(U)->addr)
/* from net/unix/af_unix.c */
#define UNIX_ABSTRACT(U) (!UNIX_ANONYMOUS(U) &&				\
			  unix_sk(U)->addr->hash < UNIX_HASH_SIZE)
#define UNIX_FS(U) (!UNIX_ANONYMOUS(U) && unix_sk(U)->addr->name->sun_path[0])
#define unix_peer(sk) (unix_sk(sk)->peer)
#define unix_connected(S) ((S)->state == SS_CONNECTED)

static inline void print_unix_addr(struct sockaddr_un *A, int L)
{
	char *buf = (A) ? (char *) &(A)->sun_path : NULL;
	int len = unix_addr_len(L);
	if (!buf || len <= 0)
		printk(" <anonymous>");
	else if (buf[0])
		printk(" %s", buf);
	else
		/* abstract name len includes leading \0 */
		printk(" %d @%.*s", len - 1, len - 1, buf+1);
};

/*
	printk("%s: %s: f %d, t %d, p %d", __FUNCTION__,		\
	       #SK ,							\
*/
#define print_unix_sk(SK)						\
do {									\
	struct unix_sock *u = unix_sk(SK);				\
	printk("%s: f %d, t %d, p %d",	#SK ,				\
	       (SK)->sk_family, (SK)->sk_type, (SK)->sk_protocol);	\
	if (u->addr)							\
		print_unix_addr(u->addr->name, u->addr->len);		\
	else								\
		print_unix_addr(NULL, sizeof(sa_family_t));		\
	/* printk("\n");*/						\
} while (0)

#define print_sk(SK)							\
do {									\
	if (!(SK)) {							\
		printk("%s: %s is null\n", __FUNCTION__, #SK);		\
	} else if ((SK)->sk_family == PF_UNIX) {			\
		print_unix_sk(SK);					\
		printk("\n");						\
	} else {							\
		printk("%s: %s: family %d\n", __FUNCTION__, #SK ,	\
		       (SK)->sk_family);				\
	}								\
} while (0)

#define print_sock_addr(U) \
do {			       \
	printk("%s:\n", __FUNCTION__);					\
	printk("    sock %s:", sock_ctx && sock_ctx->label ? aa_label_printk(sock_ctx->label, GFP_ATOMIC); : "<null>"); print_sk(sock); \
	printk("    other %s:", other_ctx && other_ctx->label ? aa_label_printk(other_ctx->label, GFP_ATOMIC); : "<null>"); print_sk(other); \
	printk("    new %s", new_ctx && new_ctx->label ? aa_label_printk(new_ctx->label, GFP_ATOMIC); : "<null>"); print_sk(newsk); \
} while (0)




int aa_unix_peer_perm(struct aa_label *label, const char *op, u32 request,
		      struct sock *sk, struct sock *peer_sk,
		      struct aa_label *peer_label);
int aa_unix_label_sk_perm(struct aa_label *label, const char *op, u32 request,
			  struct sock *sk);
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
int aa_unix_file_perm(struct aa_label *label, const char *op, u32 request,
		      struct socket *sock);

#endif /* __AA_AF_UNIX_H */
