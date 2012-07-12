#ifndef _NET_IP6_ROUTE_H
#define _NET_IP6_ROUTE_H

#define IP6_RT_PRIO_USER	1024
#define IP6_RT_PRIO_ADDRCONF	256

struct route_info {
	__u8			type;
	__u8			length;
	__u8			prefix_len;
#if defined(__BIG_ENDIAN_BITFIELD)
	__u8			reserved_h:3,
				route_pref:2,
				reserved_l:3;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			reserved_l:3,
				route_pref:2,
				reserved_h:3;
#endif
	__be32			lifetime;
	__u8			prefix[0];	/* 0,8 or 16 */
};

#include <net/flow.h>
#include <net/ip6_fib.h>
#include <net/sock.h>
#include <linux/ip.h>
#include <linux/ipv6.h>

#define RT6_LOOKUP_F_IFACE		0x00000001
#define RT6_LOOKUP_F_REACHABLE		0x00000002
#define RT6_LOOKUP_F_HAS_SADDR		0x00000004
#define RT6_LOOKUP_F_SRCPREF_TMP	0x00000008
#define RT6_LOOKUP_F_SRCPREF_PUBLIC	0x00000010
#define RT6_LOOKUP_F_SRCPREF_COA	0x00000020

/*
 * rt6_srcprefs2flags() and rt6_flags2srcprefs() translate
 * between IPV6_ADDR_PREFERENCES socket option values
 *	IPV6_PREFER_SRC_TMP    = 0x1
 *	IPV6_PREFER_SRC_PUBLIC = 0x2
 *	IPV6_PREFER_SRC_COA    = 0x4
 * and above RT6_LOOKUP_F_SRCPREF_xxx flags.
 */
static inline int rt6_srcprefs2flags(unsigned int srcprefs)
{
	/* No need to bitmask because srcprefs have only 3 bits. */
	return srcprefs << 3;
}

static inline unsigned int rt6_flags2srcprefs(int flags)
{
	return (flags >> 3) & 7;
}

extern void rt6_bind_peer(struct rt6_info *rt, int create);

static inline struct inet_peer *__rt6_get_peer(struct rt6_info *rt, int create)
{
	if (rt6_has_peer(rt))
		return rt6_peer_ptr(rt);

	rt6_bind_peer(rt, create);
	return (rt6_has_peer(rt) ? rt6_peer_ptr(rt) : NULL);
}

static inline struct inet_peer *rt6_get_peer(struct rt6_info *rt)
{
	return __rt6_get_peer(rt, 0);
}

static inline struct inet_peer *rt6_get_peer_create(struct rt6_info *rt)
{
	return __rt6_get_peer(rt, 1);
}

extern void			ip6_route_input(struct sk_buff *skb);

extern struct dst_entry *	ip6_route_output(struct net *net,
						 const struct sock *sk,
						 struct flowi6 *fl6);
extern struct dst_entry *	ip6_route_lookup(struct net *net,
						 struct flowi6 *fl6, int flags);

extern int			ip6_route_init(void);
extern void			ip6_route_cleanup(void);

extern int			ipv6_route_ioctl(struct net *net,
						 unsigned int cmd,
						 void __user *arg);

extern int			ip6_route_add(struct fib6_config *cfg);
extern int			ip6_ins_rt(struct rt6_info *);
extern int			ip6_del_rt(struct rt6_info *);

extern int			ip6_route_get_saddr(struct net *net,
						    struct rt6_info *rt,
						    const struct in6_addr *daddr,
						    unsigned int prefs,
						    struct in6_addr *saddr);

extern struct rt6_info		*rt6_lookup(struct net *net,
					    const struct in6_addr *daddr,
					    const struct in6_addr *saddr,
					    int oif, int flags);

extern struct dst_entry *icmp6_dst_alloc(struct net_device *dev,
					 struct neighbour *neigh,
					 struct flowi6 *fl6);
extern int icmp6_dst_gc(void);

extern void fib6_force_start_gc(struct net *net);

extern struct rt6_info *addrconf_dst_alloc(struct inet6_dev *idev,
					   const struct in6_addr *addr,
					   bool anycast);

extern int			ip6_dst_hoplimit(struct dst_entry *dst);

/*
 *	support functions for ND
 *
 */
extern struct rt6_info *	rt6_get_dflt_router(const struct in6_addr *addr,
						    struct net_device *dev);
extern struct rt6_info *	rt6_add_dflt_router(const struct in6_addr *gwaddr,
						    struct net_device *dev,
						    unsigned int pref);

extern void			rt6_purge_dflt_routers(struct net *net);

extern int			rt6_route_rcv(struct net_device *dev,
					      u8 *opt, int len,
					      const struct in6_addr *gwaddr);

extern void			rt6_redirect(struct sk_buff *skb);

extern void ip6_update_pmtu(struct sk_buff *skb, struct net *net, __be32 mtu,
			    int oif, u32 mark);
extern void ip6_sk_update_pmtu(struct sk_buff *skb, struct sock *sk,
			       __be32 mtu);
extern void ip6_redirect(struct sk_buff *skb, struct net *net, int oif, u32 mark);
extern void ip6_sk_redirect(struct sk_buff *skb, struct sock *sk);

struct netlink_callback;

struct rt6_rtnl_dump_arg {
	struct sk_buff *skb;
	struct netlink_callback *cb;
	struct net *net;
};

extern int rt6_dump_route(struct rt6_info *rt, void *p_arg);
extern void rt6_ifdown(struct net *net, struct net_device *dev);
extern void rt6_mtu_change(struct net_device *dev, unsigned int mtu);
extern void rt6_remove_prefsrc(struct inet6_ifaddr *ifp);


/*
 *	Store a destination cache entry in a socket
 */
static inline void __ip6_dst_store(struct sock *sk, struct dst_entry *dst,
				   struct in6_addr *daddr, struct in6_addr *saddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct rt6_info *rt = (struct rt6_info *) dst;

	sk_setup_caps(sk, dst);
	np->daddr_cache = daddr;
#ifdef CONFIG_IPV6_SUBTREES
	np->saddr_cache = saddr;
#endif
	np->dst_cookie = rt->rt6i_node ? rt->rt6i_node->fn_sernum : 0;
}

static inline void ip6_dst_store(struct sock *sk, struct dst_entry *dst,
				 struct in6_addr *daddr, struct in6_addr *saddr)
{
	spin_lock(&sk->sk_dst_lock);
	__ip6_dst_store(sk, dst, daddr, saddr);
	spin_unlock(&sk->sk_dst_lock);
}

static inline bool ipv6_unicast_destination(const struct sk_buff *skb)
{
	struct rt6_info *rt = (struct rt6_info *) skb_dst(skb);

	return rt->rt6i_flags & RTF_LOCAL;
}

int ip6_fragment(struct sk_buff *skb, int (*output)(struct sk_buff *));

static inline int ip6_skb_dst_mtu(struct sk_buff *skb)
{
	struct ipv6_pinfo *np = skb->sk ? inet6_sk(skb->sk) : NULL;

	return (np && np->pmtudisc == IPV6_PMTUDISC_PROBE) ?
	       skb_dst(skb)->dev->mtu : dst_mtu(skb_dst(skb));
}

#endif
