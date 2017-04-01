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
#include <linux/bug.h>
#include <net/inet_sock.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/seq_file.h>
#include <linux/poll.h>

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
#if IS_ENABLED(CONFIG_IPV6)
		struct inet6_skb_parm	h6;
#endif
	} header;
	__u16		cscov;
	__u8		partial_cov;
};
#define UDP_SKB_CB(__skb)	((struct udp_skb_cb *)((__skb)->cb))

/**
 *	struct udp_hslot - UDP hash slot
 *
 *	@head:	head of list of sockets
 *	@count:	number of sockets in 'head' list
 *	@lock:	spinlock protecting changes to head/count
 */
struct udp_hslot {
	struct hlist_head	head;
	int			count;
	spinlock_t		lock;
} __attribute__((aligned(2 * sizeof(long))));

/**
 *	struct udp_table - UDP table
 *
 *	@hash:	hash table, sockets are hashed on (local port)
 *	@hash2:	hash table, sockets are hashed on (local port, local address)
 *	@mask:	number of slots in hash tables, minus 1
 *	@log:	log2(number of slots in hash table)
 */
struct udp_table {
	struct udp_hslot	*hash;
	struct udp_hslot	*hash2;
	unsigned int		mask;
	unsigned int		log;
};
extern struct udp_table udp_table;
void udp_table_init(struct udp_table *, const char *);
static inline struct udp_hslot *udp_hashslot(struct udp_table *table,
					     struct net *net, unsigned int num)
{
	return &table->hash[udp_hashfn(net, num, table->mask)];
}
/*
 * For secondary hash, net_hash_mix() is performed before calling
 * udp_hashslot2(), this explains difference with udp_hashslot()
 */
static inline struct udp_hslot *udp_hashslot2(struct udp_table *table,
					      unsigned int hash)
{
	return &table->hash2[hash & table->mask];
}

extern struct proto udp_prot;

extern atomic_long_t udp_memory_allocated;

/* sysctl variables for udp */
extern long sysctl_udp_mem[3];
extern int sysctl_udp_rmem_min;
extern int sysctl_udp_wmem_min;

struct sk_buff;

/*
 *	Generic checksumming routines for UDP(-Lite) v4 and v6
 */
static inline __sum16 __udp_lib_checksum_complete(struct sk_buff *skb)
{
	return (UDP_SKB_CB(skb)->cscov == skb->len ?
		__skb_checksum_complete(skb) :
		__skb_checksum_complete_head(skb, UDP_SKB_CB(skb)->cscov));
}

static inline int udp_lib_checksum_complete(struct sk_buff *skb)
{
	return !skb_csum_unnecessary(skb) &&
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
	__wsum csum = csum_partial(skb_transport_header(skb),
				   sizeof(struct udphdr), 0);
	skb_queue_walk(&sk->sk_write_queue, skb) {
		csum = csum_add(csum, skb->csum);
	}
	return csum;
}

static inline __wsum udp_csum(struct sk_buff *skb)
{
	__wsum csum = csum_partial(skb_transport_header(skb),
				   sizeof(struct udphdr), skb->csum);

	for (skb = skb_shinfo(skb)->frag_list; skb; skb = skb->next) {
		csum = csum_add(csum, skb->csum);
	}
	return csum;
}

static inline __sum16 udp_v4_check(int len, __be32 saddr,
				   __be32 daddr, __wsum base)
{
	return csum_tcpudp_magic(saddr, daddr, len, IPPROTO_UDP, base);
}

void udp_set_csum(bool nocheck, struct sk_buff *skb,
		  __be32 saddr, __be32 daddr, int len);

static inline void udp_csum_pull_header(struct sk_buff *skb)
{
	if (!skb->csum_valid && skb->ip_summed == CHECKSUM_NONE)
		skb->csum = csum_partial(skb->data, sizeof(struct udphdr),
					 skb->csum);
	skb_pull_rcsum(skb, sizeof(struct udphdr));
	UDP_SKB_CB(skb)->cscov -= sizeof(struct udphdr);
}

typedef struct sock *(*udp_lookup_t)(struct sk_buff *skb, __be16 sport,
				     __be16 dport);

struct sk_buff **udp_gro_receive(struct sk_buff **head, struct sk_buff *skb,
				 struct udphdr *uh, udp_lookup_t lookup);
int udp_gro_complete(struct sk_buff *skb, int nhoff, udp_lookup_t lookup);

static inline struct udphdr *udp_gro_udphdr(struct sk_buff *skb)
{
	struct udphdr *uh;
	unsigned int hlen, off;

	off  = skb_gro_offset(skb);
	hlen = off + sizeof(*uh);
	uh   = skb_gro_header_fast(skb, off);
	if (skb_gro_header_hard(skb, hlen))
		uh = skb_gro_header_slow(skb, hlen, off);

	return uh;
}

/* hash routines shared between UDPv4/6 and UDP-Litev4/6 */
static inline int udp_lib_hash(struct sock *sk)
{
	BUG();
	return 0;
}

void udp_lib_unhash(struct sock *sk);
void udp_lib_rehash(struct sock *sk, u16 new_hash);

static inline void udp_lib_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

int udp_lib_get_port(struct sock *sk, unsigned short snum,
		     unsigned int hash2_nulladdr);

u32 udp_flow_hashrnd(void);

static inline __be16 udp_flow_src_port(struct net *net, struct sk_buff *skb,
				       int min, int max, bool use_eth)
{
	u32 hash;

	if (min >= max) {
		/* Use default range */
		inet_get_local_port_range(net, &min, &max);
	}

	hash = skb_get_hash(skb);
	if (unlikely(!hash)) {
		if (use_eth) {
			/* Can't find a normal hash, caller has indicated an
			 * Ethernet packet so use that to compute a hash.
			 */
			hash = jhash(skb->data, 2 * ETH_ALEN,
				     (__force u32) skb->protocol);
		} else {
			/* Can't derive any sort of hash for the packet, set
			 * to some consistent random value.
			 */
			hash = udp_flow_hashrnd();
		}
	}

	/* Since this is being sent on the wire obfuscate hash a bit
	 * to minimize possbility that any useful information to an
	 * attacker is leaked. Only upper 16 bits are relevant in the
	 * computation for 16 bit port value.
	 */
	hash ^= hash << 16;

	return htons((((u64) hash * (max - min)) >> 32) + min);
}

/* net/ipv4/udp.c */
void udp_destruct_sock(struct sock *sk);
void skb_consume_udp(struct sock *sk, struct sk_buff *skb, int len);
int __udp_enqueue_schedule_skb(struct sock *sk, struct sk_buff *skb);
void udp_skb_destructor(struct sock *sk, struct sk_buff *skb);
static inline struct sk_buff *
__skb_recv_udp(struct sock *sk, unsigned int flags, int noblock, int *peeked,
	       int *off, int *err)
{
	return __skb_recv_datagram(sk, flags | (noblock ? MSG_DONTWAIT : 0),
				   udp_skb_destructor, peeked, off, err);
}
static inline struct sk_buff *skb_recv_udp(struct sock *sk, unsigned int flags,
					   int noblock, int *err)
{
	int peeked, off = 0;

	return __skb_recv_udp(sk, flags, noblock, &peeked, &off, err);
}

void udp_v4_early_demux(struct sk_buff *skb);
int udp_get_port(struct sock *sk, unsigned short snum,
		 int (*saddr_cmp)(const struct sock *,
				  const struct sock *));
void udp_err(struct sk_buff *, u32);
int udp_abort(struct sock *sk, int err);
int udp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len);
int udp_push_pending_frames(struct sock *sk);
void udp_flush_pending_frames(struct sock *sk);
void udp4_hwcsum(struct sk_buff *skb, __be32 src, __be32 dst);
int udp_rcv(struct sk_buff *skb);
int udp_ioctl(struct sock *sk, int cmd, unsigned long arg);
int udp_init_sock(struct sock *sk);
int __udp_disconnect(struct sock *sk, int flags);
int udp_disconnect(struct sock *sk, int flags);
unsigned int udp_poll(struct file *file, struct socket *sock, poll_table *wait);
struct sk_buff *skb_udp_tunnel_segment(struct sk_buff *skb,
				       netdev_features_t features,
				       bool is_ipv6);
int udp_lib_getsockopt(struct sock *sk, int level, int optname,
		       char __user *optval, int __user *optlen);
int udp_lib_setsockopt(struct sock *sk, int level, int optname,
		       char __user *optval, unsigned int optlen,
		       int (*push_pending_frames)(struct sock *));
struct sock *udp4_lib_lookup(struct net *net, __be32 saddr, __be16 sport,
			     __be32 daddr, __be16 dport, int dif);
struct sock *__udp4_lib_lookup(struct net *net, __be32 saddr, __be16 sport,
			       __be32 daddr, __be16 dport, int dif,
			       struct udp_table *tbl, struct sk_buff *skb);
struct sock *udp4_lib_lookup_skb(struct sk_buff *skb,
				 __be16 sport, __be16 dport);
struct sock *udp6_lib_lookup(struct net *net,
			     const struct in6_addr *saddr, __be16 sport,
			     const struct in6_addr *daddr, __be16 dport,
			     int dif);
struct sock *__udp6_lib_lookup(struct net *net,
			       const struct in6_addr *saddr, __be16 sport,
			       const struct in6_addr *daddr, __be16 dport,
			       int dif, struct udp_table *tbl,
			       struct sk_buff *skb);
struct sock *udp6_lib_lookup_skb(struct sk_buff *skb,
				 __be16 sport, __be16 dport);

/*
 * 	SNMP statistics for UDP and UDP-Lite
 */
#define UDP_INC_STATS(net, field, is_udplite)		      do { \
	if (is_udplite) SNMP_INC_STATS((net)->mib.udplite_statistics, field);       \
	else		SNMP_INC_STATS((net)->mib.udp_statistics, field);  }  while(0)
#define __UDP_INC_STATS(net, field, is_udplite) 	      do { \
	if (is_udplite) __SNMP_INC_STATS((net)->mib.udplite_statistics, field);         \
	else		__SNMP_INC_STATS((net)->mib.udp_statistics, field);    }  while(0)

#define __UDP6_INC_STATS(net, field, is_udplite)	    do { \
	if (is_udplite) __SNMP_INC_STATS((net)->mib.udplite_stats_in6, field);\
	else		__SNMP_INC_STATS((net)->mib.udp_stats_in6, field);  \
} while(0)
#define UDP6_INC_STATS(net, field, __lite)		    do { \
	if (__lite) SNMP_INC_STATS((net)->mib.udplite_stats_in6, field);  \
	else	    SNMP_INC_STATS((net)->mib.udp_stats_in6, field);      \
} while(0)

#if IS_ENABLED(CONFIG_IPV6)
#define __UDPX_INC_STATS(sk, field)					\
do {									\
	if ((sk)->sk_family == AF_INET)					\
		__UDP_INC_STATS(sock_net(sk), field, 0);		\
	else								\
		__UDP6_INC_STATS(sock_net(sk), field, 0);		\
} while (0)
#else
#define __UDPX_INC_STATS(sk, field) __UDP_INC_STATS(sock_net(sk), field, 0)
#endif

/* /proc */
int udp_seq_open(struct inode *inode, struct file *file);

struct udp_seq_afinfo {
	char				*name;
	sa_family_t			family;
	struct udp_table		*udp_table;
	const struct file_operations	*seq_fops;
	struct seq_operations		seq_ops;
};

struct udp_iter_state {
	struct seq_net_private  p;
	sa_family_t		family;
	int			bucket;
	struct udp_table	*udp_table;
};

#ifdef CONFIG_PROC_FS
int udp_proc_register(struct net *net, struct udp_seq_afinfo *afinfo);
void udp_proc_unregister(struct net *net, struct udp_seq_afinfo *afinfo);

int udp4_proc_init(void);
void udp4_proc_exit(void);
#endif

int udpv4_offload_init(void);

void udp_init(void);

void udp_encap_enable(void);
#if IS_ENABLED(CONFIG_IPV6)
void udpv6_encap_enable(void);
#endif
#endif	/* _UDP_H */
