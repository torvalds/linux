/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for inet_sock
 *
 * Authors:	Many, reorganised here by
 * 		Arnaldo Carvalho de Melo <acme@mandriva.com>
 */
#ifndef _INET_SOCK_H
#define _INET_SOCK_H

#include <linux/bitops.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/jhash.h>
#include <linux/netdevice.h>

#include <net/flow.h>
#include <net/sock.h>
#include <net/request_sock.h>
#include <net/netns/hash.h>
#include <net/tcp_states.h>
#include <net/l3mdev.h>

/** struct ip_options - IP Options
 *
 * @faddr - Saved first hop address
 * @nexthop - Saved nexthop address in LSRR and SSRR
 * @is_strictroute - Strict source route
 * @srr_is_hit - Packet destination addr was our one
 * @is_changed - IP checksum more not valid
 * @rr_needaddr - Need to record addr of outgoing dev
 * @ts_needtime - Need to record timestamp
 * @ts_needaddr - Need to record addr of outgoing dev
 */
struct ip_options {
	__be32		faddr;
	__be32		nexthop;
	unsigned char	optlen;
	unsigned char	srr;
	unsigned char	rr;
	unsigned char	ts;
	unsigned char	is_strictroute:1,
			srr_is_hit:1,
			is_changed:1,
			rr_needaddr:1,
			ts_needtime:1,
			ts_needaddr:1;
	unsigned char	router_alert;
	unsigned char	cipso;
	unsigned char	__pad2;
	unsigned char	__data[];
};

struct ip_options_rcu {
	struct rcu_head rcu;
	struct ip_options opt;
};

struct ip_options_data {
	struct ip_options_rcu	opt;
	char			data[40];
};

struct inet_request_sock {
	struct request_sock	req;
#define ir_loc_addr		req.__req_common.skc_rcv_saddr
#define ir_rmt_addr		req.__req_common.skc_daddr
#define ir_num			req.__req_common.skc_num
#define ir_rmt_port		req.__req_common.skc_dport
#define ir_v6_rmt_addr		req.__req_common.skc_v6_daddr
#define ir_v6_loc_addr		req.__req_common.skc_v6_rcv_saddr
#define ir_iif			req.__req_common.skc_bound_dev_if
#define ir_cookie		req.__req_common.skc_cookie
#define ireq_net		req.__req_common.skc_net
#define ireq_state		req.__req_common.skc_state
#define ireq_family		req.__req_common.skc_family

	u16			snd_wscale : 4,
				rcv_wscale : 4,
				tstamp_ok  : 1,
				sack_ok	   : 1,
				wscale_ok  : 1,
				ecn_ok	   : 1,
				acked	   : 1,
				no_srccheck: 1,
				smc_ok	   : 1;
	u32                     ir_mark;
	union {
		struct ip_options_rcu __rcu	*ireq_opt;
#if IS_ENABLED(CONFIG_IPV6)
		struct {
			struct ipv6_txoptions	*ipv6_opt;
			struct sk_buff		*pktopts;
		};
#endif
	};
};

static inline struct inet_request_sock *inet_rsk(const struct request_sock *sk)
{
	return (struct inet_request_sock *)sk;
}

static inline u32 inet_request_mark(const struct sock *sk, struct sk_buff *skb)
{
	u32 mark = READ_ONCE(sk->sk_mark);

	if (!mark && READ_ONCE(sock_net(sk)->ipv4.sysctl_tcp_fwmark_accept))
		return skb->mark;

	return mark;
}

static inline int inet_request_bound_dev_if(const struct sock *sk,
					    struct sk_buff *skb)
{
	int bound_dev_if = READ_ONCE(sk->sk_bound_dev_if);
#ifdef CONFIG_NET_L3_MASTER_DEV
	struct net *net = sock_net(sk);

	if (!bound_dev_if && READ_ONCE(net->ipv4.sysctl_tcp_l3mdev_accept))
		return l3mdev_master_ifindex_by_index(net, skb->skb_iif);
#endif

	return bound_dev_if;
}

static inline int inet_sk_bound_l3mdev(const struct sock *sk)
{
#ifdef CONFIG_NET_L3_MASTER_DEV
	struct net *net = sock_net(sk);

	if (!READ_ONCE(net->ipv4.sysctl_tcp_l3mdev_accept))
		return l3mdev_master_ifindex_by_index(net,
						      sk->sk_bound_dev_if);
#endif

	return 0;
}

static inline bool inet_bound_dev_eq(bool l3mdev_accept, int bound_dev_if,
				     int dif, int sdif)
{
	if (!bound_dev_if)
		return !sdif || l3mdev_accept;
	return bound_dev_if == dif || bound_dev_if == sdif;
}

static inline bool inet_sk_bound_dev_eq(struct net *net, int bound_dev_if,
					int dif, int sdif)
{
#if IS_ENABLED(CONFIG_NET_L3_MASTER_DEV)
	return inet_bound_dev_eq(!!READ_ONCE(net->ipv4.sysctl_tcp_l3mdev_accept),
				 bound_dev_if, dif, sdif);
#else
	return inet_bound_dev_eq(true, bound_dev_if, dif, sdif);
#endif
}

struct inet_cork {
	unsigned int		flags;
	__be32			addr;
	struct ip_options	*opt;
	unsigned int		fragsize;
	int			length; /* Total length of all frames */
	struct dst_entry	*dst;
	u8			tx_flags;
	__u8			ttl;
	__s16			tos;
	char			priority;
	__u16			gso_size;
	u64			transmit_time;
	u32			mark;
};

struct inet_cork_full {
	struct inet_cork	base;
	struct flowi		fl;
};

struct ip_mc_socklist;
struct ipv6_pinfo;
struct rtable;

/** struct inet_sock - representation of INET sockets
 *
 * @sk - ancestor class
 * @pinet6 - pointer to IPv6 control block
 * @inet_daddr - Foreign IPv4 addr
 * @inet_rcv_saddr - Bound local IPv4 addr
 * @inet_dport - Destination port
 * @inet_num - Local port
 * @inet_flags - various atomic flags
 * @inet_saddr - Sending source
 * @uc_ttl - Unicast TTL
 * @inet_sport - Source port
 * @inet_id - ID counter for DF pkts
 * @tos - TOS
 * @mc_ttl - Multicasting TTL
 * @uc_index - Unicast outgoing device index
 * @mc_index - Multicast device index
 * @mc_list - Group array
 * @cork - info to build ip hdr on each ip frag while socket is corked
 */
struct inet_sock {
	/* sk and pinet6 has to be the first two members of inet_sock */
	struct sock		sk;
#if IS_ENABLED(CONFIG_IPV6)
	struct ipv6_pinfo	*pinet6;
#endif
	/* Socket demultiplex comparisons on incoming packets. */
#define inet_daddr		sk.__sk_common.skc_daddr
#define inet_rcv_saddr		sk.__sk_common.skc_rcv_saddr
#define inet_dport		sk.__sk_common.skc_dport
#define inet_num		sk.__sk_common.skc_num

	unsigned long		inet_flags;
	__be32			inet_saddr;
	__s16			uc_ttl;
	__be16			inet_sport;
	struct ip_options_rcu __rcu	*inet_opt;
	atomic_t		inet_id;

	__u8			tos;
	__u8			min_ttl;
	__u8			mc_ttl;
	__u8			pmtudisc;
	__u8			rcv_tos;
	__u8			convert_csum;
	int			uc_index;
	int			mc_index;
	__be32			mc_addr;
	u32			local_port_range;	/* high << 16 | low */

	struct ip_mc_socklist __rcu	*mc_list;
	struct inet_cork_full	cork;
};

#define IPCORK_OPT	1	/* ip-options has been held in ipcork.opt */

enum {
	INET_FLAGS_PKTINFO	= 0,
	INET_FLAGS_TTL		= 1,
	INET_FLAGS_TOS		= 2,
	INET_FLAGS_RECVOPTS	= 3,
	INET_FLAGS_RETOPTS	= 4,
	INET_FLAGS_PASSSEC	= 5,
	INET_FLAGS_ORIGDSTADDR	= 6,
	INET_FLAGS_CHECKSUM	= 7,
	INET_FLAGS_RECVFRAGSIZE	= 8,

	INET_FLAGS_RECVERR	= 9,
	INET_FLAGS_RECVERR_RFC4884 = 10,
	INET_FLAGS_FREEBIND	= 11,
	INET_FLAGS_HDRINCL	= 12,
	INET_FLAGS_MC_LOOP	= 13,
	INET_FLAGS_MC_ALL	= 14,
	INET_FLAGS_TRANSPARENT	= 15,
	INET_FLAGS_IS_ICSK	= 16,
	INET_FLAGS_NODEFRAG	= 17,
	INET_FLAGS_BIND_ADDRESS_NO_PORT = 18,
	INET_FLAGS_DEFER_CONNECT = 19,
	INET_FLAGS_MC6_LOOP	= 20,
	INET_FLAGS_RECVERR6_RFC4884 = 21,
	INET_FLAGS_MC6_ALL	= 22,
	INET_FLAGS_AUTOFLOWLABEL_SET = 23,
	INET_FLAGS_AUTOFLOWLABEL = 24,
	INET_FLAGS_DONTFRAG	= 25,
	INET_FLAGS_RECVERR6	= 26,
	INET_FLAGS_REPFLOW	= 27,
	INET_FLAGS_RTALERT_ISOLATE = 28,
	INET_FLAGS_SNDFLOW	= 29,
};

/* cmsg flags for inet */
#define IP_CMSG_PKTINFO		BIT(INET_FLAGS_PKTINFO)
#define IP_CMSG_TTL		BIT(INET_FLAGS_TTL)
#define IP_CMSG_TOS		BIT(INET_FLAGS_TOS)
#define IP_CMSG_RECVOPTS	BIT(INET_FLAGS_RECVOPTS)
#define IP_CMSG_RETOPTS		BIT(INET_FLAGS_RETOPTS)
#define IP_CMSG_PASSSEC		BIT(INET_FLAGS_PASSSEC)
#define IP_CMSG_ORIGDSTADDR	BIT(INET_FLAGS_ORIGDSTADDR)
#define IP_CMSG_CHECKSUM	BIT(INET_FLAGS_CHECKSUM)
#define IP_CMSG_RECVFRAGSIZE	BIT(INET_FLAGS_RECVFRAGSIZE)

#define IP_CMSG_ALL	(IP_CMSG_PKTINFO | IP_CMSG_TTL |		\
			 IP_CMSG_TOS | IP_CMSG_RECVOPTS |		\
			 IP_CMSG_RETOPTS | IP_CMSG_PASSSEC |		\
			 IP_CMSG_ORIGDSTADDR | IP_CMSG_CHECKSUM |	\
			 IP_CMSG_RECVFRAGSIZE)

static inline unsigned long inet_cmsg_flags(const struct inet_sock *inet)
{
	return READ_ONCE(inet->inet_flags) & IP_CMSG_ALL;
}

#define inet_test_bit(nr, sk)			\
	test_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags)
#define inet_set_bit(nr, sk)			\
	set_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags)
#define inet_clear_bit(nr, sk)			\
	clear_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags)
#define inet_assign_bit(nr, sk, val)		\
	assign_bit(INET_FLAGS_##nr, &inet_sk(sk)->inet_flags, val)

static inline bool sk_is_inet(struct sock *sk)
{
	return sk->sk_family == AF_INET || sk->sk_family == AF_INET6;
}

/**
 * sk_to_full_sk - Access to a full socket
 * @sk: pointer to a socket
 *
 * SYNACK messages might be attached to request sockets.
 * Some places want to reach the listener in this case.
 */
static inline struct sock *sk_to_full_sk(struct sock *sk)
{
#ifdef CONFIG_INET
	if (sk && sk->sk_state == TCP_NEW_SYN_RECV)
		sk = inet_reqsk(sk)->rsk_listener;
#endif
	return sk;
}

/* sk_to_full_sk() variant with a const argument */
static inline const struct sock *sk_const_to_full_sk(const struct sock *sk)
{
#ifdef CONFIG_INET
	if (sk && sk->sk_state == TCP_NEW_SYN_RECV)
		sk = ((const struct request_sock *)sk)->rsk_listener;
#endif
	return sk;
}

static inline struct sock *skb_to_full_sk(const struct sk_buff *skb)
{
	return sk_to_full_sk(skb->sk);
}

#define inet_sk(ptr) container_of_const(ptr, struct inet_sock, sk)

static inline void __inet_sk_copy_descendant(struct sock *sk_to,
					     const struct sock *sk_from,
					     const int ancestor_size)
{
	memcpy(inet_sk(sk_to) + 1, inet_sk(sk_from) + 1,
	       sk_from->sk_prot->obj_size - ancestor_size);
}

int inet_sk_rebuild_header(struct sock *sk);

/**
 * inet_sk_state_load - read sk->sk_state for lockless contexts
 * @sk: socket pointer
 *
 * Paired with inet_sk_state_store(). Used in places we don't hold socket lock:
 * tcp_diag_get_info(), tcp_get_info(), tcp_poll(), get_tcp4_sock() ...
 */
static inline int inet_sk_state_load(const struct sock *sk)
{
	/* state change might impact lockless readers. */
	return smp_load_acquire(&sk->sk_state);
}

/**
 * inet_sk_state_store - update sk->sk_state
 * @sk: socket pointer
 * @newstate: new state
 *
 * Paired with inet_sk_state_load(). Should be used in contexts where
 * state change might impact lockless readers.
 */
void inet_sk_state_store(struct sock *sk, int newstate);

void inet_sk_set_state(struct sock *sk, int state);

static inline unsigned int __inet_ehashfn(const __be32 laddr,
					  const __u16 lport,
					  const __be32 faddr,
					  const __be16 fport,
					  u32 initval)
{
	return jhash_3words((__force __u32) laddr,
			    (__force __u32) faddr,
			    ((__u32) lport) << 16 | (__force __u32)fport,
			    initval);
}

struct request_sock *inet_reqsk_alloc(const struct request_sock_ops *ops,
				      struct sock *sk_listener,
				      bool attach_listener);

static inline __u8 inet_sk_flowi_flags(const struct sock *sk)
{
	__u8 flags = 0;

	if (inet_test_bit(TRANSPARENT, sk) || inet_test_bit(HDRINCL, sk))
		flags |= FLOWI_FLAG_ANYSRC;
	return flags;
}

static inline void inet_inc_convert_csum(struct sock *sk)
{
	inet_sk(sk)->convert_csum++;
}

static inline void inet_dec_convert_csum(struct sock *sk)
{
	if (inet_sk(sk)->convert_csum > 0)
		inet_sk(sk)->convert_csum--;
}

static inline bool inet_get_convert_csum(struct sock *sk)
{
	return !!inet_sk(sk)->convert_csum;
}


static inline bool inet_can_nonlocal_bind(struct net *net,
					  struct inet_sock *inet)
{
	return READ_ONCE(net->ipv4.sysctl_ip_nonlocal_bind) ||
		test_bit(INET_FLAGS_FREEBIND, &inet->inet_flags) ||
		test_bit(INET_FLAGS_TRANSPARENT, &inet->inet_flags);
}

static inline bool inet_addr_valid_or_nonlocal(struct net *net,
					       struct inet_sock *inet,
					       __be32 addr,
					       int addr_type)
{
	return inet_can_nonlocal_bind(net, inet) ||
		addr == htonl(INADDR_ANY) ||
		addr_type == RTN_LOCAL ||
		addr_type == RTN_MULTICAST ||
		addr_type == RTN_BROADCAST;
}

#endif	/* _INET_SOCK_H */
