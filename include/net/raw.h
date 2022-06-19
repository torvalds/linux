/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */
#ifndef _RAW_H
#define _RAW_H

#include <net/inet_sock.h>
#include <net/protocol.h>
#include <linux/icmp.h>

extern struct proto raw_prot;

extern struct raw_hashinfo raw_v4_hashinfo;
bool raw_v4_match(struct net *net, struct sock *sk, unsigned short num,
		  __be32 raddr, __be32 laddr, int dif, int sdif);

int raw_abort(struct sock *sk, int err);
void raw_icmp_error(struct sk_buff *, int, u32);
int raw_local_deliver(struct sk_buff *, int);

int raw_rcv(struct sock *, struct sk_buff *);

#define RAW_HTABLE_SIZE	MAX_INET_PROTOS

struct raw_hashinfo {
	rwlock_t lock;
	struct hlist_nulls_head ht[RAW_HTABLE_SIZE];
};

static inline void raw_hashinfo_init(struct raw_hashinfo *hashinfo)
{
	int i;

	rwlock_init(&hashinfo->lock);
	for (i = 0; i < RAW_HTABLE_SIZE; i++)
		INIT_HLIST_NULLS_HEAD(&hashinfo->ht[i], i);
}

#ifdef CONFIG_PROC_FS
int raw_proc_init(void);
void raw_proc_exit(void);

struct raw_iter_state {
	struct seq_net_private p;
	int bucket;
};

static inline struct raw_iter_state *raw_seq_private(struct seq_file *seq)
{
	return seq->private;
}
void *raw_seq_start(struct seq_file *seq, loff_t *pos);
void *raw_seq_next(struct seq_file *seq, void *v, loff_t *pos);
void raw_seq_stop(struct seq_file *seq, void *v);
#endif

int raw_hash_sk(struct sock *sk);
void raw_unhash_sk(struct sock *sk);
void raw_init(void);

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

static inline bool raw_sk_bound_dev_eq(struct net *net, int bound_dev_if,
				       int dif, int sdif)
{
#if IS_ENABLED(CONFIG_NET_L3_MASTER_DEV)
	return inet_bound_dev_eq(!!net->ipv4.sysctl_raw_l3mdev_accept,
				 bound_dev_if, dif, sdif);
#else
	return inet_bound_dev_eq(true, bound_dev_if, dif, sdif);
#endif
}

#endif	/* _RAW_H */
