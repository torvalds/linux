/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _NET_IP6_ROUTE_H
#define _NET_IP6_ROUTE_H

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
	__u8			prefix[];	/* 0,8 or 16 */
};

#include <net/addrconf.h>
#include <net/flow.h>
#include <net/ip6_fib.h>
#include <net/sock.h>
#include <net/lwtunnel.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/route.h>
#include <net/nexthop.h>

#define RT6_LOOKUP_F_IFACE		0x00000001
#define RT6_LOOKUP_F_REACHABLE		0x00000002
#define RT6_LOOKUP_F_HAS_SADDR		0x00000004
#define RT6_LOOKUP_F_SRCPREF_TMP	0x00000008
#define RT6_LOOKUP_F_SRCPREF_PUBLIC	0x00000010
#define RT6_LOOKUP_F_SRCPREF_COA	0x00000020
#define RT6_LOOKUP_F_IGNORE_LINKSTATE	0x00000040
#define RT6_LOOKUP_F_DST_NOREF		0x00000080

/* We do not (yet ?) support IPv6 jumbograms (RFC 2675)
 * Unlike IPv4, hdr->seg_len doesn't include the IPv6 header
 */
#define IP6_MAX_MTU (0xFFFF + sizeof(struct ipv6hdr))

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

static inline bool rt6_need_strict(const struct in6_addr *daddr)
{
	return ipv6_addr_type(daddr) &
		(IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL | IPV6_ADDR_LOOPBACK);
}

/* fib entries using a nexthop object can not be coalesced into
 * a multipath route
 */
static inline bool rt6_qualify_for_ecmp(const struct fib6_info *f6i)
{
	/* the RTF_ADDRCONF flag filters out RA's */
	return !(f6i->fib6_flags & RTF_ADDRCONF) && !f6i->nh &&
		f6i->fib6_nh->fib_nh_gw_family;
}

void ip6_route_input(struct sk_buff *skb);
struct dst_entry *ip6_route_input_lookup(struct net *net,
					 struct net_device *dev,
					 struct flowi6 *fl6,
					 const struct sk_buff *skb, int flags);

struct dst_entry *ip6_route_output_flags_noref(struct net *net,
					       const struct sock *sk,
					       struct flowi6 *fl6, int flags);

struct dst_entry *ip6_route_output_flags(struct net *net, const struct sock *sk,
					 struct flowi6 *fl6, int flags);

static inline struct dst_entry *ip6_route_output(struct net *net,
						 const struct sock *sk,
						 struct flowi6 *fl6)
{
	return ip6_route_output_flags(net, sk, fl6, 0);
}

/* Only conditionally release dst if flags indicates
 * !RT6_LOOKUP_F_DST_NOREF or dst is in uncached_list.
 */
static inline void ip6_rt_put_flags(struct rt6_info *rt, int flags)
{
	if (!(flags & RT6_LOOKUP_F_DST_NOREF) ||
	    !list_empty(&rt->rt6i_uncached))
		ip6_rt_put(rt);
}

struct dst_entry *ip6_route_lookup(struct net *net, struct flowi6 *fl6,
				   const struct sk_buff *skb, int flags);
struct rt6_info *ip6_pol_route(struct net *net, struct fib6_table *table,
			       int ifindex, struct flowi6 *fl6,
			       const struct sk_buff *skb, int flags);

void ip6_route_init_special_entries(void);
int ip6_route_init(void);
void ip6_route_cleanup(void);

int ipv6_route_ioctl(struct net *net, unsigned int cmd, void __user *arg);

int ip6_route_add(struct fib6_config *cfg, gfp_t gfp_flags,
		  struct netlink_ext_ack *extack);
int ip6_ins_rt(struct net *net, struct fib6_info *f6i);
int ip6_del_rt(struct net *net, struct fib6_info *f6i);

void rt6_flush_exceptions(struct fib6_info *f6i);
void rt6_age_exceptions(struct fib6_info *f6i, struct fib6_gc_args *gc_args,
			unsigned long now);

static inline int ip6_route_get_saddr(struct net *net, struct fib6_info *f6i,
				      const struct in6_addr *daddr,
				      unsigned int prefs,
				      struct in6_addr *saddr)
{
	int err = 0;

	if (f6i && f6i->fib6_prefsrc.plen) {
		*saddr = f6i->fib6_prefsrc.addr;
	} else {
		struct net_device *dev = f6i ? fib6_info_nh_dev(f6i) : NULL;

		err = ipv6_dev_get_saddr(net, dev, daddr, prefs, saddr);
	}

	return err;
}

struct rt6_info *rt6_lookup(struct net *net, const struct in6_addr *daddr,
			    const struct in6_addr *saddr, int oif,
			    const struct sk_buff *skb, int flags);
u32 rt6_multipath_hash(const struct net *net, const struct flowi6 *fl6,
		       const struct sk_buff *skb, struct flow_keys *hkeys);

struct dst_entry *icmp6_dst_alloc(struct net_device *dev, struct flowi6 *fl6);

void fib6_force_start_gc(struct net *net);

struct fib6_info *addrconf_f6i_alloc(struct net *net, struct inet6_dev *idev,
				     const struct in6_addr *addr, bool anycast,
				     gfp_t gfp_flags);

struct rt6_info *ip6_dst_alloc(struct net *net, struct net_device *dev,
			       int flags);

/*
 *	support functions for ND
 *
 */
struct fib6_info *rt6_get_dflt_router(struct net *net,
				     const struct in6_addr *addr,
				     struct net_device *dev);
struct fib6_info *rt6_add_dflt_router(struct net *net,
				     const struct in6_addr *gwaddr,
				     struct net_device *dev, unsigned int pref);

void rt6_purge_dflt_routers(struct net *net);

int rt6_route_rcv(struct net_device *dev, u8 *opt, int len,
		  const struct in6_addr *gwaddr);

void ip6_update_pmtu(struct sk_buff *skb, struct net *net, __be32 mtu, int oif,
		     u32 mark, kuid_t uid);
void ip6_sk_update_pmtu(struct sk_buff *skb, struct sock *sk, __be32 mtu);
void ip6_redirect(struct sk_buff *skb, struct net *net, int oif, u32 mark,
		  kuid_t uid);
void ip6_redirect_no_header(struct sk_buff *skb, struct net *net, int oif);
void ip6_sk_redirect(struct sk_buff *skb, struct sock *sk);

struct netlink_callback;

struct rt6_rtnl_dump_arg {
	struct sk_buff *skb;
	struct netlink_callback *cb;
	struct net *net;
	struct fib_dump_filter filter;
};

int rt6_dump_route(struct fib6_info *f6i, void *p_arg, unsigned int skip);
void rt6_mtu_change(struct net_device *dev, unsigned int mtu);
void rt6_remove_prefsrc(struct inet6_ifaddr *ifp);
void rt6_clean_tohost(struct net *net, struct in6_addr *gateway);
void rt6_sync_up(struct net_device *dev, unsigned char nh_flags);
void rt6_disable_ip(struct net_device *dev, unsigned long event);
void rt6_sync_down_dev(struct net_device *dev, unsigned long event);
void rt6_multipath_rebalance(struct fib6_info *f6i);

void rt6_uncached_list_add(struct rt6_info *rt);
void rt6_uncached_list_del(struct rt6_info *rt);

static inline const struct rt6_info *skb_rt6_info(const struct sk_buff *skb)
{
	const struct dst_entry *dst = skb_dst(skb);
	const struct rt6_info *rt6 = NULL;

	if (dst)
		rt6 = container_of(dst, struct rt6_info, dst);

	return rt6;
}

/*
 *	Store a destination cache entry in a socket
 */
static inline void ip6_dst_store(struct sock *sk, struct dst_entry *dst,
				 const struct in6_addr *daddr,
				 const struct in6_addr *saddr)
{
	struct ipv6_pinfo *np = inet6_sk(sk);

	np->dst_cookie = rt6_get_cookie((struct rt6_info *)dst);
	sk_setup_caps(sk, dst);
	np->daddr_cache = daddr;
#ifdef CONFIG_IPV6_SUBTREES
	np->saddr_cache = saddr;
#endif
}

void ip6_sk_dst_store_flow(struct sock *sk, struct dst_entry *dst,
			   const struct flowi6 *fl6);

static inline bool ipv6_unicast_destination(const struct sk_buff *skb)
{
	struct rt6_info *rt = (struct rt6_info *) skb_dst(skb);

	return rt->rt6i_flags & RTF_LOCAL;
}

static inline bool ipv6_anycast_destination(const struct dst_entry *dst,
					    const struct in6_addr *daddr)
{
	struct rt6_info *rt = (struct rt6_info *)dst;

	return rt->rt6i_flags & RTF_ANYCAST ||
		(rt->rt6i_dst.plen < 127 &&
		 ipv6_addr_equal(&rt->rt6i_dst.addr, daddr));
}

int ip6_fragment(struct net *net, struct sock *sk, struct sk_buff *skb,
		 int (*output)(struct net *, struct sock *, struct sk_buff *));

static inline int ip6_skb_dst_mtu(struct sk_buff *skb)
{
	struct ipv6_pinfo *np = skb->sk && !dev_recursion_level() ?
				inet6_sk(skb->sk) : NULL;

	return (np && np->pmtudisc >= IPV6_PMTUDISC_PROBE) ?
	       skb_dst(skb)->dev->mtu : dst_mtu(skb_dst(skb));
}

static inline bool ip6_sk_accept_pmtu(const struct sock *sk)
{
	return inet6_sk(sk)->pmtudisc != IPV6_PMTUDISC_INTERFACE &&
	       inet6_sk(sk)->pmtudisc != IPV6_PMTUDISC_OMIT;
}

static inline bool ip6_sk_ignore_df(const struct sock *sk)
{
	return inet6_sk(sk)->pmtudisc < IPV6_PMTUDISC_DO ||
	       inet6_sk(sk)->pmtudisc == IPV6_PMTUDISC_OMIT;
}

static inline const struct in6_addr *rt6_nexthop(const struct rt6_info *rt,
						 const struct in6_addr *daddr)
{
	if (rt->rt6i_flags & RTF_GATEWAY)
		return &rt->rt6i_gateway;
	else if (unlikely(rt->rt6i_flags & RTF_CACHE))
		return &rt->rt6i_dst.addr;
	else
		return daddr;
}

static inline bool rt6_duplicate_nexthop(struct fib6_info *a, struct fib6_info *b)
{
	struct fib6_nh *nha, *nhb;

	if (a->nh || b->nh)
		return nexthop_cmp(a->nh, b->nh);

	nha = a->fib6_nh;
	nhb = b->fib6_nh;
	return nha->fib_nh_dev == nhb->fib_nh_dev &&
	       ipv6_addr_equal(&nha->fib_nh_gw6, &nhb->fib_nh_gw6) &&
	       !lwtunnel_cmp_encap(nha->fib_nh_lws, nhb->fib_nh_lws);
}

static inline unsigned int ip6_dst_mtu_forward(const struct dst_entry *dst)
{
	struct inet6_dev *idev;
	unsigned int mtu;

	if (dst_metric_locked(dst, RTAX_MTU)) {
		mtu = dst_metric_raw(dst, RTAX_MTU);
		if (mtu)
			return mtu;
	}

	mtu = IPV6_MIN_MTU;
	rcu_read_lock();
	idev = __in6_dev_get(dst->dev);
	if (idev)
		mtu = idev->cnf.mtu6;
	rcu_read_unlock();

	return mtu;
}

u32 ip6_mtu_from_fib6(const struct fib6_result *res,
		      const struct in6_addr *daddr,
		      const struct in6_addr *saddr);

struct neighbour *ip6_neigh_lookup(const struct in6_addr *gw,
				   struct net_device *dev, struct sk_buff *skb,
				   const void *daddr);
#endif
