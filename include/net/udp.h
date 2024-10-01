/* SPDX-License-Identifier: GPL-2.0-or-later */
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
 */
#ifndef _UDP_H
#define _UDP_H

#include <linux/list.h>
#include <linux/bug.h>
#include <net/inet_sock.h>
#include <net/gso.h>
#include <net/sock.h>
#include <net/snmp.h>
#include <net/ip.h>
#include <linux/ipv6.h>
#include <linux/seq_file.h>
#include <linux/poll.h>
#include <linux/indirect_call_wrapper.h>

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
					     const struct net *net,
					     unsigned int num)
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
DECLARE_PER_CPU(int, udp_memory_per_cpu_fw_alloc);

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

typedef struct sock *(*udp_lookup_t)(const struct sk_buff *skb, __be16 sport,
				     __be16 dport);

void udp_v6_early_demux(struct sk_buff *skb);
INDIRECT_CALLABLE_DECLARE(int udpv6_rcv(struct sk_buff *));

struct sk_buff *__udp_gso_segment(struct sk_buff *gso_skb,
				  netdev_features_t features, bool is_ipv6);

static inline void udp_lib_init_sock(struct sock *sk)
{
	struct udp_sock *up = udp_sk(sk);

	skb_queue_head_init(&up->reader_queue);
	up->forward_threshold = sk->sk_rcvbuf >> 2;
	set_bit(SOCK_CUSTOM_SOCKOPT, &sk->sk_socket->flags);
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
	 * to minimize possibility that any useful information to an
	 * attacker is leaked. Only upper 16 bits are relevant in the
	 * computation for 16 bit port value.
	 */
	hash ^= hash << 16;

	return htons((((u64) hash * (max - min)) >> 32) + min);
}

static inline int udp_rqueue_get(struct sock *sk)
{
	return sk_rmem_alloc_get(sk) - READ_ONCE(udp_sk(sk)->forward_deficit);
}

static inline bool udp_sk_bound_dev_eq(const struct net *net, int bound_dev_if,
				       int dif, int sdif)
{
#if IS_ENABLED(CONFIG_NET_L3_MASTER_DEV)
	return inet_bound_dev_eq(!!READ_ONCE(net->ipv4.sysctl_udp_l3mdev_accept),
				 bound_dev_if, dif, sdif);
#else
	return inet_bound_dev_eq(true, bound_dev_if, dif, sdif);
#endif
}

/* net/ipv4/udp.c */
void udp_destruct_common(struct sock *sk);
void skb_consume_udp(struct sock *sk, struct sk_buff *skb, int len);
int __udp_enqueue_schedule_skb(struct sock *sk, struct sk_buff *skb);
void udp_skb_destructor(struct sock *sk, struct sk_buff *skb);
struct sk_buff *__skb_recv_udp(struct sock *sk, unsigned int flags, int *off,
			       int *err);
static inline struct sk_buff *skb_recv_udp(struct sock *sk, unsigned int flags,
					   int *err)
{
	int off = 0;

	return __skb_recv_udp(sk, flags, &off, err);
}

int udp_v4_early_demux(struct sk_buff *skb);
bool udp_sk_rx_dst_set(struct sock *sk, struct dst_entry *dst);
int udp_err(struct sk_buff *, u32);
int udp_abort(struct sock *sk, int err);
int udp_sendmsg(struct sock *sk, struct msghdr *msg, size_t len);
void udp_splice_eof(struct socket *sock);
int udp_push_pending_frames(struct sock *sk);
void udp_flush_pending_frames(struct sock *sk);
int udp_cmsg_send(struct sock *sk, struct msghdr *msg, u16 *gso_size);
void udp4_hwcsum(struct sk_buff *skb, __be32 src, __be32 dst);
int udp_rcv(struct sk_buff *skb);
int udp_ioctl(struct sock *sk, int cmd, int *karg);
int udp_init_sock(struct sock *sk);
int udp_pre_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len);
int __udp_disconnect(struct sock *sk, int flags);
int udp_disconnect(struct sock *sk, int flags);
__poll_t udp_poll(struct file *file, struct socket *sock, poll_table *wait);
struct sk_buff *skb_udp_tunnel_segment(struct sk_buff *skb,
				       netdev_features_t features,
				       bool is_ipv6);
int udp_lib_getsockopt(struct sock *sk, int level, int optname,
		       char __user *optval, int __user *optlen);
int udp_lib_setsockopt(struct sock *sk, int level, int optname,
		       sockptr_t optval, unsigned int optlen,
		       int (*push_pending_frames)(struct sock *));
struct sock *udp4_lib_lookup(const struct net *net, __be32 saddr, __be16 sport,
			     __be32 daddr, __be16 dport, int dif);
struct sock *__udp4_lib_lookup(const struct net *net, __be32 saddr,
			       __be16 sport,
			       __be32 daddr, __be16 dport, int dif, int sdif,
			       struct udp_table *tbl, struct sk_buff *skb);
struct sock *udp4_lib_lookup_skb(const struct sk_buff *skb,
				 __be16 sport, __be16 dport);
struct sock *udp6_lib_lookup(const struct net *net,
			     const struct in6_addr *saddr, __be16 sport,
			     const struct in6_addr *daddr, __be16 dport,
			     int dif);
struct sock *__udp6_lib_lookup(const struct net *net,
			       const struct in6_addr *saddr, __be16 sport,
			       const struct in6_addr *daddr, __be16 dport,
			       int dif, int sdif, struct udp_table *tbl,
			       struct sk_buff *skb);
struct sock *udp6_lib_lookup_skb(const struct sk_buff *skb,
				 __be16 sport, __be16 dport);
int udp_read_skb(struct sock *sk, skb_read_actor_t recv_actor);

/* UDP uses skb->dev_scratch to cache as much information as possible and avoid
 * possibly multiple cache miss on dequeue()
 */
struct udp_dev_scratch {
	/* skb->truesize and the stateless bit are embedded in a single field;
	 * do not use a bitfield since the compiler emits better/smaller code
	 * this way
	 */
	u32 _tsize_state;

#if BITS_PER_LONG == 64
	/* len and the bit needed to compute skb_csum_unnecessary
	 * will be on cold cache lines at recvmsg time.
	 * skb->len can be stored on 16 bits since the udp header has been
	 * already validated and pulled.
	 */
	u16 len;
	bool is_linear;
	bool csum_unnecessary;
#endif
};

static inline struct udp_dev_scratch *udp_skb_scratch(struct sk_buff *skb)
{
	return (struct udp_dev_scratch *)&skb->dev_scratch;
}

#if BITS_PER_LONG == 64
static inline unsigned int udp_skb_len(struct sk_buff *skb)
{
	return udp_skb_scratch(skb)->len;
}

static inline bool udp_skb_csum_unnecessary(struct sk_buff *skb)
{
	return udp_skb_scratch(skb)->csum_unnecessary;
}

static inline bool udp_skb_is_linear(struct sk_buff *skb)
{
	return udp_skb_scratch(skb)->is_linear;
}

#else
static inline unsigned int udp_skb_len(struct sk_buff *skb)
{
	return skb->len;
}

static inline bool udp_skb_csum_unnecessary(struct sk_buff *skb)
{
	return skb_csum_unnecessary(skb);
}

static inline bool udp_skb_is_linear(struct sk_buff *skb)
{
	return !skb_is_nonlinear(skb);
}
#endif

static inline int copy_linear_skb(struct sk_buff *skb, int len, int off,
				  struct iov_iter *to)
{
	return copy_to_iter_full(skb->data + off, len, to) ? 0 : -EFAULT;
}

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
#define __UDPX_MIB(sk, ipv4)						\
({									\
	ipv4 ? (IS_UDPLITE(sk) ? sock_net(sk)->mib.udplite_statistics :	\
				 sock_net(sk)->mib.udp_statistics) :	\
		(IS_UDPLITE(sk) ? sock_net(sk)->mib.udplite_stats_in6 :	\
				 sock_net(sk)->mib.udp_stats_in6);	\
})
#else
#define __UDPX_MIB(sk, ipv4)						\
({									\
	IS_UDPLITE(sk) ? sock_net(sk)->mib.udplite_statistics :		\
			 sock_net(sk)->mib.udp_statistics;		\
})
#endif

#define __UDPX_INC_STATS(sk, field) \
	__SNMP_INC_STATS(__UDPX_MIB(sk, (sk)->sk_family == AF_INET), field)

#ifdef CONFIG_PROC_FS
struct udp_seq_afinfo {
	sa_family_t			family;
	struct udp_table		*udp_table;
};

struct udp_iter_state {
	struct seq_net_private  p;
	int			bucket;
};

void *udp_seq_start(struct seq_file *seq, loff_t *pos);
void *udp_seq_next(struct seq_file *seq, void *v, loff_t *pos);
void udp_seq_stop(struct seq_file *seq, void *v);

extern const struct seq_operations udp_seq_ops;
extern const struct seq_operations udp6_seq_ops;

int udp4_proc_init(void);
void udp4_proc_exit(void);
#endif /* CONFIG_PROC_FS */

int udpv4_offload_init(void);

void udp_init(void);

DECLARE_STATIC_KEY_FALSE(udp_encap_needed_key);
void udp_encap_enable(void);
void udp_encap_disable(void);
#if IS_ENABLED(CONFIG_IPV6)
DECLARE_STATIC_KEY_FALSE(udpv6_encap_needed_key);
void udpv6_encap_enable(void);
#endif

static inline struct sk_buff *udp_rcv_segment(struct sock *sk,
					      struct sk_buff *skb, bool ipv4)
{
	netdev_features_t features = NETIF_F_SG;
	struct sk_buff *segs;

	/* Avoid csum recalculation by skb_segment unless userspace explicitly
	 * asks for the final checksum values
	 */
	if (!inet_get_convert_csum(sk))
		features |= NETIF_F_IP_CSUM | NETIF_F_IPV6_CSUM;

	/* UDP segmentation expects packets of type CHECKSUM_PARTIAL or
	 * CHECKSUM_NONE in __udp_gso_segment. UDP GRO indeed builds partial
	 * packets in udp_gro_complete_segment. As does UDP GSO, verified by
	 * udp_send_skb. But when those packets are looped in dev_loopback_xmit
	 * their ip_summed CHECKSUM_NONE is changed to CHECKSUM_UNNECESSARY.
	 * Reset in this specific case, where PARTIAL is both correct and
	 * required.
	 */
	if (skb->pkt_type == PACKET_LOOPBACK)
		skb->ip_summed = CHECKSUM_PARTIAL;

	/* the GSO CB lays after the UDP one, no need to save and restore any
	 * CB fragment
	 */
	segs = __skb_gso_segment(skb, features, false);
	if (IS_ERR_OR_NULL(segs)) {
		int segs_nr = skb_shinfo(skb)->gso_segs;

		atomic_add(segs_nr, &sk->sk_drops);
		SNMP_ADD_STATS(__UDPX_MIB(sk, ipv4), UDP_MIB_INERRORS, segs_nr);
		kfree_skb(skb);
		return NULL;
	}

	consume_skb(skb);
	return segs;
}

static inline void udp_post_segment_fix_csum(struct sk_buff *skb)
{
	/* UDP-lite can't land here - no GRO */
	WARN_ON_ONCE(UDP_SKB_CB(skb)->partial_cov);

	/* UDP packets generated with UDP_SEGMENT and traversing:
	 *
	 * UDP tunnel(xmit) -> veth (segmentation) -> veth (gro) -> UDP tunnel (rx)
	 *
	 * can reach an UDP socket with CHECKSUM_NONE, because
	 * __iptunnel_pull_header() converts CHECKSUM_PARTIAL into NONE.
	 * SKB_GSO_UDP_L4 or SKB_GSO_FRAGLIST packets with no UDP tunnel will
	 * have a valid checksum, as the GRO engine validates the UDP csum
	 * before the aggregation and nobody strips such info in between.
	 * Instead of adding another check in the tunnel fastpath, we can force
	 * a valid csum after the segmentation.
	 * Additionally fixup the UDP CB.
	 */
	UDP_SKB_CB(skb)->cscov = skb->len;
	if (skb->ip_summed == CHECKSUM_NONE && !skb->csum_valid)
		skb->csum_valid = 1;
}

#ifdef CONFIG_BPF_SYSCALL
struct sk_psock;
int udp_bpf_update_proto(struct sock *sk, struct sk_psock *psock, bool restore);
#endif

#endif	/* _UDP_H */
