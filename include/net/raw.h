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


extern struct proto raw_prot;


extern void 	raw_err(struct sock *, struct sk_buff *, u32 info);
extern int 	raw_rcv(struct sock *, struct sk_buff *);

/* Note: v4 ICMP wants to get at this stuff, if you change the
 *       hashing mechanism, make sure you update icmp.c as well.
 */
#define RAWV4_HTABLE_SIZE	MAX_INET_PROTOS
extern struct hlist_head raw_v4_htable[RAWV4_HTABLE_SIZE];

extern rwlock_t raw_v4_lock;


extern struct sock *__raw_v4_lookup(struct sock *sk, unsigned short num,
				    unsigned long raddr, unsigned long laddr,
				    int dif);

extern void raw_v4_input(struct sk_buff *skb, struct iphdr *iph, int hash);

#endif	/* _RAW_H */
