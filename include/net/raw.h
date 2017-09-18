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
#include <linux/icmp.h>

extern struct proto raw_prot;

extern struct raw_hashinfo raw_v4_hashinfo;
struct sock *__raw_v4_lookup(struct net *net, struct sock *sk,
			     unsigned short num, __be32 raddr,
			     __be32 laddr, int dif, int sdif);

int raw_abort(struct sock *sk, int err);
void raw_icmp_error(struct sk_buff *, int, u32);
int raw_local_deliver(struct sk_buff *, int);

int raw_rcv(struct sock *, struct sk_buff *);

#define RAW_HTABLE_SIZE	MAX_INET_PROTOS

struct raw_hashinfo {
	rwlock_t lock;
	struct hlist_head ht[RAW_HTABLE_SIZE];
};

#ifdef CONFIG_PROC_FS
int raw_proc_init(void);
void raw_proc_exit(void);

struct raw_iter_state {
	struct seq_net_private p;
	int bucket;
	struct raw_hashinfo *h;
};

static inline struct raw_iter_state *raw_seq_private(struct seq_file *seq)
{
	return seq->private;
}
void *raw_seq_start(struct seq_file *seq, loff_t *pos);
void *raw_seq_next(struct seq_file *seq, void *v, loff_t *pos);
void raw_seq_stop(struct seq_file *seq, void *v);
int raw_seq_open(struct inode *ino, struct file *file,
		 struct raw_hashinfo *h, const struct seq_operations *ops);

#endif

int raw_hash_sk(struct sock *sk);
void raw_unhash_sk(struct sock *sk);

struct raw_sock {
	/* inet_sock has to be the first member */
	struct inet_sock   inet;
	struct icmp_filter filter;
	u32		   ipmr_table;
};

static inline struct raw_sock *raw_sk(const struct sock *sk)
{
	return (struct raw_sock *)sk;
}

#endif	/* _RAW_H */
