/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET  is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the IP router.
 *
 * Version:	@(#)route.h	1.0.4	05/27/93
 *
 * Authors:	Ross Biro
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 * Fixes:
 *		Alan Cox	:	Reformatted. Added ip_rt_local()
 *		Alan Cox	:	Support for TCP parameters.
 *		Alexey Kuznetsov:	Major changes for new routing code.
 *		Mike McLagan    :	Routing by source
 *		Robert Olsson   :	Added rt_cache statistics
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _ROUTE_H
#define _ROUTE_H

#include <net/dst.h>
#include <net/inetpeer.h>
#include <net/flow.h>
#include <net/inet_sock.h>
#include <linux/in_route.h>
#include <linux/rtnetlink.h>
#include <linux/route.h>
#include <linux/ip.h>
#include <linux/cache.h>
#include <linux/security.h>

#ifndef __KERNEL__
#warning This file is not supposed to be used outside of kernel.
#endif

#define RTO_ONLINK	0x01

#define RTO_CONN	0
/* RTO_CONN is not used (being alias for 0), but preserved not to break
 * some modules referring to it. */

#define RT_CONN_FLAGS(sk)   (RT_TOS(inet_sk(sk)->tos) | sock_flag(sk, SOCK_LOCALROUTE))

struct fib_nh;
struct inet_peer;
struct fib_info;
struct rtable {
	struct dst_entry	dst;

	/* Lookup key. */
	__be32			rt_key_dst;
	__be32			rt_key_src;

	int			rt_genid;
	unsigned		rt_flags;
	__u16			rt_type;
	__u8			rt_tos;

	__be32			rt_dst;	/* Path destination	*/
	__be32			rt_src;	/* Path source		*/
	int			rt_route_iif;
	int			rt_iif;
	int			rt_oif;
	__u32			rt_mark;

	/* Info on neighbour */
	__be32			rt_gateway;

	/* Miscellaneous cached information */
	__be32			rt_spec_dst; /* RFC1122 specific destination */
	u32			rt_peer_genid;
	struct inet_peer	*peer; /* long-living peer info */
	struct fib_info		*fi; /* for client ref to shared metrics */
};

static inline bool rt_is_input_route(struct rtable *rt)
{
	return rt->rt_route_iif != 0;
}

static inline bool rt_is_output_route(struct rtable *rt)
{
	return rt->rt_route_iif == 0;
}

struct ip_rt_acct {
	__u32 	o_bytes;
	__u32 	o_packets;
	__u32 	i_bytes;
	__u32 	i_packets;
};

struct rt_cache_stat {
        unsigned int in_hit;
        unsigned int in_slow_tot;
        unsigned int in_slow_mc;
        unsigned int in_no_route;
        unsigned int in_brd;
        unsigned int in_martian_dst;
        unsigned int in_martian_src;
        unsigned int out_hit;
        unsigned int out_slow_tot;
        unsigned int out_slow_mc;
        unsigned int gc_total;
        unsigned int gc_ignored;
        unsigned int gc_goal_miss;
        unsigned int gc_dst_overflow;
        unsigned int in_hlist_search;
        unsigned int out_hlist_search;
};

extern struct ip_rt_acct __percpu *ip_rt_acct;

struct in_device;
extern int		ip_rt_init(void);
extern void		ip_rt_redirect(__be32 old_gw, __be32 dst, __be32 new_gw,
				       __be32 src, struct net_device *dev);
extern void		rt_cache_flush(struct net *net, int how);
extern void		rt_cache_flush_batch(struct net *net);
extern struct rtable *__ip_route_output_key(struct net *, const struct flowi4 *flp);
extern struct rtable *ip_route_output_flow(struct net *, struct flowi4 *flp,
					   struct sock *sk);
extern struct dst_entry *ipv4_blackhole_route(struct net *net, struct dst_entry *dst_orig);

static inline struct rtable *ip_route_output_key(struct net *net, struct flowi4 *flp)
{
	return ip_route_output_flow(net, flp, NULL);
}

static inline struct rtable *ip_route_output(struct net *net, __be32 daddr,
					     __be32 saddr, u8 tos, int oif)
{
	struct flowi4 fl4 = {
		.flowi4_oif = oif,
		.daddr = daddr,
		.saddr = saddr,
		.flowi4_tos = tos,
	};
	return ip_route_output_key(net, &fl4);
}

static inline struct rtable *ip_route_output_ports(struct net *net, struct sock *sk,
						   __be32 daddr, __be32 saddr,
						   __be16 dport, __be16 sport,
						   __u8 proto, __u8 tos, int oif)
{
	struct flowi4 fl4 = {
		.flowi4_oif = oif,
		.flowi4_flags = sk ? inet_sk_flowi_flags(sk) : 0,
		.flowi4_mark = sk ? sk->sk_mark : 0,
		.daddr = daddr,
		.saddr = saddr,
		.flowi4_tos = tos,
		.flowi4_proto = proto,
		.fl4_dport = dport,
		.fl4_sport = sport,
	};
	if (sk)
		security_sk_classify_flow(sk, flowi4_to_flowi(&fl4));
	return ip_route_output_flow(net, &fl4, sk);
}

static inline struct rtable *ip_route_output_gre(struct net *net,
						 __be32 daddr, __be32 saddr,
						 __be32 gre_key, __u8 tos, int oif)
{
	struct flowi4 fl4 = {
		.flowi4_oif = oif,
		.daddr = daddr,
		.saddr = saddr,
		.flowi4_tos = tos,
		.flowi4_proto = IPPROTO_GRE,
		.fl4_gre_key = gre_key,
	};
	return ip_route_output_key(net, &fl4);
}

extern int ip_route_input_common(struct sk_buff *skb, __be32 dst, __be32 src,
				 u8 tos, struct net_device *devin, bool noref);

static inline int ip_route_input(struct sk_buff *skb, __be32 dst, __be32 src,
				 u8 tos, struct net_device *devin)
{
	return ip_route_input_common(skb, dst, src, tos, devin, false);
}

static inline int ip_route_input_noref(struct sk_buff *skb, __be32 dst, __be32 src,
				       u8 tos, struct net_device *devin)
{
	return ip_route_input_common(skb, dst, src, tos, devin, true);
}

extern unsigned short	ip_rt_frag_needed(struct net *net, struct iphdr *iph, unsigned short new_mtu, struct net_device *dev);
extern void		ip_rt_send_redirect(struct sk_buff *skb);

extern unsigned		inet_addr_type(struct net *net, __be32 addr);
extern unsigned		inet_dev_addr_type(struct net *net, const struct net_device *dev, __be32 addr);
extern void		ip_rt_multicast_event(struct in_device *);
extern int		ip_rt_ioctl(struct net *, unsigned int cmd, void __user *arg);
extern void		ip_rt_get_source(u8 *src, struct rtable *rt);
extern int		ip_rt_dump(struct sk_buff *skb,  struct netlink_callback *cb);

struct in_ifaddr;
extern void fib_add_ifaddr(struct in_ifaddr *);
extern void fib_del_ifaddr(struct in_ifaddr *, struct in_ifaddr *);

static inline void ip_rt_put(struct rtable * rt)
{
	if (rt)
		dst_release(&rt->dst);
}

#define IPTOS_RT_MASK	(IPTOS_TOS_MASK & ~3)

extern const __u8 ip_tos2prio[16];

static inline char rt_tos2priority(u8 tos)
{
	return ip_tos2prio[IPTOS_TOS(tos)>>1];
}

static inline struct rtable *ip_route_connect(__be32 dst, __be32 src, u32 tos,
					      int oif, u8 protocol,
					      __be16 sport, __be16 dport,
					      struct sock *sk, bool can_sleep)
{
	struct flowi4 fl4 = {
		.flowi4_oif = oif,
		.flowi4_mark = sk->sk_mark,
		.daddr = dst,
		.saddr = src,
		.flowi4_tos = tos,
		.flowi4_proto = protocol,
		.fl4_sport = sport,
		.fl4_dport = dport,
	};
	struct net *net = sock_net(sk);
	struct rtable *rt;

	if (inet_sk(sk)->transparent)
		fl4.flowi4_flags |= FLOWI_FLAG_ANYSRC;
	if (protocol == IPPROTO_TCP)
		fl4.flowi4_flags |= FLOWI_FLAG_PRECOW_METRICS;
	if (can_sleep)
		fl4.flowi4_flags |= FLOWI_FLAG_CAN_SLEEP;

	if (!dst || !src) {
		rt = __ip_route_output_key(net, &fl4);
		if (IS_ERR(rt))
			return rt;
		fl4.daddr = rt->rt_dst;
		fl4.saddr = rt->rt_src;
		ip_rt_put(rt);
	}
	security_sk_classify_flow(sk, flowi4_to_flowi(&fl4));
	return ip_route_output_flow(net, &fl4, sk);
}

static inline struct rtable *ip_route_newports(struct rtable *rt,
					       u8 protocol, __be16 orig_sport,
					       __be16 orig_dport, __be16 sport,
					       __be16 dport, struct sock *sk)
{
	if (sport != orig_sport || dport != orig_dport) {
		struct flowi4 fl4 = {
			.flowi4_oif = rt->rt_oif,
			.flowi4_mark = rt->rt_mark,
			.daddr = rt->rt_dst,
			.saddr = rt->rt_src,
			.flowi4_tos = rt->rt_tos,
			.flowi4_proto = protocol,
			.fl4_sport = sport,
			.fl4_dport = dport
		};
		if (inet_sk(sk)->transparent)
			fl4.flowi4_flags |= FLOWI_FLAG_ANYSRC;
		if (protocol == IPPROTO_TCP)
			fl4.flowi4_flags |= FLOWI_FLAG_PRECOW_METRICS;
		ip_rt_put(rt);
		security_sk_classify_flow(sk, flowi4_to_flowi(&fl4));
		return ip_route_output_flow(sock_net(sk), &fl4, sk);
	}
	return rt;
}

extern void rt_bind_peer(struct rtable *rt, int create);

static inline struct inet_peer *rt_get_peer(struct rtable *rt)
{
	if (rt->peer)
		return rt->peer;

	rt_bind_peer(rt, 0);
	return rt->peer;
}

static inline int inet_iif(const struct sk_buff *skb)
{
	return skb_rtable(skb)->rt_iif;
}

extern int sysctl_ip_default_ttl;

static inline int ip4_dst_hoplimit(const struct dst_entry *dst)
{
	int hoplimit = dst_metric_raw(dst, RTAX_HOPLIMIT);

	if (hoplimit == 0)
		hoplimit = sysctl_ip_default_ttl;
	return hoplimit;
}

#endif	/* _ROUTE_H */
