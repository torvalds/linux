/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for inet_sock
 *
 * Authors:	Many, reorganised here by
 * 		Arnaldo Carvalho de Melo <acme@mandriva.com>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _INET_SOCK_H
#define _INET_SOCK_H


#include <linux/kmemcheck.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/jhash.h>
#include <linux/netdevice.h>

#include <net/flow.h>
#include <net/sock.h>
#include <net/request_sock.h>
#include <net/netns/hash.h>

/** struct ip_options - IP Options
 *
 * @faddr - Saved first hop address
 * @nexthop - Saved nexthop address in LSRR and SSRR
 * @is_data - Options in __data, rather than skb
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
	unsigned char	__data[0];
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
#if IS_ENABLED(CONFIG_IPV6)
	u16			inet6_rsk_offset;
#endif
	__be16			loc_port;
	__be32			loc_addr;
	__be32			rmt_addr;
	__be16			rmt_port;
	kmemcheck_bitfield_begin(flags);
	u16			snd_wscale : 4,
				rcv_wscale : 4,
				tstamp_ok  : 1,
				sack_ok	   : 1,
				wscale_ok  : 1,
				ecn_ok	   : 1,
				acked	   : 1,
				no_srccheck: 1;
	kmemcheck_bitfield_end(flags);
	u32                     ir_mark;
	struct ip_options_rcu	*opt;
};

static inline struct inet_request_sock *inet_rsk(const struct request_sock *sk)
{
	return (struct inet_request_sock *)sk;
}

static inline u32 inet_request_mark(struct sock *sk, struct sk_buff *skb)
{
	if (!sk->sk_mark && sock_net(sk)->ipv4.sysctl_tcp_fwmark_accept)
		return skb->mark;

	return sk->sk_mark;
}

struct inet_cork {
	unsigned int		flags;
	__be32			addr;
	struct ip_options	*opt;
	unsigned int		fragsize;
	int			length; /* Total length of all frames */
	struct dst_entry	*dst;
	u8			tx_flags;
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
 * @inet_saddr - Sending source
 * @uc_ttl - Unicast TTL
 * @inet_sport - Source port
 * @inet_id - ID counter for DF pkts
 * @tos - TOS
 * @mc_ttl - Multicasting TTL
 * @is_icsk - is this an inet_connection_sock?
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
#define inet_addrpair		sk.__sk_common.skc_addrpair
#define inet_dport		sk.__sk_common.skc_dport
#define inet_num		sk.__sk_common.skc_num
#define inet_portpair		sk.__sk_common.skc_portpair

	__be32			inet_saddr;
	__s16			uc_ttl;
	__u16			cmsg_flags;
	__be16			inet_sport;
	__u16			inet_id;

	struct ip_options_rcu __rcu	*inet_opt;
	int			rx_dst_ifindex;
	__u8			tos;
	__u8			min_ttl;
	__u8			mc_ttl;
	__u8			pmtudisc;
	__u8			recverr:1,
				is_icsk:1,
				freebind:1,
				hdrincl:1,
				mc_loop:1,
				transparent:1,
				mc_all:1,
				nodefrag:1;
	__u8			rcv_tos;
	int			uc_index;
	int			mc_index;
	__be32			mc_addr;
	struct ip_mc_socklist __rcu	*mc_list;
	struct inet_cork_full	cork;
};

#define IPCORK_OPT	1	/* ip-options has been held in ipcork.opt */
#define IPCORK_ALLFRAG	2	/* always fragment (for ipv6 for now) */

static inline struct inet_sock *inet_sk(const struct sock *sk)
{
	return (struct inet_sock *)sk;
}

static inline void __inet_sk_copy_descendant(struct sock *sk_to,
					     const struct sock *sk_from,
					     const int ancestor_size)
{
	memcpy(inet_sk(sk_to) + 1, inet_sk(sk_from) + 1,
	       sk_from->sk_prot->obj_size - ancestor_size);
}
#if !(IS_ENABLED(CONFIG_IPV6))
static inline void inet_sk_copy_descendant(struct sock *sk_to,
					   const struct sock *sk_from)
{
	__inet_sk_copy_descendant(sk_to, sk_from, sizeof(struct inet_sock));
}
#endif

extern int inet_sk_rebuild_header(struct sock *sk);

extern u32 inet_ehash_secret;
extern u32 ipv6_hash_secret;
extern void build_ehash_secret(void);

static inline unsigned int inet_ehashfn(struct net *net,
					const __be32 laddr, const __u16 lport,
					const __be32 faddr, const __be16 fport)
{
	return jhash_3words((__force __u32) laddr,
			    (__force __u32) faddr,
			    ((__u32) lport) << 16 | (__force __u32)fport,
			    inet_ehash_secret + net_hash_mix(net));
}

static inline int inet_sk_ehashfn(const struct sock *sk)
{
	const struct inet_sock *inet = inet_sk(sk);
	const __be32 laddr = inet->inet_rcv_saddr;
	const __u16 lport = inet->inet_num;
	const __be32 faddr = inet->inet_daddr;
	const __be16 fport = inet->inet_dport;
	struct net *net = sock_net(sk);

	return inet_ehashfn(net, laddr, lport, faddr, fport);
}

static inline struct request_sock *inet_reqsk_alloc(struct request_sock_ops *ops)
{
	struct request_sock *req = reqsk_alloc(ops);
	struct inet_request_sock *ireq = inet_rsk(req);

	if (req != NULL) {
		kmemcheck_annotate_bitfield(ireq, flags);
		ireq->opt = NULL;
	}

	return req;
}

static inline __u8 inet_sk_flowi_flags(const struct sock *sk)
{
	__u8 flags = 0;

	if (inet_sk(sk)->transparent || inet_sk(sk)->hdrincl)
		flags |= FLOWI_FLAG_ANYSRC;
	return flags;
}

#endif	/* _INET_SOCK_H */
