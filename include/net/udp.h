/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the UDP module.
 *
 * Version:	@(#)udp.h	1.0.2	05/07/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	: Turned on udp checksums. I don't want to
 *				  chase 'memory corruption' bugs that aren't!
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _UDP_H
#define _UDP_H

#include <linux/list.h>
#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/seq_file.h>

/**
 *	struct udp_skb_cb  -  UDP(-Lite) private variables
 *
 *	@header:      private variables used by IPv4/IPv6
 *	@cscov:       checksum coverage length (UDP-Lite only)
 *	@partial_cov: if set indicates partial csum coverage
 */
struct udp_skb_cb {
	union {
		struct inet_skb_parm	h4;
#if defined(CONFIG_IPV6) || defined (CONFIG_IPV6_MODULE)
		struct inet6_skb_parm	h6;
#endif
	} header;
	__u16		cscov;
	__u8		partial_cov;
};
#define UDP_SKB_CB(__skb)	((struct udp_skb_cb *)((__skb)->cb))

extern struct hlist_head udp_hash[UDP_HTABLE_SIZE];
extern rwlock_t udp_hash_lock;


/* Note: this must match 'valbool' in sock_setsockopt */
#define UDP_CSUM_NOXMIT		1

/* Used by SunRPC/xprt layer. */
#define UDP_CSUM_NORCV		2

/* Default, as per the RFC, is to always do csums. */
#define UDP_CSUM_DEFAULT	0

extern struct proto udp_prot;

struct sk_buff;

/*
 *	Generic checksumming routines for UDP(-Lite) v4 and v6
 */
static inline u16  __udp_lib_checksum_complete(struct sk_buff *skb)
{
	if (! UDP_SKB_CB(skb)->partial_cov)
		return __skb_checksum_complete(skb);
	return  csum_fold(skb_checksum(skb, 0, UDP_SKB_CB(skb)->cscov,
			  skb->csum));
}

static __inline__ int udp_lib_checksum_complete(struct sk_buff *skb)
{
	return skb->ip_summed != CHECKSUM_UNNECESSARY &&
		__udp_lib_checksum_complete(skb);
}

/**
 * 	udp_csum_outgoing  -  compute UDPv4/v6 checksum over fragments
 * 	@sk: 	socket we are writing to
 * 	@skb: 	sk_buff containing the filled-in UDP header
 * 	        (checksum field must be zeroed out)
 */
static inline __wsum udp_csum_outgoing(struct sock *sk, struct sk_buff *skb)
{
	__wsum csum = csum_partial(skb->h.raw, sizeof(struct udphdr), 0);

	skb_queue_walk(&sk->sk_write_queue, skb) {
		csum = csum_add(csum, skb->csum);
	}
	return csum;
}

/* hash routines shared between UDPv4/6 and UDP-Litev4/6 */
static inline void udp_lib_hash(struct sock *sk)
{
	BUG();
}

static inline void udp_lib_unhash(struct sock *sk)
{
	write_lock_bh(&udp_hash_lock);
	if (sk_del_node_init(sk)) {
		inet_sk(sk)->num = 0;
		sock_prot_dec_use(sk->sk_prot);
	}
	write_unlock_bh(&udp_hash_lock);
}

static inline void udp_lib_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}


/* net/ipv4/udp.c */
extern int	udp_get_port(struct sock *sk, unsigned short snum,
			     int (*saddr_cmp)(const struct sock *, const struct sock *));
extern void	udp_err(struct sk_buff *, u32);

extern int	udp_sendmsg(struct kiocb *iocb, struct sock *sk,
			    struct msghdr *msg, size_t len);

extern int	udp_rcv(struct sk_buff *skb);
extern int	udp_ioctl(struct sock *sk, int cmd, unsigned long arg);
extern int	udp_disconnect(struct sock *sk, int flags);
extern unsigned int udp_poll(struct file *file, struct socket *sock,
			     poll_table *wait);

DECLARE_SNMP_STAT(struct udp_mib, udp_statistics);
/*
 * 	SNMP statistics for UDP and UDP-Lite
 */
#define UDP_INC_STATS_USER(field, is_udplite)			       do {   \
	if (is_udplite) SNMP_INC_STATS_USER(udplite_statistics, field);       \
	else		SNMP_INC_STATS_USER(udp_statistics, field);  }  while(0)
#define UDP_INC_STATS_BH(field, is_udplite) 			       do  {  \
	if (is_udplite) SNMP_INC_STATS_BH(udplite_statistics, field);         \
	else		SNMP_INC_STATS_BH(udp_statistics, field);    }  while(0)

/* /proc */
struct udp_seq_afinfo {
	struct module		*owner;
	char			*name;
	sa_family_t		family;
	struct hlist_head	*hashtable;
	int 			(*seq_show) (struct seq_file *m, void *v);
	struct file_operations	*seq_fops;
};

struct udp_iter_state {
	sa_family_t		family;
	struct hlist_head	*hashtable;
	int			bucket;
	struct seq_operations	seq_ops;
};

#ifdef CONFIG_PROC_FS
extern int udp_proc_register(struct udp_seq_afinfo *afinfo);
extern void udp_proc_unregister(struct udp_seq_afinfo *afinfo);

extern int  udp4_proc_init(void);
extern void udp4_proc_exit(void);
#endif
#endif	/* _UDP_H */
