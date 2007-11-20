/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the RAW-IP module.
 *
 * Version:	@(#)raw.h	1.0.2	05/07/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _RAW_H
#define _RAW_H


#include <net/protocol.h>

extern struct proto raw_prot;

void raw_icmp_error(struct sk_buff *, int, u32);
int raw_local_deliver(struct sk_buff *, int);

extern int 	raw_rcv(struct sock *, struct sk_buff *);

#define RAW_HTABLE_SIZE	MAX_INET_PROTOS

struct raw_hashinfo {
	rwlock_t lock;
	struct hlist_head ht[RAW_HTABLE_SIZE];
};

#ifdef CONFIG_PROC_FS
extern int  raw_proc_init(void);
extern void raw_proc_exit(void);
#endif

void raw_hash_sk(struct sock *sk, struct raw_hashinfo *h);
void raw_unhash_sk(struct sock *sk, struct raw_hashinfo *h);

#endif	/* _RAW_H */
