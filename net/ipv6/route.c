/*
 *	Linux INET6 implementation
 *	FIB front-end.
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*	Changes:
 *
 *	YOSHIFUJI Hideaki @USAGI
 *		reworked default router selection.
 *		- respect outgoing interface
 *		- select from (probably) reachable routers (i.e.
 *		routers in REACHABLE, STALE, DELAY or PROBE states).
 *		- always select the same router if it is (probably)
 *		reachable.  otherwise, round-robin the list.
 *	Ville Nuorvala
 *		Fixed routing subtrees.
 */

#include <linux/capability.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/types.h>
#include <linux/times.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/route.h>
#include <linux/netdevice.h>
#include <linux/in6.h>
#include <linux/mroute6.h>
#include <linux/init.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/nsproxy.h>
#include <linux/slab.h>
#include <net/net_namespace.h>
#include <net/snmp.h>
#include <net/ipv6.h>
#include <net/ip6_fib.h>
#include <net/ip6_route.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/tcp.h>
#include <linux/rtnetlink.h>
#include <net/dst.h>
#include <net/xfrm.h>
#include <net/netevent.h>
#include <net/netlink.h>

#include <asm/uaccess.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

/* Set to 3 to get tracing. */
#define RT6_DEBUG 2

#if RT6_DEBUG >= 3
#define RDBG(x) printk x
#define RT6_TRACE(x...) printk(KERN_DEBUG x)
#else
#define RDBG(x)
#define RT6_TRACE(x...) do { ; } while (0)
#endif

static struct rt6_info *ip6_rt_copy(const struct rt6_info *ort,
				    const struct in6_addr *dest);
static struct dst_entry	*ip6_dst_check(struct dst_entry *dst, u32 cookie);
static unsigned int	 ip6_default_advmss(const struct dst_entry *dst);
static unsigned int	 ip6_default_mtu(const struct dst_entry *dst);
static struct dst_entry *ip6_negative_advice(struct dst_entry *);
static void		ip6_dst_destroy(struct dst_entry *);
static void		ip6_dst_ifdown(struct dst_entry *,
				       struct net_device *dev, int how);
static int		 ip6_dst_gc(struct dst_ops *ops);

static int		ip6_pkt_discard(struct sk_buff *skb);
static int		ip6_pkt_discard_out(struct sk_buff *skb);
static void		ip6_link_failure(struct sk_buff *skb);
static void		ip6_rt_update_pmtu(struct dst_entry *dst, u32 mtu);

#ifdef CONFIG_IPV6_ROUTE_INFO
static struct rt6_info *rt6_add_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr, int ifindex,
					   unsigned pref);
static struct rt6_info *rt6_get_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr, int ifindex);
#endif

static u32 *ipv6_cow_metrics(struct dst_entry *dst, unsigned long old)
{
	struct rt6_info *rt = (struct rt6_info *) dst;
	struct inet_peer *peer;
	u32 *p = NULL;

	if (!(rt->dst.flags & DST_HOST))
		return NULL;

	if (!rt->rt6i_peer)
		rt6_bind_peer(rt, 1);

	peer = rt->rt6i_peer;
	if (peer) {
		u32 *old_p = __DST_METRICS_PTR(old);
		unsigned long prev, new;

		p = peer->metrics;
		if (inet_metrics_new(peer))
			memcpy(p, old_p, sizeof(u32) * RTAX_MAX);

		new = (unsigned long) p;
		prev = cmpxchg(&dst->_metrics, old, new);

		if (prev != old) {
			p = __DST_METRICS_PTR(prev);
			if (prev & DST_METRICS_READ_ONLY)
				p = NULL;
		}
	}
	return p;
}

static struct neighbour *ip6_neigh_lookup(const struct dst_entry *dst, const void *daddr)
{
	return __neigh_lookup_errno(&nd_tbl, daddr, dst->dev);
}

static struct dst_ops ip6_dst_ops_template = {
	.family			=	AF_INET6,
	.protocol		=	cpu_to_be16(ETH_P_IPV6),
	.gc			=	ip6_dst_gc,
	.gc_thresh		=	1024,
	.check			=	ip6_dst_check,
	.default_advmss		=	ip6_default_advmss,
	.default_mtu		=	ip6_default_mtu,
	.cow_metrics		=	ipv6_cow_metrics,
	.destroy		=	ip6_dst_destroy,
	.ifdown			=	ip6_dst_ifdown,
	.negative_advice	=	ip6_negative_advice,
	.link_failure		=	ip6_link_failure,
	.update_pmtu		=	ip6_rt_update_pmtu,
	.local_out		=	__ip6_local_out,
	.neigh_lookup		=	ip6_neigh_lookup,
};

static unsigned int ip6_blackhole_default_mtu(const struct dst_entry *dst)
{
	return 0;
}

static void ip6_rt_blackhole_update_pmtu(struct dst_entry *dst, u32 mtu)
{
}

static u32 *ip6_rt_blackhole_cow_metrics(struct dst_entry *dst,
					 unsigned long old)
{
	return NULL;
}

static struct dst_ops ip6_dst_blackhole_ops = {
	.family			=	AF_INET6,
	.protocol		=	cpu_to_be16(ETH_P_IPV6),
	.destroy		=	ip6_dst_destroy,
	.check			=	ip6_dst_check,
	.default_mtu		=	ip6_blackhole_default_mtu,
	.default_advmss		=	ip6_default_advmss,
	.update_pmtu		=	ip6_rt_blackhole_update_pmtu,
	.cow_metrics		=	ip6_rt_blackhole_cow_metrics,
	.neigh_lookup		=	ip6_neigh_lookup,
};

static const u32 ip6_template_metrics[RTAX_MAX] = {
	[RTAX_HOPLIMIT - 1] = 255,
};

static struct rt6_info ip6_null_entry_template = {
	.dst = {
		.__refcnt	= ATOMIC_INIT(1),
		.__use		= 1,
		.obsolete	= -1,
		.error		= -ENETUNREACH,
		.input		= ip6_pkt_discard,
		.output		= ip6_pkt_discard_out,
	},
	.rt6i_flags	= (RTF_REJECT | RTF_NONEXTHOP),
	.rt6i_protocol  = RTPROT_KERNEL,
	.rt6i_metric	= ~(u32) 0,
	.rt6i_ref	= ATOMIC_INIT(1),
};

#ifdef CONFIG_IPV6_MULTIPLE_TABLES

static int ip6_pkt_prohibit(struct sk_buff *skb);
static int ip6_pkt_prohibit_out(struct sk_buff *skb);

static struct rt6_info ip6_prohibit_entry_template = {
	.dst = {
		.__refcnt	= ATOMIC_INIT(1),
		.__use		= 1,
		.obsolete	= -1,
		.error		= -EACCES,
		.input		= ip6_pkt_prohibit,
		.output		= ip6_pkt_prohibit_out,
	},
	.rt6i_flags	= (RTF_REJECT | RTF_NONEXTHOP),
	.rt6i_protocol  = RTPROT_KERNEL,
	.rt6i_metric	= ~(u32) 0,
	.rt6i_ref	= ATOMIC_INIT(1),
};

static struct rt6_info ip6_blk_hole_entry_template = {
	.dst = {
		.__refcnt	= ATOMIC_INIT(1),
		.__use		= 1,
		.obsolete	= -1,
		.error		= -EINVAL,
		.input		= dst_discard,
		.output		= dst_discard,
	},
	.rt6i_flags	= (RTF_REJECT | RTF_NONEXTHOP),
	.rt6i_protocol  = RTPROT_KERNEL,
	.rt6i_metric	= ~(u32) 0,
	.rt6i_ref	= ATOMIC_INIT(1),
};

#endif

/* allocate dst with ip6_dst_ops */
static inline struct rt6_info *ip6_dst_alloc(struct dst_ops *ops,
					     struct net_device *dev,
					     int flags)
{
	struct rt6_info *rt = dst_alloc(ops, dev, 0, 0, flags);

	if (rt != NULL)
		memset(&rt->rt6i_table, 0,
			sizeof(*rt) - sizeof(struct dst_entry));

	return rt;
}

static void ip6_dst_destroy(struct dst_entry *dst)
{
	struct rt6_info *rt = (struct rt6_info *)dst;
	struct inet6_dev *idev = rt->rt6i_idev;
	struct inet_peer *peer = rt->rt6i_peer;

	if (!(rt->dst.flags & DST_HOST))
		dst_destroy_metrics_generic(dst);

	if (idev != NULL) {
		rt->rt6i_idev = NULL;
		in6_dev_put(idev);
	}
	if (peer) {
		rt->rt6i_peer = NULL;
		inet_putpeer(peer);
	}
}

static atomic_t __rt6_peer_genid = ATOMIC_INIT(0);

static u32 rt6_peer_genid(void)
{
	return atomic_read(&__rt6_peer_genid);
}

void rt6_bind_peer(struct rt6_info *rt, int create)
{
	struct inet_peer *peer;

	peer = inet_getpeer_v6(&rt->rt6i_dst.addr, create);
	if (peer && cmpxchg(&rt->rt6i_peer, NULL, peer) != NULL)
		inet_putpeer(peer);
	else
		rt->rt6i_peer_genid = rt6_peer_genid();
}

static void ip6_dst_ifdown(struct dst_entry *dst, struct net_device *dev,
			   int how)
{
	struct rt6_info *rt = (struct rt6_info *)dst;
	struct inet6_dev *idev = rt->rt6i_idev;
	struct net_device *loopback_dev =
		dev_net(dev)->loopback_dev;

	if (dev != loopback_dev && idev != NULL && idev->dev == dev) {
		struct inet6_dev *loopback_idev =
			in6_dev_get(loopback_dev);
		if (loopback_idev != NULL) {
			rt->rt6i_idev = loopback_idev;
			in6_dev_put(idev);
		}
	}
}

static __inline__ int rt6_check_expired(const struct rt6_info *rt)
{
	return (rt->rt6i_flags & RTF_EXPIRES) &&
		time_after(jiffies, rt->rt6i_expires);
}

static inline int rt6_need_strict(const struct in6_addr *daddr)
{
	return ipv6_addr_type(daddr) &
		(IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL | IPV6_ADDR_LOOPBACK);
}

/*
 *	Route lookup. Any table->tb6_lock is implied.
 */

static inline struct rt6_info *rt6_device_match(struct net *net,
						    struct rt6_info *rt,
						    const struct in6_addr *saddr,
						    int oif,
						    int flags)
{
	struct rt6_info *local = NULL;
	struct rt6_info *sprt;

	if (!oif && ipv6_addr_any(saddr))
		goto out;

	for (sprt = rt; sprt; sprt = sprt->dst.rt6_next) {
		struct net_device *dev = sprt->rt6i_dev;

		if (oif) {
			if (dev->ifindex == oif)
				return sprt;
			if (dev->flags & IFF_LOOPBACK) {
				if (sprt->rt6i_idev == NULL ||
				    sprt->rt6i_idev->dev->ifindex != oif) {
					if (flags & RT6_LOOKUP_F_IFACE && oif)
						continue;
					if (local && (!oif ||
						      local->rt6i_idev->dev->ifindex == oif))
						continue;
				}
				local = sprt;
			}
		} else {
			if (ipv6_chk_addr(net, saddr, dev,
					  flags & RT6_LOOKUP_F_IFACE))
				return sprt;
		}
	}

	if (oif) {
		if (local)
			return local;

		if (flags & RT6_LOOKUP_F_IFACE)
			return net->ipv6.ip6_null_entry;
	}
out:
	return rt;
}

#ifdef CONFIG_IPV6_ROUTER_PREF
static void rt6_probe(struct rt6_info *rt)
{
	struct neighbour *neigh;
	/*
	 * Okay, this does not seem to be appropriate
	 * for now, however, we need to check if it
	 * is really so; aka Router Reachability Probing.
	 *
	 * Router Reachability Probe MUST be rate-limited
	 * to no more than one per minute.
	 */
	rcu_read_lock();
	neigh = rt ? dst_get_neighbour(&rt->dst) : NULL;
	if (!neigh || (neigh->nud_state & NUD_VALID))
		goto out;
	read_lock_bh(&neigh->lock);
	if (!(neigh->nud_state & NUD_VALID) &&
	    time_after(jiffies, neigh->updated + rt->rt6i_idev->cnf.rtr_probe_interval)) {
		struct in6_addr mcaddr;
		struct in6_addr *target;

		neigh->updated = jiffies;
		read_unlock_bh(&neigh->lock);

		target = (struct in6_addr *)&neigh->primary_key;
		addrconf_addr_solict_mult(target, &mcaddr);
		ndisc_send_ns(rt->rt6i_dev, NULL, target, &mcaddr, NULL);
	} else {
		read_unlock_bh(&neigh->lock);
	}
out:
	rcu_read_unlock();
}
#else
static inline void rt6_probe(struct rt6_info *rt)
{
}
#endif

/*
 * Default Router Selection (RFC 2461 6.3.6)
 */
static inline int rt6_check_dev(struct rt6_info *rt, int oif)
{
	struct net_device *dev = rt->rt6i_dev;
	if (!oif || dev->ifindex == oif)
		return 2;
	if ((dev->flags & IFF_LOOPBACK) &&
	    rt->rt6i_idev && rt->rt6i_idev->dev->ifindex == oif)
		return 1;
	return 0;
}

static inline int rt6_check_neigh(struct rt6_info *rt)
{
	struct neighbour *neigh;
	int m;

	rcu_read_lock();
	neigh = dst_get_neighbour(&rt->dst);
	if (rt->rt6i_flags & RTF_NONEXTHOP ||
	    !(rt->rt6i_flags & RTF_GATEWAY))
		m = 1;
	else if (neigh) {
		read_lock_bh(&neigh->lock);
		if (neigh->nud_state & NUD_VALID)
			m = 2;
#ifdef CONFIG_IPV6_ROUTER_PREF
		else if (neigh->nud_state & NUD_FAILED)
			m = 0;
#endif
		else
			m = 1;
		read_unlock_bh(&neigh->lock);
	} else
		m = 0;
	rcu_read_unlock();
	return m;
}

static int rt6_score_route(struct rt6_info *rt, int oif,
			   int strict)
{
	int m, n;

	m = rt6_check_dev(rt, oif);
	if (!m && (strict & RT6_LOOKUP_F_IFACE))
		return -1;
#ifdef CONFIG_IPV6_ROUTER_PREF
	m |= IPV6_DECODE_PREF(IPV6_EXTRACT_PREF(rt->rt6i_flags)) << 2;
#endif
	n = rt6_check_neigh(rt);
	if (!n && (strict & RT6_LOOKUP_F_REACHABLE))
		return -1;
	return m;
}

static struct rt6_info *find_match(struct rt6_info *rt, int oif, int strict,
				   int *mpri, struct rt6_info *match)
{
	int m;

	if (rt6_check_expired(rt))
		goto out;

	m = rt6_score_route(rt, oif, strict);
	if (m < 0)
		goto out;

	if (m > *mpri) {
		if (strict & RT6_LOOKUP_F_REACHABLE)
			rt6_probe(match);
		*mpri = m;
		match = rt;
	} else if (strict & RT6_LOOKUP_F_REACHABLE) {
		rt6_probe(rt);
	}

out:
	return match;
}

static struct rt6_info *find_rr_leaf(struct fib6_node *fn,
				     struct rt6_info *rr_head,
				     u32 metric, int oif, int strict)
{
	struct rt6_info *rt, *match;
	int mpri = -1;

	match = NULL;
	for (rt = rr_head; rt && rt->rt6i_metric == metric;
	     rt = rt->dst.rt6_next)
		match = find_match(rt, oif, strict, &mpri, match);
	for (rt = fn->leaf; rt && rt != rr_head && rt->rt6i_metric == metric;
	     rt = rt->dst.rt6_next)
		match = find_match(rt, oif, strict, &mpri, match);

	return match;
}

static struct rt6_info *rt6_select(struct fib6_node *fn, int oif, int strict)
{
	struct rt6_info *match, *rt0;
	struct net *net;

	RT6_TRACE("%s(fn->leaf=%p, oif=%d)\n",
		  __func__, fn->leaf, oif);

	rt0 = fn->rr_ptr;
	if (!rt0)
		fn->rr_ptr = rt0 = fn->leaf;

	match = find_rr_leaf(fn, rt0, rt0->rt6i_metric, oif, strict);

	if (!match &&
	    (strict & RT6_LOOKUP_F_REACHABLE)) {
		struct rt6_info *next = rt0->dst.rt6_next;

		/* no entries matched; do round-robin */
		if (!next || next->rt6i_metric != rt0->rt6i_metric)
			next = fn->leaf;

		if (next != rt0)
			fn->rr_ptr = next;
	}

	RT6_TRACE("%s() => %p\n",
		  __func__, match);

	net = dev_net(rt0->rt6i_dev);
	return match ? match : net->ipv6.ip6_null_entry;
}

#ifdef CONFIG_IPV6_ROUTE_INFO
int rt6_route_rcv(struct net_device *dev, u8 *opt, int len,
		  const struct in6_addr *gwaddr)
{
	struct net *net = dev_net(dev);
	struct route_info *rinfo = (struct route_info *) opt;
	struct in6_addr prefix_buf, *prefix;
	unsigned int pref;
	unsigned long lifetime;
	struct rt6_info *rt;

	if (len < sizeof(struct route_info)) {
		return -EINVAL;
	}

	/* Sanity check for prefix_len and length */
	if (rinfo->length > 3) {
		return -EINVAL;
	} else if (rinfo->prefix_len > 128) {
		return -EINVAL;
	} else if (rinfo->prefix_len > 64) {
		if (rinfo->length < 2) {
			return -EINVAL;
		}
	} else if (rinfo->prefix_len > 0) {
		if (rinfo->length < 1) {
			return -EINVAL;
		}
	}

	pref = rinfo->route_pref;
	if (pref == ICMPV6_ROUTER_PREF_INVALID)
		return -EINVAL;

	lifetime = addrconf_timeout_fixup(ntohl(rinfo->lifetime), HZ);

	if (rinfo->length == 3)
		prefix = (struct in6_addr *)rinfo->prefix;
	else {
		/* this function is safe */
		ipv6_addr_prefix(&prefix_buf,
				 (struct in6_addr *)rinfo->prefix,
				 rinfo->prefix_len);
		prefix = &prefix_buf;
	}

	rt = rt6_get_route_info(net, prefix, rinfo->prefix_len, gwaddr,
				dev->ifindex);

	if (rt && !lifetime) {
		ip6_del_rt(rt);
		rt = NULL;
	}

	if (!rt && lifetime)
		rt = rt6_add_route_info(net, prefix, rinfo->prefix_len, gwaddr, dev->ifindex,
					pref);
	else if (rt)
		rt->rt6i_flags = RTF_ROUTEINFO |
				 (rt->rt6i_flags & ~RTF_PREF_MASK) | RTF_PREF(pref);

	if (rt) {
		if (!addrconf_finite_timeout(lifetime)) {
			rt->rt6i_flags &= ~RTF_EXPIRES;
		} else {
			rt->rt6i_expires = jiffies + HZ * lifetime;
			rt->rt6i_flags |= RTF_EXPIRES;
		}
		dst_release(&rt->dst);
	}
	return 0;
}
#endif

#define BACKTRACK(__net, saddr)			\
do { \
	if (rt == __net->ipv6.ip6_null_entry) {	\
		struct fib6_node *pn; \
		while (1) { \
			if (fn->fn_flags & RTN_TL_ROOT) \
				goto out; \
			pn = fn->parent; \
			if (FIB6_SUBTREE(pn) && FIB6_SUBTREE(pn) != fn) \
				fn = fib6_lookup(FIB6_SUBTREE(pn), NULL, saddr); \
			else \
				fn = pn; \
			if (fn->fn_flags & RTN_RTINFO) \
				goto restart; \
		} \
	} \
} while(0)

static struct rt6_info *ip6_pol_route_lookup(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6, int flags)
{
	struct fib6_node *fn;
	struct rt6_info *rt;

	read_lock_bh(&table->tb6_lock);
	fn = fib6_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
restart:
	rt = fn->leaf;
	rt = rt6_device_match(net, rt, &fl6->saddr, fl6->flowi6_oif, flags);
	BACKTRACK(net, &fl6->saddr);
out:
	dst_use(&rt->dst, jiffies);
	read_unlock_bh(&table->tb6_lock);
	return rt;

}

struct rt6_info *rt6_lookup(struct net *net, const struct in6_addr *daddr,
			    const struct in6_addr *saddr, int oif, int strict)
{
	struct flowi6 fl6 = {
		.flowi6_oif = oif,
		.daddr = *daddr,
	};
	struct dst_entry *dst;
	int flags = strict ? RT6_LOOKUP_F_IFACE : 0;

	if (saddr) {
		memcpy(&fl6.saddr, saddr, sizeof(*saddr));
		flags |= RT6_LOOKUP_F_HAS_SADDR;
	}

	dst = fib6_rule_lookup(net, &fl6, flags, ip6_pol_route_lookup);
	if (dst->error == 0)
		return (struct rt6_info *) dst;

	dst_release(dst);

	return NULL;
}

EXPORT_SYMBOL(rt6_lookup);

/* ip6_ins_rt is called with FREE table->tb6_lock.
   It takes new route entry, the addition fails by any reason the
   route is freed. In any case, if caller does not hold it, it may
   be destroyed.
 */

static int __ip6_ins_rt(struct rt6_info *rt, struct nl_info *info)
{
	int err;
	struct fib6_table *table;

	table = rt->rt6i_table;
	write_lock_bh(&table->tb6_lock);
	err = fib6_add(&table->tb6_root, rt, info);
	write_unlock_bh(&table->tb6_lock);

	return err;
}

int ip6_ins_rt(struct rt6_info *rt)
{
	struct nl_info info = {
		.nl_net = dev_net(rt->rt6i_dev),
	};
	return __ip6_ins_rt(rt, &info);
}

static struct rt6_info *rt6_alloc_cow(const struct rt6_info *ort,
				      const struct in6_addr *daddr,
				      const struct in6_addr *saddr)
{
	struct rt6_info *rt;

	/*
	 *	Clone the route.
	 */

	rt = ip6_rt_copy(ort, daddr);

	if (rt) {
		struct neighbour *neigh;
		int attempts = !in_softirq();

		if (!(rt->rt6i_flags&RTF_GATEWAY)) {
			if (rt->rt6i_dst.plen != 128 &&
			    ipv6_addr_equal(&ort->rt6i_dst.addr, daddr))
				rt->rt6i_flags |= RTF_ANYCAST;
			ipv6_addr_copy(&rt->rt6i_gateway, daddr);
		}

		rt->rt6i_flags |= RTF_CACHE;

#ifdef CONFIG_IPV6_SUBTREES
		if (rt->rt6i_src.plen && saddr) {
			ipv6_addr_copy(&rt->rt6i_src.addr, saddr);
			rt->rt6i_src.plen = 128;
		}
#endif

	retry:
		neigh = ndisc_get_neigh(rt->rt6i_dev, &rt->rt6i_gateway);
		if (IS_ERR(neigh)) {
			struct net *net = dev_net(rt->rt6i_dev);
			int saved_rt_min_interval =
				net->ipv6.sysctl.ip6_rt_gc_min_interval;
			int saved_rt_elasticity =
				net->ipv6.sysctl.ip6_rt_gc_elasticity;

			if (attempts-- > 0) {
				net->ipv6.sysctl.ip6_rt_gc_elasticity = 1;
				net->ipv6.sysctl.ip6_rt_gc_min_interval = 0;

				ip6_dst_gc(&net->ipv6.ip6_dst_ops);

				net->ipv6.sysctl.ip6_rt_gc_elasticity =
					saved_rt_elasticity;
				net->ipv6.sysctl.ip6_rt_gc_min_interval =
					saved_rt_min_interval;
				goto retry;
			}

			if (net_ratelimit())
				printk(KERN_WARNING
				       "ipv6: Neighbour table overflow.\n");
			dst_free(&rt->dst);
			return NULL;
		}
		dst_set_neighbour(&rt->dst, neigh);

	}

	return rt;
}

static struct rt6_info *rt6_alloc_clone(struct rt6_info *ort,
					const struct in6_addr *daddr)
{
	struct rt6_info *rt = ip6_rt_copy(ort, daddr);

	if (rt) {
		rt->rt6i_flags |= RTF_CACHE;
		dst_set_neighbour(&rt->dst, neigh_clone(dst_get_neighbour_raw(&ort->dst)));
	}
	return rt;
}

static struct rt6_info *ip6_pol_route(struct net *net, struct fib6_table *table, int oif,
				      struct flowi6 *fl6, int flags)
{
	struct fib6_node *fn;
	struct rt6_info *rt, *nrt;
	int strict = 0;
	int attempts = 3;
	int err;
	int reachable = net->ipv6.devconf_all->forwarding ? 0 : RT6_LOOKUP_F_REACHABLE;

	strict |= flags & RT6_LOOKUP_F_IFACE;

relookup:
	read_lock_bh(&table->tb6_lock);

restart_2:
	fn = fib6_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);

restart:
	rt = rt6_select(fn, oif, strict | reachable);

	BACKTRACK(net, &fl6->saddr);
	if (rt == net->ipv6.ip6_null_entry ||
	    rt->rt6i_flags & RTF_CACHE)
		goto out;

	dst_hold(&rt->dst);
	read_unlock_bh(&table->tb6_lock);

	if (!dst_get_neighbour_raw(&rt->dst) && !(rt->rt6i_flags & RTF_NONEXTHOP))
		nrt = rt6_alloc_cow(rt, &fl6->daddr, &fl6->saddr);
	else if (!(rt->dst.flags & DST_HOST))
		nrt = rt6_alloc_clone(rt, &fl6->daddr);
	else
		goto out2;

	dst_release(&rt->dst);
	rt = nrt ? : net->ipv6.ip6_null_entry;

	dst_hold(&rt->dst);
	if (nrt) {
		err = ip6_ins_rt(nrt);
		if (!err)
			goto out2;
	}

	if (--attempts <= 0)
		goto out2;

	/*
	 * Race condition! In the gap, when table->tb6_lock was
	 * released someone could insert this route.  Relookup.
	 */
	dst_release(&rt->dst);
	goto relookup;

out:
	if (reachable) {
		reachable = 0;
		goto restart_2;
	}
	dst_hold(&rt->dst);
	read_unlock_bh(&table->tb6_lock);
out2:
	rt->dst.lastuse = jiffies;
	rt->dst.__use++;

	return rt;
}

static struct rt6_info *ip6_pol_route_input(struct net *net, struct fib6_table *table,
					    struct flowi6 *fl6, int flags)
{
	return ip6_pol_route(net, table, fl6->flowi6_iif, fl6, flags);
}

void ip6_route_input(struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct net *net = dev_net(skb->dev);
	int flags = RT6_LOOKUP_F_HAS_SADDR;
	struct flowi6 fl6 = {
		.flowi6_iif = skb->dev->ifindex,
		.daddr = iph->daddr,
		.saddr = iph->saddr,
		.flowlabel = (* (__be32 *) iph)&IPV6_FLOWINFO_MASK,
		.flowi6_mark = skb->mark,
		.flowi6_proto = iph->nexthdr,
	};

	if (rt6_need_strict(&iph->daddr) && skb->dev->type != ARPHRD_PIMREG)
		flags |= RT6_LOOKUP_F_IFACE;

	skb_dst_set(skb, fib6_rule_lookup(net, &fl6, flags, ip6_pol_route_input));
}

static struct rt6_info *ip6_pol_route_output(struct net *net, struct fib6_table *table,
					     struct flowi6 *fl6, int flags)
{
	return ip6_pol_route(net, table, fl6->flowi6_oif, fl6, flags);
}

struct dst_entry * ip6_route_output(struct net *net, const struct sock *sk,
				    struct flowi6 *fl6)
{
	int flags = 0;

	if ((sk && sk->sk_bound_dev_if) || rt6_need_strict(&fl6->daddr))
		flags |= RT6_LOOKUP_F_IFACE;

	if (!ipv6_addr_any(&fl6->saddr))
		flags |= RT6_LOOKUP_F_HAS_SADDR;
	else if (sk)
		flags |= rt6_srcprefs2flags(inet6_sk(sk)->srcprefs);

	return fib6_rule_lookup(net, fl6, flags, ip6_pol_route_output);
}

EXPORT_SYMBOL(ip6_route_output);

struct dst_entry *ip6_blackhole_route(struct net *net, struct dst_entry *dst_orig)
{
	struct rt6_info *rt, *ort = (struct rt6_info *) dst_orig;
	struct dst_entry *new = NULL;

	rt = dst_alloc(&ip6_dst_blackhole_ops, ort->dst.dev, 1, 0, 0);
	if (rt) {
		memset(&rt->rt6i_table, 0, sizeof(*rt) - sizeof(struct dst_entry));

		new = &rt->dst;

		new->__use = 1;
		new->input = dst_discard;
		new->output = dst_discard;

		if (dst_metrics_read_only(&ort->dst))
			new->_metrics = ort->dst._metrics;
		else
			dst_copy_metrics(new, &ort->dst);
		rt->rt6i_idev = ort->rt6i_idev;
		if (rt->rt6i_idev)
			in6_dev_hold(rt->rt6i_idev);
		rt->rt6i_expires = 0;

		ipv6_addr_copy(&rt->rt6i_gateway, &ort->rt6i_gateway);
		rt->rt6i_flags = ort->rt6i_flags & ~RTF_EXPIRES;
		rt->rt6i_metric = 0;

		memcpy(&rt->rt6i_dst, &ort->rt6i_dst, sizeof(struct rt6key));
#ifdef CONFIG_IPV6_SUBTREES
		memcpy(&rt->rt6i_src, &ort->rt6i_src, sizeof(struct rt6key));
#endif

		dst_free(new);
	}

	dst_release(dst_orig);
	return new ? new : ERR_PTR(-ENOMEM);
}

/*
 *	Destination cache support functions
 */

static struct dst_entry *ip6_dst_check(struct dst_entry *dst, u32 cookie)
{
	struct rt6_info *rt;

	rt = (struct rt6_info *) dst;

	if (rt->rt6i_node && (rt->rt6i_node->fn_sernum == cookie)) {
		if (rt->rt6i_peer_genid != rt6_peer_genid()) {
			if (!rt->rt6i_peer)
				rt6_bind_peer(rt, 0);
			rt->rt6i_peer_genid = rt6_peer_genid();
		}
		return dst;
	}
	return NULL;
}

static struct dst_entry *ip6_negative_advice(struct dst_entry *dst)
{
	struct rt6_info *rt = (struct rt6_info *) dst;

	if (rt) {
		if (rt->rt6i_flags & RTF_CACHE) {
			if (rt6_check_expired(rt)) {
				ip6_del_rt(rt);
				dst = NULL;
			}
		} else {
			dst_release(dst);
			dst = NULL;
		}
	}
	return dst;
}

static void ip6_link_failure(struct sk_buff *skb)
{
	struct rt6_info *rt;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0);

	rt = (struct rt6_info *) skb_dst(skb);
	if (rt) {
		if (rt->rt6i_flags&RTF_CACHE) {
			dst_set_expires(&rt->dst, 0);
			rt->rt6i_flags |= RTF_EXPIRES;
		} else if (rt->rt6i_node && (rt->rt6i_flags & RTF_DEFAULT))
			rt->rt6i_node->fn_sernum = -1;
	}
}

static void ip6_rt_update_pmtu(struct dst_entry *dst, u32 mtu)
{
	struct rt6_info *rt6 = (struct rt6_info*)dst;

	if (mtu < dst_mtu(dst) && rt6->rt6i_dst.plen == 128) {
		rt6->rt6i_flags |= RTF_MODIFIED;
		if (mtu < IPV6_MIN_MTU) {
			u32 features = dst_metric(dst, RTAX_FEATURES);
			mtu = IPV6_MIN_MTU;
			features |= RTAX_FEATURE_ALLFRAG;
			dst_metric_set(dst, RTAX_FEATURES, features);
		}
		dst_metric_set(dst, RTAX_MTU, mtu);
	}
}

static unsigned int ip6_default_advmss(const struct dst_entry *dst)
{
	struct net_device *dev = dst->dev;
	unsigned int mtu = dst_mtu(dst);
	struct net *net = dev_net(dev);

	mtu -= sizeof(struct ipv6hdr) + sizeof(struct tcphdr);

	if (mtu < net->ipv6.sysctl.ip6_rt_min_advmss)
		mtu = net->ipv6.sysctl.ip6_rt_min_advmss;

	/*
	 * Maximal non-jumbo IPv6 payload is IPV6_MAXPLEN and
	 * corresponding MSS is IPV6_MAXPLEN - tcp_header_size.
	 * IPV6_MAXPLEN is also valid and means: "any MSS,
	 * rely only on pmtu discovery"
	 */
	if (mtu > IPV6_MAXPLEN - sizeof(struct tcphdr))
		mtu = IPV6_MAXPLEN;
	return mtu;
}

static unsigned int ip6_default_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = IPV6_MIN_MTU;
	struct inet6_dev *idev;

	rcu_read_lock();
	idev = __in6_dev_get(dst->dev);
	if (idev)
		mtu = idev->cnf.mtu6;
	rcu_read_unlock();

	return mtu;
}

static struct dst_entry *icmp6_dst_gc_list;
static DEFINE_SPINLOCK(icmp6_dst_lock);

struct dst_entry *icmp6_dst_alloc(struct net_device *dev,
				  struct neighbour *neigh,
				  const struct in6_addr *addr)
{
	struct rt6_info *rt;
	struct inet6_dev *idev = in6_dev_get(dev);
	struct net *net = dev_net(dev);

	if (unlikely(idev == NULL))
		return NULL;

	rt = ip6_dst_alloc(&net->ipv6.ip6_dst_ops, dev, 0);
	if (unlikely(rt == NULL)) {
		in6_dev_put(idev);
		goto out;
	}

	if (neigh)
		neigh_hold(neigh);
	else {
		neigh = ndisc_get_neigh(dev, addr);
		if (IS_ERR(neigh))
			neigh = NULL;
	}

	rt->dst.flags |= DST_HOST;
	rt->dst.output  = ip6_output;
	dst_set_neighbour(&rt->dst, neigh);
	atomic_set(&rt->dst.__refcnt, 1);
	ipv6_addr_copy(&rt->rt6i_dst.addr, addr);
	rt->rt6i_dst.plen = 128;
	rt->rt6i_idev     = idev;
	dst_metric_set(&rt->dst, RTAX_HOPLIMIT, 255);

	spin_lock_bh(&icmp6_dst_lock);
	rt->dst.next = icmp6_dst_gc_list;
	icmp6_dst_gc_list = &rt->dst;
	spin_unlock_bh(&icmp6_dst_lock);

	fib6_force_start_gc(net);

out:
	return &rt->dst;
}

int icmp6_dst_gc(void)
{
	struct dst_entry *dst, **pprev;
	int more = 0;

	spin_lock_bh(&icmp6_dst_lock);
	pprev = &icmp6_dst_gc_list;

	while ((dst = *pprev) != NULL) {
		if (!atomic_read(&dst->__refcnt)) {
			*pprev = dst->next;
			dst_free(dst);
		} else {
			pprev = &dst->next;
			++more;
		}
	}

	spin_unlock_bh(&icmp6_dst_lock);

	return more;
}

static void icmp6_clean_all(int (*func)(struct rt6_info *rt, void *arg),
			    void *arg)
{
	struct dst_entry *dst, **pprev;

	spin_lock_bh(&icmp6_dst_lock);
	pprev = &icmp6_dst_gc_list;
	while ((dst = *pprev) != NULL) {
		struct rt6_info *rt = (struct rt6_info *) dst;
		if (func(rt, arg)) {
			*pprev = dst->next;
			dst_free(dst);
		} else {
			pprev = &dst->next;
		}
	}
	spin_unlock_bh(&icmp6_dst_lock);
}

static int ip6_dst_gc(struct dst_ops *ops)
{
	unsigned long now = jiffies;
	struct net *net = container_of(ops, struct net, ipv6.ip6_dst_ops);
	int rt_min_interval = net->ipv6.sysctl.ip6_rt_gc_min_interval;
	int rt_max_size = net->ipv6.sysctl.ip6_rt_max_size;
	int rt_elasticity = net->ipv6.sysctl.ip6_rt_gc_elasticity;
	int rt_gc_timeout = net->ipv6.sysctl.ip6_rt_gc_timeout;
	unsigned long rt_last_gc = net->ipv6.ip6_rt_last_gc;
	int entries;

	entries = dst_entries_get_fast(ops);
	if (time_after(rt_last_gc + rt_min_interval, now) &&
	    entries <= rt_max_size)
		goto out;

	net->ipv6.ip6_rt_gc_expire++;
	fib6_run_gc(net->ipv6.ip6_rt_gc_expire, net);
	net->ipv6.ip6_rt_last_gc = now;
	entries = dst_entries_get_slow(ops);
	if (entries < ops->gc_thresh)
		net->ipv6.ip6_rt_gc_expire = rt_gc_timeout>>1;
out:
	net->ipv6.ip6_rt_gc_expire -= net->ipv6.ip6_rt_gc_expire>>rt_elasticity;
	return entries > rt_max_size;
}

/* Clean host part of a prefix. Not necessary in radix tree,
   but results in cleaner routing tables.

   Remove it only when all the things will work!
 */

int ip6_dst_hoplimit(struct dst_entry *dst)
{
	int hoplimit = dst_metric_raw(dst, RTAX_HOPLIMIT);
	if (hoplimit == 0) {
		struct net_device *dev = dst->dev;
		struct inet6_dev *idev;

		rcu_read_lock();
		idev = __in6_dev_get(dev);
		if (idev)
			hoplimit = idev->cnf.hop_limit;
		else
			hoplimit = dev_net(dev)->ipv6.devconf_all->hop_limit;
		rcu_read_unlock();
	}
	return hoplimit;
}
EXPORT_SYMBOL(ip6_dst_hoplimit);

/*
 *
 */

int ip6_route_add(struct fib6_config *cfg)
{
	int err;
	struct net *net = cfg->fc_nlinfo.nl_net;
	struct rt6_info *rt = NULL;
	struct net_device *dev = NULL;
	struct inet6_dev *idev = NULL;
	struct fib6_table *table;
	int addr_type;

	if (cfg->fc_dst_len > 128 || cfg->fc_src_len > 128)
		return -EINVAL;
#ifndef CONFIG_IPV6_SUBTREES
	if (cfg->fc_src_len)
		return -EINVAL;
#endif
	if (cfg->fc_ifindex) {
		err = -ENODEV;
		dev = dev_get_by_index(net, cfg->fc_ifindex);
		if (!dev)
			goto out;
		idev = in6_dev_get(dev);
		if (!idev)
			goto out;
	}

	if (cfg->fc_metric == 0)
		cfg->fc_metric = IP6_RT_PRIO_USER;

	err = -ENOBUFS;
	if (NULL != cfg->fc_nlinfo.nlh &&
	    !(cfg->fc_nlinfo.nlh->nlmsg_flags&NLM_F_CREATE)) {
		table = fib6_get_table(net, cfg->fc_table);
		if (table == NULL) {
			printk(KERN_WARNING "IPv6: NLM_F_CREATE should be specified when creating new route\n");
			table = fib6_new_table(net, cfg->fc_table);
		}
	} else {
		table = fib6_new_table(net, cfg->fc_table);
	}
	if (table == NULL) {
		goto out;
	}

	rt = ip6_dst_alloc(&net->ipv6.ip6_dst_ops, NULL, DST_NOCOUNT);

	if (rt == NULL) {
		err = -ENOMEM;
		goto out;
	}

	rt->dst.obsolete = -1;
	rt->rt6i_expires = (cfg->fc_flags & RTF_EXPIRES) ?
				jiffies + clock_t_to_jiffies(cfg->fc_expires) :
				0;

	if (cfg->fc_protocol == RTPROT_UNSPEC)
		cfg->fc_protocol = RTPROT_BOOT;
	rt->rt6i_protocol = cfg->fc_protocol;

	addr_type = ipv6_addr_type(&cfg->fc_dst);

	if (addr_type & IPV6_ADDR_MULTICAST)
		rt->dst.input = ip6_mc_input;
	else if (cfg->fc_flags & RTF_LOCAL)
		rt->dst.input = ip6_input;
	else
		rt->dst.input = ip6_forward;

	rt->dst.output = ip6_output;

	ipv6_addr_prefix(&rt->rt6i_dst.addr, &cfg->fc_dst, cfg->fc_dst_len);
	rt->rt6i_dst.plen = cfg->fc_dst_len;
	if (rt->rt6i_dst.plen == 128)
	       rt->dst.flags |= DST_HOST;

	if (!(rt->dst.flags & DST_HOST) && cfg->fc_mx) {
		u32 *metrics = kzalloc(sizeof(u32) * RTAX_MAX, GFP_KERNEL);
		if (!metrics) {
			err = -ENOMEM;
			goto out;
		}
		dst_init_metrics(&rt->dst, metrics, 0);
	}
#ifdef CONFIG_IPV6_SUBTREES
	ipv6_addr_prefix(&rt->rt6i_src.addr, &cfg->fc_src, cfg->fc_src_len);
	rt->rt6i_src.plen = cfg->fc_src_len;
#endif

	rt->rt6i_metric = cfg->fc_metric;

	/* We cannot add true routes via loopback here,
	   they would result in kernel looping; promote them to reject routes
	 */
	if ((cfg->fc_flags & RTF_REJECT) ||
	    (dev && (dev->flags&IFF_LOOPBACK) && !(addr_type&IPV6_ADDR_LOOPBACK)
					      && !(cfg->fc_flags&RTF_LOCAL))) {
		/* hold loopback dev/idev if we haven't done so. */
		if (dev != net->loopback_dev) {
			if (dev) {
				dev_put(dev);
				in6_dev_put(idev);
			}
			dev = net->loopback_dev;
			dev_hold(dev);
			idev = in6_dev_get(dev);
			if (!idev) {
				err = -ENODEV;
				goto out;
			}
		}
		rt->dst.output = ip6_pkt_discard_out;
		rt->dst.input = ip6_pkt_discard;
		rt->dst.error = -ENETUNREACH;
		rt->rt6i_flags = RTF_REJECT|RTF_NONEXTHOP;
		goto install_route;
	}

	if (cfg->fc_flags & RTF_GATEWAY) {
		const struct in6_addr *gw_addr;
		int gwa_type;

		gw_addr = &cfg->fc_gateway;
		ipv6_addr_copy(&rt->rt6i_gateway, gw_addr);
		gwa_type = ipv6_addr_type(gw_addr);

		if (gwa_type != (IPV6_ADDR_LINKLOCAL|IPV6_ADDR_UNICAST)) {
			struct rt6_info *grt;

			/* IPv6 strictly inhibits using not link-local
			   addresses as nexthop address.
			   Otherwise, router will not able to send redirects.
			   It is very good, but in some (rare!) circumstances
			   (SIT, PtP, NBMA NOARP links) it is handy to allow
			   some exceptions. --ANK
			 */
			err = -EINVAL;
			if (!(gwa_type&IPV6_ADDR_UNICAST))
				goto out;

			grt = rt6_lookup(net, gw_addr, NULL, cfg->fc_ifindex, 1);

			err = -EHOSTUNREACH;
			if (grt == NULL)
				goto out;
			if (dev) {
				if (dev != grt->rt6i_dev) {
					dst_release(&grt->dst);
					goto out;
				}
			} else {
				dev = grt->rt6i_dev;
				idev = grt->rt6i_idev;
				dev_hold(dev);
				in6_dev_hold(grt->rt6i_idev);
			}
			if (!(grt->rt6i_flags&RTF_GATEWAY))
				err = 0;
			dst_release(&grt->dst);

			if (err)
				goto out;
		}
		err = -EINVAL;
		if (dev == NULL || (dev->flags&IFF_LOOPBACK))
			goto out;
	}

	err = -ENODEV;
	if (dev == NULL)
		goto out;

	if (!ipv6_addr_any(&cfg->fc_prefsrc)) {
		if (!ipv6_chk_addr(net, &cfg->fc_prefsrc, dev, 0)) {
			err = -EINVAL;
			goto out;
		}
		ipv6_addr_copy(&rt->rt6i_prefsrc.addr, &cfg->fc_prefsrc);
		rt->rt6i_prefsrc.plen = 128;
	} else
		rt->rt6i_prefsrc.plen = 0;

	if (cfg->fc_flags & (RTF_GATEWAY | RTF_NONEXTHOP)) {
		struct neighbour *n = __neigh_lookup_errno(&nd_tbl, &rt->rt6i_gateway, dev);
		if (IS_ERR(n)) {
			err = PTR_ERR(n);
			goto out;
		}
		dst_set_neighbour(&rt->dst, n);
	}

	rt->rt6i_flags = cfg->fc_flags;

install_route:
	if (cfg->fc_mx) {
		struct nlattr *nla;
		int remaining;

		nla_for_each_attr(nla, cfg->fc_mx, cfg->fc_mx_len, remaining) {
			int type = nla_type(nla);

			if (type) {
				if (type > RTAX_MAX) {
					err = -EINVAL;
					goto out;
				}

				dst_metric_set(&rt->dst, type, nla_get_u32(nla));
			}
		}
	}

	rt->dst.dev = dev;
	rt->rt6i_idev = idev;
	rt->rt6i_table = table;

	cfg->fc_nlinfo.nl_net = dev_net(dev);

	return __ip6_ins_rt(rt, &cfg->fc_nlinfo);

out:
	if (dev)
		dev_put(dev);
	if (idev)
		in6_dev_put(idev);
	if (rt)
		dst_free(&rt->dst);
	return err;
}

static int __ip6_del_rt(struct rt6_info *rt, struct nl_info *info)
{
	int err;
	struct fib6_table *table;
	struct net *net = dev_net(rt->rt6i_dev);

	if (rt == net->ipv6.ip6_null_entry)
		return -ENOENT;

	table = rt->rt6i_table;
	write_lock_bh(&table->tb6_lock);

	err = fib6_del(rt, info);
	dst_release(&rt->dst);

	write_unlock_bh(&table->tb6_lock);

	return err;
}

int ip6_del_rt(struct rt6_info *rt)
{
	struct nl_info info = {
		.nl_net = dev_net(rt->rt6i_dev),
	};
	return __ip6_del_rt(rt, &info);
}

static int ip6_route_del(struct fib6_config *cfg)
{
	struct fib6_table *table;
	struct fib6_node *fn;
	struct rt6_info *rt;
	int err = -ESRCH;

	table = fib6_get_table(cfg->fc_nlinfo.nl_net, cfg->fc_table);
	if (table == NULL)
		return err;

	read_lock_bh(&table->tb6_lock);

	fn = fib6_locate(&table->tb6_root,
			 &cfg->fc_dst, cfg->fc_dst_len,
			 &cfg->fc_src, cfg->fc_src_len);

	if (fn) {
		for (rt = fn->leaf; rt; rt = rt->dst.rt6_next) {
			if (cfg->fc_ifindex &&
			    (rt->rt6i_dev == NULL ||
			     rt->rt6i_dev->ifindex != cfg->fc_ifindex))
				continue;
			if (cfg->fc_flags & RTF_GATEWAY &&
			    !ipv6_addr_equal(&cfg->fc_gateway, &rt->rt6i_gateway))
				continue;
			if (cfg->fc_metric && cfg->fc_metric != rt->rt6i_metric)
				continue;
			dst_hold(&rt->dst);
			read_unlock_bh(&table->tb6_lock);

			return __ip6_del_rt(rt, &cfg->fc_nlinfo);
		}
	}
	read_unlock_bh(&table->tb6_lock);

	return err;
}

/*
 *	Handle redirects
 */
struct ip6rd_flowi {
	struct flowi6 fl6;
	struct in6_addr gateway;
};

static struct rt6_info *__ip6_route_redirect(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     int flags)
{
	struct ip6rd_flowi *rdfl = (struct ip6rd_flowi *)fl6;
	struct rt6_info *rt;
	struct fib6_node *fn;

	/*
	 * Get the "current" route for this destination and
	 * check if the redirect has come from approriate router.
	 *
	 * RFC 2461 specifies that redirects should only be
	 * accepted if they come from the nexthop to the target.
	 * Due to the way the routes are chosen, this notion
	 * is a bit fuzzy and one might need to check all possible
	 * routes.
	 */

	read_lock_bh(&table->tb6_lock);
	fn = fib6_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
restart:
	for (rt = fn->leaf; rt; rt = rt->dst.rt6_next) {
		/*
		 * Current route is on-link; redirect is always invalid.
		 *
		 * Seems, previous statement is not true. It could
		 * be node, which looks for us as on-link (f.e. proxy ndisc)
		 * But then router serving it might decide, that we should
		 * know truth 8)8) --ANK (980726).
		 */
		if (rt6_check_expired(rt))
			continue;
		if (!(rt->rt6i_flags & RTF_GATEWAY))
			continue;
		if (fl6->flowi6_oif != rt->rt6i_dev->ifindex)
			continue;
		if (!ipv6_addr_equal(&rdfl->gateway, &rt->rt6i_gateway))
			continue;
		break;
	}

	if (!rt)
		rt = net->ipv6.ip6_null_entry;
	BACKTRACK(net, &fl6->saddr);
out:
	dst_hold(&rt->dst);

	read_unlock_bh(&table->tb6_lock);

	return rt;
};

static struct rt6_info *ip6_route_redirect(const struct in6_addr *dest,
					   const struct in6_addr *src,
					   const struct in6_addr *gateway,
					   struct net_device *dev)
{
	int flags = RT6_LOOKUP_F_HAS_SADDR;
	struct net *net = dev_net(dev);
	struct ip6rd_flowi rdfl = {
		.fl6 = {
			.flowi6_oif = dev->ifindex,
			.daddr = *dest,
			.saddr = *src,
		},
	};

	ipv6_addr_copy(&rdfl.gateway, gateway);

	if (rt6_need_strict(dest))
		flags |= RT6_LOOKUP_F_IFACE;

	return (struct rt6_info *)fib6_rule_lookup(net, &rdfl.fl6,
						   flags, __ip6_route_redirect);
}

void rt6_redirect(const struct in6_addr *dest, const struct in6_addr *src,
		  const struct in6_addr *saddr,
		  struct neighbour *neigh, u8 *lladdr, int on_link)
{
	struct rt6_info *rt, *nrt = NULL;
	struct netevent_redirect netevent;
	struct net *net = dev_net(neigh->dev);

	rt = ip6_route_redirect(dest, src, saddr, neigh->dev);

	if (rt == net->ipv6.ip6_null_entry) {
		if (net_ratelimit())
			printk(KERN_DEBUG "rt6_redirect: source isn't a valid nexthop "
			       "for redirect target\n");
		goto out;
	}

	/*
	 *	We have finally decided to accept it.
	 */

	neigh_update(neigh, lladdr, NUD_STALE,
		     NEIGH_UPDATE_F_WEAK_OVERRIDE|
		     NEIGH_UPDATE_F_OVERRIDE|
		     (on_link ? 0 : (NEIGH_UPDATE_F_OVERRIDE_ISROUTER|
				     NEIGH_UPDATE_F_ISROUTER))
		     );

	/*
	 * Redirect received -> path was valid.
	 * Look, redirects are sent only in response to data packets,
	 * so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm(&rt->dst);

	/* Duplicate redirect: silently ignore. */
	if (neigh == dst_get_neighbour_raw(&rt->dst))
		goto out;

	nrt = ip6_rt_copy(rt, dest);
	if (nrt == NULL)
		goto out;

	nrt->rt6i_flags = RTF_GATEWAY|RTF_UP|RTF_DYNAMIC|RTF_CACHE;
	if (on_link)
		nrt->rt6i_flags &= ~RTF_GATEWAY;

	ipv6_addr_copy(&nrt->rt6i_gateway, (struct in6_addr*)neigh->primary_key);
	dst_set_neighbour(&nrt->dst, neigh_clone(neigh));

	if (ip6_ins_rt(nrt))
		goto out;

	netevent.old = &rt->dst;
	netevent.new = &nrt->dst;
	call_netevent_notifiers(NETEVENT_REDIRECT, &netevent);

	if (rt->rt6i_flags&RTF_CACHE) {
		ip6_del_rt(rt);
		return;
	}

out:
	dst_release(&rt->dst);
}

/*
 *	Handle ICMP "packet too big" messages
 *	i.e. Path MTU discovery
 */

static void rt6_do_pmtu_disc(const struct in6_addr *daddr, const struct in6_addr *saddr,
			     struct net *net, u32 pmtu, int ifindex)
{
	struct rt6_info *rt, *nrt;
	int allfrag = 0;
again:
	rt = rt6_lookup(net, daddr, saddr, ifindex, 0);
	if (rt == NULL)
		return;

	if (rt6_check_expired(rt)) {
		ip6_del_rt(rt);
		goto again;
	}

	if (pmtu >= dst_mtu(&rt->dst))
		goto out;

	if (pmtu < IPV6_MIN_MTU) {
		/*
		 * According to RFC2460, PMTU is set to the IPv6 Minimum Link
		 * MTU (1280) and a fragment header should always be included
		 * after a node receiving Too Big message reporting PMTU is
		 * less than the IPv6 Minimum Link MTU.
		 */
		pmtu = IPV6_MIN_MTU;
		allfrag = 1;
	}

	/* New mtu received -> path was valid.
	   They are sent only in response to data packets,
	   so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm(&rt->dst);

	/* Host route. If it is static, it would be better
	   not to override it, but add new one, so that
	   when cache entry will expire old pmtu
	   would return automatically.
	 */
	if (rt->rt6i_flags & RTF_CACHE) {
		dst_metric_set(&rt->dst, RTAX_MTU, pmtu);
		if (allfrag) {
			u32 features = dst_metric(&rt->dst, RTAX_FEATURES);
			features |= RTAX_FEATURE_ALLFRAG;
			dst_metric_set(&rt->dst, RTAX_FEATURES, features);
		}
		dst_set_expires(&rt->dst, net->ipv6.sysctl.ip6_rt_mtu_expires);
		rt->rt6i_flags |= RTF_MODIFIED|RTF_EXPIRES;
		goto out;
	}

	/* Network route.
	   Two cases are possible:
	   1. It is connected route. Action: COW
	   2. It is gatewayed route or NONEXTHOP route. Action: clone it.
	 */
	if (!dst_get_neighbour_raw(&rt->dst) && !(rt->rt6i_flags & RTF_NONEXTHOP))
		nrt = rt6_alloc_cow(rt, daddr, saddr);
	else
		nrt = rt6_alloc_clone(rt, daddr);

	if (nrt) {
		dst_metric_set(&nrt->dst, RTAX_MTU, pmtu);
		if (allfrag) {
			u32 features = dst_metric(&nrt->dst, RTAX_FEATURES);
			features |= RTAX_FEATURE_ALLFRAG;
			dst_metric_set(&nrt->dst, RTAX_FEATURES, features);
		}

		/* According to RFC 1981, detecting PMTU increase shouldn't be
		 * happened within 5 mins, the recommended timer is 10 mins.
		 * Here this route expiration time is set to ip6_rt_mtu_expires
		 * which is 10 mins. After 10 mins the decreased pmtu is expired
		 * and detecting PMTU increase will be automatically happened.
		 */
		dst_set_expires(&nrt->dst, net->ipv6.sysctl.ip6_rt_mtu_expires);
		nrt->rt6i_flags |= RTF_DYNAMIC|RTF_EXPIRES;

		ip6_ins_rt(nrt);
	}
out:
	dst_release(&rt->dst);
}

void rt6_pmtu_discovery(const struct in6_addr *daddr, const struct in6_addr *saddr,
			struct net_device *dev, u32 pmtu)
{
	struct net *net = dev_net(dev);

	/*
	 * RFC 1981 states that a node "MUST reduce the size of the packets it
	 * is sending along the path" that caused the Packet Too Big message.
	 * Since it's not possible in the general case to determine which
	 * interface was used to send the original packet, we update the MTU
	 * on the interface that will be used to send future packets. We also
	 * update the MTU on the interface that received the Packet Too Big in
	 * case the original packet was forced out that interface with
	 * SO_BINDTODEVICE or similar. This is the next best thing to the
	 * correct behaviour, which would be to update the MTU on all
	 * interfaces.
	 */
	rt6_do_pmtu_disc(daddr, saddr, net, pmtu, 0);
	rt6_do_pmtu_disc(daddr, saddr, net, pmtu, dev->ifindex);
}

/*
 *	Misc support functions
 */

static struct rt6_info *ip6_rt_copy(const struct rt6_info *ort,
				    const struct in6_addr *dest)
{
	struct net *net = dev_net(ort->rt6i_dev);
	struct rt6_info *rt = ip6_dst_alloc(&net->ipv6.ip6_dst_ops,
					    ort->dst.dev, 0);

	if (rt) {
		rt->dst.input = ort->dst.input;
		rt->dst.output = ort->dst.output;
		rt->dst.flags |= DST_HOST;

		ipv6_addr_copy(&rt->rt6i_dst.addr, dest);
		rt->rt6i_dst.plen = 128;
		dst_copy_metrics(&rt->dst, &ort->dst);
		rt->dst.error = ort->dst.error;
		rt->rt6i_idev = ort->rt6i_idev;
		if (rt->rt6i_idev)
			in6_dev_hold(rt->rt6i_idev);
		rt->dst.lastuse = jiffies;
		rt->rt6i_expires = 0;

		ipv6_addr_copy(&rt->rt6i_gateway, &ort->rt6i_gateway);
		rt->rt6i_flags = ort->rt6i_flags & ~RTF_EXPIRES;
		rt->rt6i_metric = 0;

#ifdef CONFIG_IPV6_SUBTREES
		memcpy(&rt->rt6i_src, &ort->rt6i_src, sizeof(struct rt6key));
#endif
		memcpy(&rt->rt6i_prefsrc, &ort->rt6i_prefsrc, sizeof(struct rt6key));
		rt->rt6i_table = ort->rt6i_table;
	}
	return rt;
}

#ifdef CONFIG_IPV6_ROUTE_INFO
static struct rt6_info *rt6_get_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr, int ifindex)
{
	struct fib6_node *fn;
	struct rt6_info *rt = NULL;
	struct fib6_table *table;

	table = fib6_get_table(net, RT6_TABLE_INFO);
	if (table == NULL)
		return NULL;

	write_lock_bh(&table->tb6_lock);
	fn = fib6_locate(&table->tb6_root, prefix ,prefixlen, NULL, 0);
	if (!fn)
		goto out;

	for (rt = fn->leaf; rt; rt = rt->dst.rt6_next) {
		if (rt->rt6i_dev->ifindex != ifindex)
			continue;
		if ((rt->rt6i_flags & (RTF_ROUTEINFO|RTF_GATEWAY)) != (RTF_ROUTEINFO|RTF_GATEWAY))
			continue;
		if (!ipv6_addr_equal(&rt->rt6i_gateway, gwaddr))
			continue;
		dst_hold(&rt->dst);
		break;
	}
out:
	write_unlock_bh(&table->tb6_lock);
	return rt;
}

static struct rt6_info *rt6_add_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr, int ifindex,
					   unsigned pref)
{
	struct fib6_config cfg = {
		.fc_table	= RT6_TABLE_INFO,
		.fc_metric	= IP6_RT_PRIO_USER,
		.fc_ifindex	= ifindex,
		.fc_dst_len	= prefixlen,
		.fc_flags	= RTF_GATEWAY | RTF_ADDRCONF | RTF_ROUTEINFO |
				  RTF_UP | RTF_PREF(pref),
		.fc_nlinfo.pid = 0,
		.fc_nlinfo.nlh = NULL,
		.fc_nlinfo.nl_net = net,
	};

	ipv6_addr_copy(&cfg.fc_dst, prefix);
	ipv6_addr_copy(&cfg.fc_gateway, gwaddr);

	/* We should treat it as a default route if prefix length is 0. */
	if (!prefixlen)
		cfg.fc_flags |= RTF_DEFAULT;

	ip6_route_add(&cfg);

	return rt6_get_route_info(net, prefix, prefixlen, gwaddr, ifindex);
}
#endif

struct rt6_info *rt6_get_dflt_router(const struct in6_addr *addr, struct net_device *dev)
{
	struct rt6_info *rt;
	struct fib6_table *table;

	table = fib6_get_table(dev_net(dev), RT6_TABLE_DFLT);
	if (table == NULL)
		return NULL;

	write_lock_bh(&table->tb6_lock);
	for (rt = table->tb6_root.leaf; rt; rt=rt->dst.rt6_next) {
		if (dev == rt->rt6i_dev &&
		    ((rt->rt6i_flags & (RTF_ADDRCONF | RTF_DEFAULT)) == (RTF_ADDRCONF | RTF_DEFAULT)) &&
		    ipv6_addr_equal(&rt->rt6i_gateway, addr))
			break;
	}
	if (rt)
		dst_hold(&rt->dst);
	write_unlock_bh(&table->tb6_lock);
	return rt;
}

struct rt6_info *rt6_add_dflt_router(const struct in6_addr *gwaddr,
				     struct net_device *dev,
				     unsigned int pref)
{
	struct fib6_config cfg = {
		.fc_table	= RT6_TABLE_DFLT,
		.fc_metric	= IP6_RT_PRIO_USER,
		.fc_ifindex	= dev->ifindex,
		.fc_flags	= RTF_GATEWAY | RTF_ADDRCONF | RTF_DEFAULT |
				  RTF_UP | RTF_EXPIRES | RTF_PREF(pref),
		.fc_nlinfo.pid = 0,
		.fc_nlinfo.nlh = NULL,
		.fc_nlinfo.nl_net = dev_net(dev),
	};

	ipv6_addr_copy(&cfg.fc_gateway, gwaddr);

	ip6_route_add(&cfg);

	return rt6_get_dflt_router(gwaddr, dev);
}

void rt6_purge_dflt_routers(struct net *net)
{
	struct rt6_info *rt;
	struct fib6_table *table;

	/* NOTE: Keep consistent with rt6_get_dflt_router */
	table = fib6_get_table(net, RT6_TABLE_DFLT);
	if (table == NULL)
		return;

restart:
	read_lock_bh(&table->tb6_lock);
	for (rt = table->tb6_root.leaf; rt; rt = rt->dst.rt6_next) {
		if (rt->rt6i_flags & (RTF_DEFAULT | RTF_ADDRCONF)) {
			dst_hold(&rt->dst);
			read_unlock_bh(&table->tb6_lock);
			ip6_del_rt(rt);
			goto restart;
		}
	}
	read_unlock_bh(&table->tb6_lock);
}

static void rtmsg_to_fib6_config(struct net *net,
				 struct in6_rtmsg *rtmsg,
				 struct fib6_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->fc_table = RT6_TABLE_MAIN;
	cfg->fc_ifindex = rtmsg->rtmsg_ifindex;
	cfg->fc_metric = rtmsg->rtmsg_metric;
	cfg->fc_expires = rtmsg->rtmsg_info;
	cfg->fc_dst_len = rtmsg->rtmsg_dst_len;
	cfg->fc_src_len = rtmsg->rtmsg_src_len;
	cfg->fc_flags = rtmsg->rtmsg_flags;

	cfg->fc_nlinfo.nl_net = net;

	ipv6_addr_copy(&cfg->fc_dst, &rtmsg->rtmsg_dst);
	ipv6_addr_copy(&cfg->fc_src, &rtmsg->rtmsg_src);
	ipv6_addr_copy(&cfg->fc_gateway, &rtmsg->rtmsg_gateway);
}

int ipv6_route_ioctl(struct net *net, unsigned int cmd, void __user *arg)
{
	struct fib6_config cfg;
	struct in6_rtmsg rtmsg;
	int err;

	switch(cmd) {
	case SIOCADDRT:		/* Add a route */
	case SIOCDELRT:		/* Delete a route */
		if (!capable(CAP_NET_ADMIN))
			return -EPERM;
		err = copy_from_user(&rtmsg, arg,
				     sizeof(struct in6_rtmsg));
		if (err)
			return -EFAULT;

		rtmsg_to_fib6_config(net, &rtmsg, &cfg);

		rtnl_lock();
		switch (cmd) {
		case SIOCADDRT:
			err = ip6_route_add(&cfg);
			break;
		case SIOCDELRT:
			err = ip6_route_del(&cfg);
			break;
		default:
			err = -EINVAL;
		}
		rtnl_unlock();

		return err;
	}

	return -EINVAL;
}

/*
 *	Drop the packet on the floor
 */

static int ip6_pkt_drop(struct sk_buff *skb, u8 code, int ipstats_mib_noroutes)
{
	int type;
	struct dst_entry *dst = skb_dst(skb);
	switch (ipstats_mib_noroutes) {
	case IPSTATS_MIB_INNOROUTES:
		type = ipv6_addr_type(&ipv6_hdr(skb)->daddr);
		if (type == IPV6_ADDR_ANY) {
			IP6_INC_STATS(dev_net(dst->dev), ip6_dst_idev(dst),
				      IPSTATS_MIB_INADDRERRORS);
			break;
		}
		/* FALLTHROUGH */
	case IPSTATS_MIB_OUTNOROUTES:
		IP6_INC_STATS(dev_net(dst->dev), ip6_dst_idev(dst),
			      ipstats_mib_noroutes);
		break;
	}
	icmpv6_send(skb, ICMPV6_DEST_UNREACH, code, 0);
	kfree_skb(skb);
	return 0;
}

static int ip6_pkt_discard(struct sk_buff *skb)
{
	return ip6_pkt_drop(skb, ICMPV6_NOROUTE, IPSTATS_MIB_INNOROUTES);
}

static int ip6_pkt_discard_out(struct sk_buff *skb)
{
	skb->dev = skb_dst(skb)->dev;
	return ip6_pkt_drop(skb, ICMPV6_NOROUTE, IPSTATS_MIB_OUTNOROUTES);
}

#ifdef CONFIG_IPV6_MULTIPLE_TABLES

static int ip6_pkt_prohibit(struct sk_buff *skb)
{
	return ip6_pkt_drop(skb, ICMPV6_ADM_PROHIBITED, IPSTATS_MIB_INNOROUTES);
}

static int ip6_pkt_prohibit_out(struct sk_buff *skb)
{
	skb->dev = skb_dst(skb)->dev;
	return ip6_pkt_drop(skb, ICMPV6_ADM_PROHIBITED, IPSTATS_MIB_OUTNOROUTES);
}

#endif

/*
 *	Allocate a dst for local (unicast / anycast) address.
 */

struct rt6_info *addrconf_dst_alloc(struct inet6_dev *idev,
				    const struct in6_addr *addr,
				    int anycast)
{
	struct net *net = dev_net(idev->dev);
	struct rt6_info *rt = ip6_dst_alloc(&net->ipv6.ip6_dst_ops,
					    net->loopback_dev, 0);
	struct neighbour *neigh;

	if (rt == NULL) {
		if (net_ratelimit())
			pr_warning("IPv6:  Maximum number of routes reached,"
				   " consider increasing route/max_size.\n");
		return ERR_PTR(-ENOMEM);
	}

	in6_dev_hold(idev);

	rt->dst.flags |= DST_HOST;
	rt->dst.input = ip6_input;
	rt->dst.output = ip6_output;
	rt->rt6i_idev = idev;
	rt->dst.obsolete = -1;

	rt->rt6i_flags = RTF_UP | RTF_NONEXTHOP;
	if (anycast)
		rt->rt6i_flags |= RTF_ANYCAST;
	else
		rt->rt6i_flags |= RTF_LOCAL;
	neigh = ndisc_get_neigh(rt->rt6i_dev, &rt->rt6i_gateway);
	if (IS_ERR(neigh)) {
		dst_free(&rt->dst);

		return ERR_CAST(neigh);
	}
	dst_set_neighbour(&rt->dst, neigh);

	ipv6_addr_copy(&rt->rt6i_dst.addr, addr);
	rt->rt6i_dst.plen = 128;
	rt->rt6i_table = fib6_get_table(net, RT6_TABLE_LOCAL);

	atomic_set(&rt->dst.__refcnt, 1);

	return rt;
}

int ip6_route_get_saddr(struct net *net,
			struct rt6_info *rt,
			const struct in6_addr *daddr,
			unsigned int prefs,
			struct in6_addr *saddr)
{
	struct inet6_dev *idev = ip6_dst_idev((struct dst_entry*)rt);
	int err = 0;
	if (rt->rt6i_prefsrc.plen)
		ipv6_addr_copy(saddr, &rt->rt6i_prefsrc.addr);
	else
		err = ipv6_dev_get_saddr(net, idev ? idev->dev : NULL,
					 daddr, prefs, saddr);
	return err;
}

/* remove deleted ip from prefsrc entries */
struct arg_dev_net_ip {
	struct net_device *dev;
	struct net *net;
	struct in6_addr *addr;
};

static int fib6_remove_prefsrc(struct rt6_info *rt, void *arg)
{
	struct net_device *dev = ((struct arg_dev_net_ip *)arg)->dev;
	struct net *net = ((struct arg_dev_net_ip *)arg)->net;
	struct in6_addr *addr = ((struct arg_dev_net_ip *)arg)->addr;

	if (((void *)rt->rt6i_dev == dev || dev == NULL) &&
	    rt != net->ipv6.ip6_null_entry &&
	    ipv6_addr_equal(addr, &rt->rt6i_prefsrc.addr)) {
		/* remove prefsrc entry */
		rt->rt6i_prefsrc.plen = 0;
	}
	return 0;
}

void rt6_remove_prefsrc(struct inet6_ifaddr *ifp)
{
	struct net *net = dev_net(ifp->idev->dev);
	struct arg_dev_net_ip adni = {
		.dev = ifp->idev->dev,
		.net = net,
		.addr = &ifp->addr,
	};
	fib6_clean_all(net, fib6_remove_prefsrc, 0, &adni);
}

struct arg_dev_net {
	struct net_device *dev;
	struct net *net;
};

static int fib6_ifdown(struct rt6_info *rt, void *arg)
{
	const struct arg_dev_net *adn = arg;
	const struct net_device *dev = adn->dev;

	if ((rt->rt6i_dev == dev || dev == NULL) &&
	    rt != adn->net->ipv6.ip6_null_entry) {
		RT6_TRACE("deleted by ifdown %p\n", rt);
		return -1;
	}
	return 0;
}

void rt6_ifdown(struct net *net, struct net_device *dev)
{
	struct arg_dev_net adn = {
		.dev = dev,
		.net = net,
	};

	fib6_clean_all(net, fib6_ifdown, 0, &adn);
	icmp6_clean_all(fib6_ifdown, &adn);
}

struct rt6_mtu_change_arg
{
	struct net_device *dev;
	unsigned mtu;
};

static int rt6_mtu_change_route(struct rt6_info *rt, void *p_arg)
{
	struct rt6_mtu_change_arg *arg = (struct rt6_mtu_change_arg *) p_arg;
	struct inet6_dev *idev;

	/* In IPv6 pmtu discovery is not optional,
	   so that RTAX_MTU lock cannot disable it.
	   We still use this lock to block changes
	   caused by addrconf/ndisc.
	*/

	idev = __in6_dev_get(arg->dev);
	if (idev == NULL)
		return 0;

	/* For administrative MTU increase, there is no way to discover
	   IPv6 PMTU increase, so PMTU increase should be updated here.
	   Since RFC 1981 doesn't include administrative MTU increase
	   update PMTU increase is a MUST. (i.e. jumbo frame)
	 */
	/*
	   If new MTU is less than route PMTU, this new MTU will be the
	   lowest MTU in the path, update the route PMTU to reflect PMTU
	   decreases; if new MTU is greater than route PMTU, and the
	   old MTU is the lowest MTU in the path, update the route PMTU
	   to reflect the increase. In this case if the other nodes' MTU
	   also have the lowest MTU, TOO BIG MESSAGE will be lead to
	   PMTU discouvery.
	 */
	if (rt->rt6i_dev == arg->dev &&
	    !dst_metric_locked(&rt->dst, RTAX_MTU) &&
	    (dst_mtu(&rt->dst) >= arg->mtu ||
	     (dst_mtu(&rt->dst) < arg->mtu &&
	      dst_mtu(&rt->dst) == idev->cnf.mtu6))) {
		dst_metric_set(&rt->dst, RTAX_MTU, arg->mtu);
	}
	return 0;
}

void rt6_mtu_change(struct net_device *dev, unsigned mtu)
{
	struct rt6_mtu_change_arg arg = {
		.dev = dev,
		.mtu = mtu,
	};

	fib6_clean_all(dev_net(dev), rt6_mtu_change_route, 0, &arg);
}

static const struct nla_policy rtm_ipv6_policy[RTA_MAX+1] = {
	[RTA_GATEWAY]           = { .len = sizeof(struct in6_addr) },
	[RTA_OIF]               = { .type = NLA_U32 },
	[RTA_IIF]		= { .type = NLA_U32 },
	[RTA_PRIORITY]          = { .type = NLA_U32 },
	[RTA_METRICS]           = { .type = NLA_NESTED },
};

static int rtm_to_fib6_config(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct fib6_config *cfg)
{
	struct rtmsg *rtm;
	struct nlattr *tb[RTA_MAX+1];
	int err;

	err = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, rtm_ipv6_policy);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	rtm = nlmsg_data(nlh);
	memset(cfg, 0, sizeof(*cfg));

	cfg->fc_table = rtm->rtm_table;
	cfg->fc_dst_len = rtm->rtm_dst_len;
	cfg->fc_src_len = rtm->rtm_src_len;
	cfg->fc_flags = RTF_UP;
	cfg->fc_protocol = rtm->rtm_protocol;

	if (rtm->rtm_type == RTN_UNREACHABLE)
		cfg->fc_flags |= RTF_REJECT;

	if (rtm->rtm_type == RTN_LOCAL)
		cfg->fc_flags |= RTF_LOCAL;

	cfg->fc_nlinfo.pid = NETLINK_CB(skb).pid;
	cfg->fc_nlinfo.nlh = nlh;
	cfg->fc_nlinfo.nl_net = sock_net(skb->sk);

	if (tb[RTA_GATEWAY]) {
		nla_memcpy(&cfg->fc_gateway, tb[RTA_GATEWAY], 16);
		cfg->fc_flags |= RTF_GATEWAY;
	}

	if (tb[RTA_DST]) {
		int plen = (rtm->rtm_dst_len + 7) >> 3;

		if (nla_len(tb[RTA_DST]) < plen)
			goto errout;

		nla_memcpy(&cfg->fc_dst, tb[RTA_DST], plen);
	}

	if (tb[RTA_SRC]) {
		int plen = (rtm->rtm_src_len + 7) >> 3;

		if (nla_len(tb[RTA_SRC]) < plen)
			goto errout;

		nla_memcpy(&cfg->fc_src, tb[RTA_SRC], plen);
	}

	if (tb[RTA_PREFSRC])
		nla_memcpy(&cfg->fc_prefsrc, tb[RTA_PREFSRC], 16);

	if (tb[RTA_OIF])
		cfg->fc_ifindex = nla_get_u32(tb[RTA_OIF]);

	if (tb[RTA_PRIORITY])
		cfg->fc_metric = nla_get_u32(tb[RTA_PRIORITY]);

	if (tb[RTA_METRICS]) {
		cfg->fc_mx = nla_data(tb[RTA_METRICS]);
		cfg->fc_mx_len = nla_len(tb[RTA_METRICS]);
	}

	if (tb[RTA_TABLE])
		cfg->fc_table = nla_get_u32(tb[RTA_TABLE]);

	err = 0;
errout:
	return err;
}

static int inet6_rtm_delroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct fib6_config cfg;
	int err;

	err = rtm_to_fib6_config(skb, nlh, &cfg);
	if (err < 0)
		return err;

	return ip6_route_del(&cfg);
}

static int inet6_rtm_newroute(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct fib6_config cfg;
	int err;

	err = rtm_to_fib6_config(skb, nlh, &cfg);
	if (err < 0)
		return err;

	return ip6_route_add(&cfg);
}

static inline size_t rt6_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct rtmsg))
	       + nla_total_size(16) /* RTA_SRC */
	       + nla_total_size(16) /* RTA_DST */
	       + nla_total_size(16) /* RTA_GATEWAY */
	       + nla_total_size(16) /* RTA_PREFSRC */
	       + nla_total_size(4) /* RTA_TABLE */
	       + nla_total_size(4) /* RTA_IIF */
	       + nla_total_size(4) /* RTA_OIF */
	       + nla_total_size(4) /* RTA_PRIORITY */
	       + RTAX_MAX * nla_total_size(4) /* RTA_METRICS */
	       + nla_total_size(sizeof(struct rta_cacheinfo));
}

static int rt6_fill_node(struct net *net,
			 struct sk_buff *skb, struct rt6_info *rt,
			 struct in6_addr *dst, struct in6_addr *src,
			 int iif, int type, u32 pid, u32 seq,
			 int prefix, int nowait, unsigned int flags)
{
	struct rtmsg *rtm;
	struct nlmsghdr *nlh;
	long expires;
	u32 table;
	struct neighbour *n;

	if (prefix) {	/* user wants prefix routes only */
		if (!(rt->rt6i_flags & RTF_PREFIX_RT)) {
			/* success since this is not a prefix route */
			return 1;
		}
	}

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*rtm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	rtm = nlmsg_data(nlh);
	rtm->rtm_family = AF_INET6;
	rtm->rtm_dst_len = rt->rt6i_dst.plen;
	rtm->rtm_src_len = rt->rt6i_src.plen;
	rtm->rtm_tos = 0;
	if (rt->rt6i_table)
		table = rt->rt6i_table->tb6_id;
	else
		table = RT6_TABLE_UNSPEC;
	rtm->rtm_table = table;
	NLA_PUT_U32(skb, RTA_TABLE, table);
	if (rt->rt6i_flags&RTF_REJECT)
		rtm->rtm_type = RTN_UNREACHABLE;
	else if (rt->rt6i_flags&RTF_LOCAL)
		rtm->rtm_type = RTN_LOCAL;
	else if (rt->rt6i_dev && (rt->rt6i_dev->flags&IFF_LOOPBACK))
		rtm->rtm_type = RTN_LOCAL;
	else
		rtm->rtm_type = RTN_UNICAST;
	rtm->rtm_flags = 0;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_protocol = rt->rt6i_protocol;
	if (rt->rt6i_flags&RTF_DYNAMIC)
		rtm->rtm_protocol = RTPROT_REDIRECT;
	else if (rt->rt6i_flags & RTF_ADDRCONF)
		rtm->rtm_protocol = RTPROT_KERNEL;
	else if (rt->rt6i_flags&RTF_DEFAULT)
		rtm->rtm_protocol = RTPROT_RA;

	if (rt->rt6i_flags&RTF_CACHE)
		rtm->rtm_flags |= RTM_F_CLONED;

	if (dst) {
		NLA_PUT(skb, RTA_DST, 16, dst);
		rtm->rtm_dst_len = 128;
	} else if (rtm->rtm_dst_len)
		NLA_PUT(skb, RTA_DST, 16, &rt->rt6i_dst.addr);
#ifdef CONFIG_IPV6_SUBTREES
	if (src) {
		NLA_PUT(skb, RTA_SRC, 16, src);
		rtm->rtm_src_len = 128;
	} else if (rtm->rtm_src_len)
		NLA_PUT(skb, RTA_SRC, 16, &rt->rt6i_src.addr);
#endif
	if (iif) {
#ifdef CONFIG_IPV6_MROUTE
		if (ipv6_addr_is_multicast(&rt->rt6i_dst.addr)) {
			int err = ip6mr_get_route(net, skb, rtm, nowait);
			if (err <= 0) {
				if (!nowait) {
					if (err == 0)
						return 0;
					goto nla_put_failure;
				} else {
					if (err == -EMSGSIZE)
						goto nla_put_failure;
				}
			}
		} else
#endif
			NLA_PUT_U32(skb, RTA_IIF, iif);
	} else if (dst) {
		struct in6_addr saddr_buf;
		if (ip6_route_get_saddr(net, rt, dst, 0, &saddr_buf) == 0)
			NLA_PUT(skb, RTA_PREFSRC, 16, &saddr_buf);
	}

	if (rt->rt6i_prefsrc.plen) {
		struct in6_addr saddr_buf;
		ipv6_addr_copy(&saddr_buf, &rt->rt6i_prefsrc.addr);
		NLA_PUT(skb, RTA_PREFSRC, 16, &saddr_buf);
	}

	if (rtnetlink_put_metrics(skb, dst_metrics_ptr(&rt->dst)) < 0)
		goto nla_put_failure;

	rcu_read_lock();
	n = dst_get_neighbour(&rt->dst);
	if (n)
		NLA_PUT(skb, RTA_GATEWAY, 16, &n->primary_key);
	rcu_read_unlock();

	if (rt->dst.dev)
		NLA_PUT_U32(skb, RTA_OIF, rt->rt6i_dev->ifindex);

	NLA_PUT_U32(skb, RTA_PRIORITY, rt->rt6i_metric);

	if (!(rt->rt6i_flags & RTF_EXPIRES))
		expires = 0;
	else if (rt->rt6i_expires - jiffies < INT_MAX)
		expires = rt->rt6i_expires - jiffies;
	else
		expires = INT_MAX;

	if (rtnl_put_cacheinfo(skb, &rt->dst, 0, 0, 0,
			       expires, rt->dst.error) < 0)
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

int rt6_dump_route(struct rt6_info *rt, void *p_arg)
{
	struct rt6_rtnl_dump_arg *arg = (struct rt6_rtnl_dump_arg *) p_arg;
	int prefix;

	if (nlmsg_len(arg->cb->nlh) >= sizeof(struct rtmsg)) {
		struct rtmsg *rtm = nlmsg_data(arg->cb->nlh);
		prefix = (rtm->rtm_flags & RTM_F_PREFIX) != 0;
	} else
		prefix = 0;

	return rt6_fill_node(arg->net,
		     arg->skb, rt, NULL, NULL, 0, RTM_NEWROUTE,
		     NETLINK_CB(arg->cb->skb).pid, arg->cb->nlh->nlmsg_seq,
		     prefix, 0, NLM_F_MULTI);
}

static int inet6_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr* nlh, void *arg)
{
	struct net *net = sock_net(in_skb->sk);
	struct nlattr *tb[RTA_MAX+1];
	struct rt6_info *rt;
	struct sk_buff *skb;
	struct rtmsg *rtm;
	struct flowi6 fl6;
	int err, iif = 0;

	err = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, rtm_ipv6_policy);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	memset(&fl6, 0, sizeof(fl6));

	if (tb[RTA_SRC]) {
		if (nla_len(tb[RTA_SRC]) < sizeof(struct in6_addr))
			goto errout;

		ipv6_addr_copy(&fl6.saddr, nla_data(tb[RTA_SRC]));
	}

	if (tb[RTA_DST]) {
		if (nla_len(tb[RTA_DST]) < sizeof(struct in6_addr))
			goto errout;

		ipv6_addr_copy(&fl6.daddr, nla_data(tb[RTA_DST]));
	}

	if (tb[RTA_IIF])
		iif = nla_get_u32(tb[RTA_IIF]);

	if (tb[RTA_OIF])
		fl6.flowi6_oif = nla_get_u32(tb[RTA_OIF]);

	if (iif) {
		struct net_device *dev;
		dev = __dev_get_by_index(net, iif);
		if (!dev) {
			err = -ENODEV;
			goto errout;
		}
	}

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (skb == NULL) {
		err = -ENOBUFS;
		goto errout;
	}

	/* Reserve room for dummy headers, this skb can pass
	   through good chunk of routing engine.
	 */
	skb_reset_mac_header(skb);
	skb_reserve(skb, MAX_HEADER + sizeof(struct ipv6hdr));

	rt = (struct rt6_info*) ip6_route_output(net, NULL, &fl6);
	skb_dst_set(skb, &rt->dst);

	err = rt6_fill_node(net, skb, rt, &fl6.daddr, &fl6.saddr, iif,
			    RTM_NEWROUTE, NETLINK_CB(in_skb).pid,
			    nlh->nlmsg_seq, 0, 0, 0);
	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).pid);
errout:
	return err;
}

void inet6_rt_notify(int event, struct rt6_info *rt, struct nl_info *info)
{
	struct sk_buff *skb;
	struct net *net = info->nl_net;
	u32 seq;
	int err;

	err = -ENOBUFS;
	seq = info->nlh != NULL ? info->nlh->nlmsg_seq : 0;

	skb = nlmsg_new(rt6_nlmsg_size(), gfp_any());
	if (skb == NULL)
		goto errout;

	err = rt6_fill_node(net, skb, rt, NULL, NULL, 0,
				event, info->pid, seq, 0, 0, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in rt6_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, info->pid, RTNLGRP_IPV6_ROUTE,
		    info->nlh, gfp_any());
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_IPV6_ROUTE, err);
}

static int ip6_route_dev_notify(struct notifier_block *this,
				unsigned long event, void *data)
{
	struct net_device *dev = (struct net_device *)data;
	struct net *net = dev_net(dev);

	if (event == NETDEV_REGISTER && (dev->flags & IFF_LOOPBACK)) {
		net->ipv6.ip6_null_entry->dst.dev = dev;
		net->ipv6.ip6_null_entry->rt6i_idev = in6_dev_get(dev);
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
		net->ipv6.ip6_prohibit_entry->dst.dev = dev;
		net->ipv6.ip6_prohibit_entry->rt6i_idev = in6_dev_get(dev);
		net->ipv6.ip6_blk_hole_entry->dst.dev = dev;
		net->ipv6.ip6_blk_hole_entry->rt6i_idev = in6_dev_get(dev);
#endif
	}

	return NOTIFY_OK;
}

/*
 *	/proc
 */

#ifdef CONFIG_PROC_FS

struct rt6_proc_arg
{
	char *buffer;
	int offset;
	int length;
	int skip;
	int len;
};

static int rt6_info_route(struct rt6_info *rt, void *p_arg)
{
	struct seq_file *m = p_arg;
	struct neighbour *n;

	seq_printf(m, "%pi6 %02x ", &rt->rt6i_dst.addr, rt->rt6i_dst.plen);

#ifdef CONFIG_IPV6_SUBTREES
	seq_printf(m, "%pi6 %02x ", &rt->rt6i_src.addr, rt->rt6i_src.plen);
#else
	seq_puts(m, "00000000000000000000000000000000 00 ");
#endif
	rcu_read_lock();
	n = dst_get_neighbour(&rt->dst);
	if (n) {
		seq_printf(m, "%pi6", n->primary_key);
	} else {
		seq_puts(m, "00000000000000000000000000000000");
	}
	rcu_read_unlock();
	seq_printf(m, " %08x %08x %08x %08x %8s\n",
		   rt->rt6i_metric, atomic_read(&rt->dst.__refcnt),
		   rt->dst.__use, rt->rt6i_flags,
		   rt->rt6i_dev ? rt->rt6i_dev->name : "");
	return 0;
}

static int ipv6_route_show(struct seq_file *m, void *v)
{
	struct net *net = (struct net *)m->private;
	fib6_clean_all(net, rt6_info_route, 0, m);
	return 0;
}

static int ipv6_route_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, ipv6_route_show);
}

static const struct file_operations ipv6_route_proc_fops = {
	.owner		= THIS_MODULE,
	.open		= ipv6_route_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release_net,
};

static int rt6_stats_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = (struct net *)seq->private;
	seq_printf(seq, "%04x %04x %04x %04x %04x %04x %04x\n",
		   net->ipv6.rt6_stats->fib_nodes,
		   net->ipv6.rt6_stats->fib_route_nodes,
		   net->ipv6.rt6_stats->fib_rt_alloc,
		   net->ipv6.rt6_stats->fib_rt_entries,
		   net->ipv6.rt6_stats->fib_rt_cache,
		   dst_entries_get_slow(&net->ipv6.ip6_dst_ops),
		   net->ipv6.rt6_stats->fib_discarded_routes);

	return 0;
}

static int rt6_stats_seq_open(struct inode *inode, struct file *file)
{
	return single_open_net(inode, file, rt6_stats_seq_show);
}

static const struct file_operations rt6_stats_seq_fops = {
	.owner	 = THIS_MODULE,
	.open	 = rt6_stats_seq_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release_net,
};
#endif	/* CONFIG_PROC_FS */

#ifdef CONFIG_SYSCTL

static
int ipv6_sysctl_rtcache_flush(ctl_table *ctl, int write,
			      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net;
	int delay;
	if (!write)
		return -EINVAL;

	net = (struct net *)ctl->extra1;
	delay = net->ipv6.sysctl.flush_delay;
	proc_dointvec(ctl, write, buffer, lenp, ppos);
	fib6_run_gc(delay <= 0 ? ~0UL : (unsigned long)delay, net);
	return 0;
}

ctl_table ipv6_route_table_template[] = {
	{
		.procname	=	"flush",
		.data		=	&init_net.ipv6.sysctl.flush_delay,
		.maxlen		=	sizeof(int),
		.mode		=	0200,
		.proc_handler	=	ipv6_sysctl_rtcache_flush
	},
	{
		.procname	=	"gc_thresh",
		.data		=	&ip6_dst_ops_template.gc_thresh,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec,
	},
	{
		.procname	=	"max_size",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_max_size,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec,
	},
	{
		.procname	=	"gc_min_interval",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_gc_min_interval,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec_jiffies,
	},
	{
		.procname	=	"gc_timeout",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_gc_timeout,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec_jiffies,
	},
	{
		.procname	=	"gc_interval",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_gc_interval,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec_jiffies,
	},
	{
		.procname	=	"gc_elasticity",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_gc_elasticity,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec,
	},
	{
		.procname	=	"mtu_expires",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_mtu_expires,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec_jiffies,
	},
	{
		.procname	=	"min_adv_mss",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_min_advmss,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec,
	},
	{
		.procname	=	"gc_min_interval_ms",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_gc_min_interval,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec_ms_jiffies,
	},
	{ }
};

struct ctl_table * __net_init ipv6_route_sysctl_init(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(ipv6_route_table_template,
			sizeof(ipv6_route_table_template),
			GFP_KERNEL);

	if (table) {
		table[0].data = &net->ipv6.sysctl.flush_delay;
		table[0].extra1 = net;
		table[1].data = &net->ipv6.ip6_dst_ops.gc_thresh;
		table[2].data = &net->ipv6.sysctl.ip6_rt_max_size;
		table[3].data = &net->ipv6.sysctl.ip6_rt_gc_min_interval;
		table[4].data = &net->ipv6.sysctl.ip6_rt_gc_timeout;
		table[5].data = &net->ipv6.sysctl.ip6_rt_gc_interval;
		table[6].data = &net->ipv6.sysctl.ip6_rt_gc_elasticity;
		table[7].data = &net->ipv6.sysctl.ip6_rt_mtu_expires;
		table[8].data = &net->ipv6.sysctl.ip6_rt_min_advmss;
		table[9].data = &net->ipv6.sysctl.ip6_rt_gc_min_interval;
	}

	return table;
}
#endif

static int __net_init ip6_route_net_init(struct net *net)
{
	int ret = -ENOMEM;

	memcpy(&net->ipv6.ip6_dst_ops, &ip6_dst_ops_template,
	       sizeof(net->ipv6.ip6_dst_ops));

	if (dst_entries_init(&net->ipv6.ip6_dst_ops) < 0)
		goto out_ip6_dst_ops;

	net->ipv6.ip6_null_entry = kmemdup(&ip6_null_entry_template,
					   sizeof(*net->ipv6.ip6_null_entry),
					   GFP_KERNEL);
	if (!net->ipv6.ip6_null_entry)
		goto out_ip6_dst_entries;
	net->ipv6.ip6_null_entry->dst.path =
		(struct dst_entry *)net->ipv6.ip6_null_entry;
	net->ipv6.ip6_null_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_null_entry->dst,
			 ip6_template_metrics, true);

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	net->ipv6.ip6_prohibit_entry = kmemdup(&ip6_prohibit_entry_template,
					       sizeof(*net->ipv6.ip6_prohibit_entry),
					       GFP_KERNEL);
	if (!net->ipv6.ip6_prohibit_entry)
		goto out_ip6_null_entry;
	net->ipv6.ip6_prohibit_entry->dst.path =
		(struct dst_entry *)net->ipv6.ip6_prohibit_entry;
	net->ipv6.ip6_prohibit_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_prohibit_entry->dst,
			 ip6_template_metrics, true);

	net->ipv6.ip6_blk_hole_entry = kmemdup(&ip6_blk_hole_entry_template,
					       sizeof(*net->ipv6.ip6_blk_hole_entry),
					       GFP_KERNEL);
	if (!net->ipv6.ip6_blk_hole_entry)
		goto out_ip6_prohibit_entry;
	net->ipv6.ip6_blk_hole_entry->dst.path =
		(struct dst_entry *)net->ipv6.ip6_blk_hole_entry;
	net->ipv6.ip6_blk_hole_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_blk_hole_entry->dst,
			 ip6_template_metrics, true);
#endif

	net->ipv6.sysctl.flush_delay = 0;
	net->ipv6.sysctl.ip6_rt_max_size = 4096;
	net->ipv6.sysctl.ip6_rt_gc_min_interval = HZ / 2;
	net->ipv6.sysctl.ip6_rt_gc_timeout = 60*HZ;
	net->ipv6.sysctl.ip6_rt_gc_interval = 30*HZ;
	net->ipv6.sysctl.ip6_rt_gc_elasticity = 9;
	net->ipv6.sysctl.ip6_rt_mtu_expires = 10*60*HZ;
	net->ipv6.sysctl.ip6_rt_min_advmss = IPV6_MIN_MTU - 20 - 40;

#ifdef CONFIG_PROC_FS
	proc_net_fops_create(net, "ipv6_route", 0, &ipv6_route_proc_fops);
	proc_net_fops_create(net, "rt6_stats", S_IRUGO, &rt6_stats_seq_fops);
#endif
	net->ipv6.ip6_rt_gc_expire = 30*HZ;

	ret = 0;
out:
	return ret;

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
out_ip6_prohibit_entry:
	kfree(net->ipv6.ip6_prohibit_entry);
out_ip6_null_entry:
	kfree(net->ipv6.ip6_null_entry);
#endif
out_ip6_dst_entries:
	dst_entries_destroy(&net->ipv6.ip6_dst_ops);
out_ip6_dst_ops:
	goto out;
}

static void __net_exit ip6_route_net_exit(struct net *net)
{
#ifdef CONFIG_PROC_FS
	proc_net_remove(net, "ipv6_route");
	proc_net_remove(net, "rt6_stats");
#endif
	kfree(net->ipv6.ip6_null_entry);
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	kfree(net->ipv6.ip6_prohibit_entry);
	kfree(net->ipv6.ip6_blk_hole_entry);
#endif
	dst_entries_destroy(&net->ipv6.ip6_dst_ops);
}

static struct pernet_operations ip6_route_net_ops = {
	.init = ip6_route_net_init,
	.exit = ip6_route_net_exit,
};

static struct notifier_block ip6_route_dev_notifier = {
	.notifier_call = ip6_route_dev_notify,
	.priority = 0,
};

int __init ip6_route_init(void)
{
	int ret;

	ret = -ENOMEM;
	ip6_dst_ops_template.kmem_cachep =
		kmem_cache_create("ip6_dst_cache", sizeof(struct rt6_info), 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!ip6_dst_ops_template.kmem_cachep)
		goto out;

	ret = dst_entries_init(&ip6_dst_blackhole_ops);
	if (ret)
		goto out_kmem_cache;

	ret = register_pernet_subsys(&ip6_route_net_ops);
	if (ret)
		goto out_dst_entries;

	ip6_dst_blackhole_ops.kmem_cachep = ip6_dst_ops_template.kmem_cachep;

	/* Registering of the loopback is done before this portion of code,
	 * the loopback reference in rt6_info will not be taken, do it
	 * manually for init_net */
	init_net.ipv6.ip6_null_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_null_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
  #ifdef CONFIG_IPV6_MULTIPLE_TABLES
	init_net.ipv6.ip6_prohibit_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_prohibit_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
	init_net.ipv6.ip6_blk_hole_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_blk_hole_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
  #endif
	ret = fib6_init();
	if (ret)
		goto out_register_subsys;

	ret = xfrm6_init();
	if (ret)
		goto out_fib6_init;

	ret = fib6_rules_init();
	if (ret)
		goto xfrm6_init;

	ret = -ENOBUFS;
	if (__rtnl_register(PF_INET6, RTM_NEWROUTE, inet6_rtm_newroute, NULL, NULL) ||
	    __rtnl_register(PF_INET6, RTM_DELROUTE, inet6_rtm_delroute, NULL, NULL) ||
	    __rtnl_register(PF_INET6, RTM_GETROUTE, inet6_rtm_getroute, NULL, NULL))
		goto fib6_rules_init;

	ret = register_netdevice_notifier(&ip6_route_dev_notifier);
	if (ret)
		goto fib6_rules_init;

out:
	return ret;

fib6_rules_init:
	fib6_rules_cleanup();
xfrm6_init:
	xfrm6_fini();
out_fib6_init:
	fib6_gc_cleanup();
out_register_subsys:
	unregister_pernet_subsys(&ip6_route_net_ops);
out_dst_entries:
	dst_entries_destroy(&ip6_dst_blackhole_ops);
out_kmem_cache:
	kmem_cache_destroy(ip6_dst_ops_template.kmem_cachep);
	goto out;
}

void ip6_route_cleanup(void)
{
	unregister_netdevice_notifier(&ip6_route_dev_notifier);
	fib6_rules_cleanup();
	xfrm6_fini();
	fib6_gc_cleanup();
	unregister_pernet_subsys(&ip6_route_net_ops);
	dst_entries_destroy(&ip6_dst_blackhole_ops);
	kmem_cache_destroy(ip6_dst_ops_template.kmem_cachep);
}
