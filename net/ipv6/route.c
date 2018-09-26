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

#define pr_fmt(fmt) "IPv6: " fmt

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
#include <linux/jhash.h>
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
#include <net/dst_metadata.h>
#include <net/xfrm.h>
#include <net/netevent.h>
#include <net/netlink.h>
#include <net/nexthop.h>
#include <net/lwtunnel.h>
#include <net/ip_tunnels.h>
#include <net/l3mdev.h>
#include <net/ip.h>
#include <linux/uaccess.h>

#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif

static int ip6_rt_type_to_error(u8 fib6_type);

#define CREATE_TRACE_POINTS
#include <trace/events/fib6.h>
EXPORT_TRACEPOINT_SYMBOL_GPL(fib6_table_lookup);
#undef CREATE_TRACE_POINTS

enum rt6_nud_state {
	RT6_NUD_FAIL_HARD = -3,
	RT6_NUD_FAIL_PROBE = -2,
	RT6_NUD_FAIL_DO_RR = -1,
	RT6_NUD_SUCCEED = 1
};

static struct dst_entry	*ip6_dst_check(struct dst_entry *dst, u32 cookie);
static unsigned int	 ip6_default_advmss(const struct dst_entry *dst);
static unsigned int	 ip6_mtu(const struct dst_entry *dst);
static struct dst_entry *ip6_negative_advice(struct dst_entry *);
static void		ip6_dst_destroy(struct dst_entry *);
static void		ip6_dst_ifdown(struct dst_entry *,
				       struct net_device *dev, int how);
static int		 ip6_dst_gc(struct dst_ops *ops);

static int		ip6_pkt_discard(struct sk_buff *skb);
static int		ip6_pkt_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb);
static int		ip6_pkt_prohibit(struct sk_buff *skb);
static int		ip6_pkt_prohibit_out(struct net *net, struct sock *sk, struct sk_buff *skb);
static void		ip6_link_failure(struct sk_buff *skb);
static void		ip6_rt_update_pmtu(struct dst_entry *dst, struct sock *sk,
					   struct sk_buff *skb, u32 mtu);
static void		rt6_do_redirect(struct dst_entry *dst, struct sock *sk,
					struct sk_buff *skb);
static int rt6_score_route(struct fib6_info *rt, int oif, int strict);
static size_t rt6_nlmsg_size(struct fib6_info *rt);
static int rt6_fill_node(struct net *net, struct sk_buff *skb,
			 struct fib6_info *rt, struct dst_entry *dst,
			 struct in6_addr *dest, struct in6_addr *src,
			 int iif, int type, u32 portid, u32 seq,
			 unsigned int flags);
static struct rt6_info *rt6_find_cached_rt(struct fib6_info *rt,
					   struct in6_addr *daddr,
					   struct in6_addr *saddr);

#ifdef CONFIG_IPV6_ROUTE_INFO
static struct fib6_info *rt6_add_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr,
					   struct net_device *dev,
					   unsigned int pref);
static struct fib6_info *rt6_get_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr,
					   struct net_device *dev);
#endif

struct uncached_list {
	spinlock_t		lock;
	struct list_head	head;
};

static DEFINE_PER_CPU_ALIGNED(struct uncached_list, rt6_uncached_list);

void rt6_uncached_list_add(struct rt6_info *rt)
{
	struct uncached_list *ul = raw_cpu_ptr(&rt6_uncached_list);

	rt->rt6i_uncached_list = ul;

	spin_lock_bh(&ul->lock);
	list_add_tail(&rt->rt6i_uncached, &ul->head);
	spin_unlock_bh(&ul->lock);
}

void rt6_uncached_list_del(struct rt6_info *rt)
{
	if (!list_empty(&rt->rt6i_uncached)) {
		struct uncached_list *ul = rt->rt6i_uncached_list;
		struct net *net = dev_net(rt->dst.dev);

		spin_lock_bh(&ul->lock);
		list_del(&rt->rt6i_uncached);
		atomic_dec(&net->ipv6.rt6_stats->fib_rt_uncache);
		spin_unlock_bh(&ul->lock);
	}
}

static void rt6_uncached_list_flush_dev(struct net *net, struct net_device *dev)
{
	struct net_device *loopback_dev = net->loopback_dev;
	int cpu;

	if (dev == loopback_dev)
		return;

	for_each_possible_cpu(cpu) {
		struct uncached_list *ul = per_cpu_ptr(&rt6_uncached_list, cpu);
		struct rt6_info *rt;

		spin_lock_bh(&ul->lock);
		list_for_each_entry(rt, &ul->head, rt6i_uncached) {
			struct inet6_dev *rt_idev = rt->rt6i_idev;
			struct net_device *rt_dev = rt->dst.dev;

			if (rt_idev->dev == dev) {
				rt->rt6i_idev = in6_dev_get(loopback_dev);
				in6_dev_put(rt_idev);
			}

			if (rt_dev == dev) {
				rt->dst.dev = loopback_dev;
				dev_hold(rt->dst.dev);
				dev_put(rt_dev);
			}
		}
		spin_unlock_bh(&ul->lock);
	}
}

static inline const void *choose_neigh_daddr(const struct in6_addr *p,
					     struct sk_buff *skb,
					     const void *daddr)
{
	if (!ipv6_addr_any(p))
		return (const void *) p;
	else if (skb)
		return &ipv6_hdr(skb)->daddr;
	return daddr;
}

struct neighbour *ip6_neigh_lookup(const struct in6_addr *gw,
				   struct net_device *dev,
				   struct sk_buff *skb,
				   const void *daddr)
{
	struct neighbour *n;

	daddr = choose_neigh_daddr(gw, skb, daddr);
	n = __ipv6_neigh_lookup(dev, daddr);
	if (n)
		return n;
	return neigh_create(&nd_tbl, daddr, dev);
}

static struct neighbour *ip6_dst_neigh_lookup(const struct dst_entry *dst,
					      struct sk_buff *skb,
					      const void *daddr)
{
	const struct rt6_info *rt = container_of(dst, struct rt6_info, dst);

	return ip6_neigh_lookup(&rt->rt6i_gateway, dst->dev, skb, daddr);
}

static void ip6_confirm_neigh(const struct dst_entry *dst, const void *daddr)
{
	struct net_device *dev = dst->dev;
	struct rt6_info *rt = (struct rt6_info *)dst;

	daddr = choose_neigh_daddr(&rt->rt6i_gateway, NULL, daddr);
	if (!daddr)
		return;
	if (dev->flags & (IFF_NOARP | IFF_LOOPBACK))
		return;
	if (ipv6_addr_is_multicast((const struct in6_addr *)daddr))
		return;
	__ipv6_confirm_neigh(dev, daddr);
}

static struct dst_ops ip6_dst_ops_template = {
	.family			=	AF_INET6,
	.gc			=	ip6_dst_gc,
	.gc_thresh		=	1024,
	.check			=	ip6_dst_check,
	.default_advmss		=	ip6_default_advmss,
	.mtu			=	ip6_mtu,
	.cow_metrics		=	dst_cow_metrics_generic,
	.destroy		=	ip6_dst_destroy,
	.ifdown			=	ip6_dst_ifdown,
	.negative_advice	=	ip6_negative_advice,
	.link_failure		=	ip6_link_failure,
	.update_pmtu		=	ip6_rt_update_pmtu,
	.redirect		=	rt6_do_redirect,
	.local_out		=	__ip6_local_out,
	.neigh_lookup		=	ip6_dst_neigh_lookup,
	.confirm_neigh		=	ip6_confirm_neigh,
};

static unsigned int ip6_blackhole_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = dst_metric_raw(dst, RTAX_MTU);

	return mtu ? : dst->dev->mtu;
}

static void ip6_rt_blackhole_update_pmtu(struct dst_entry *dst, struct sock *sk,
					 struct sk_buff *skb, u32 mtu)
{
}

static void ip6_rt_blackhole_redirect(struct dst_entry *dst, struct sock *sk,
				      struct sk_buff *skb)
{
}

static struct dst_ops ip6_dst_blackhole_ops = {
	.family			=	AF_INET6,
	.destroy		=	ip6_dst_destroy,
	.check			=	ip6_dst_check,
	.mtu			=	ip6_blackhole_mtu,
	.default_advmss		=	ip6_default_advmss,
	.update_pmtu		=	ip6_rt_blackhole_update_pmtu,
	.redirect		=	ip6_rt_blackhole_redirect,
	.cow_metrics		=	dst_cow_metrics_generic,
	.neigh_lookup		=	ip6_dst_neigh_lookup,
};

static const u32 ip6_template_metrics[RTAX_MAX] = {
	[RTAX_HOPLIMIT - 1] = 0,
};

static const struct fib6_info fib6_null_entry_template = {
	.fib6_flags	= (RTF_REJECT | RTF_NONEXTHOP),
	.fib6_protocol  = RTPROT_KERNEL,
	.fib6_metric	= ~(u32)0,
	.fib6_ref	= ATOMIC_INIT(1),
	.fib6_type	= RTN_UNREACHABLE,
	.fib6_metrics	= (struct dst_metrics *)&dst_default_metrics,
};

static const struct rt6_info ip6_null_entry_template = {
	.dst = {
		.__refcnt	= ATOMIC_INIT(1),
		.__use		= 1,
		.obsolete	= DST_OBSOLETE_FORCE_CHK,
		.error		= -ENETUNREACH,
		.input		= ip6_pkt_discard,
		.output		= ip6_pkt_discard_out,
	},
	.rt6i_flags	= (RTF_REJECT | RTF_NONEXTHOP),
};

#ifdef CONFIG_IPV6_MULTIPLE_TABLES

static const struct rt6_info ip6_prohibit_entry_template = {
	.dst = {
		.__refcnt	= ATOMIC_INIT(1),
		.__use		= 1,
		.obsolete	= DST_OBSOLETE_FORCE_CHK,
		.error		= -EACCES,
		.input		= ip6_pkt_prohibit,
		.output		= ip6_pkt_prohibit_out,
	},
	.rt6i_flags	= (RTF_REJECT | RTF_NONEXTHOP),
};

static const struct rt6_info ip6_blk_hole_entry_template = {
	.dst = {
		.__refcnt	= ATOMIC_INIT(1),
		.__use		= 1,
		.obsolete	= DST_OBSOLETE_FORCE_CHK,
		.error		= -EINVAL,
		.input		= dst_discard,
		.output		= dst_discard_out,
	},
	.rt6i_flags	= (RTF_REJECT | RTF_NONEXTHOP),
};

#endif

static void rt6_info_init(struct rt6_info *rt)
{
	struct dst_entry *dst = &rt->dst;

	memset(dst + 1, 0, sizeof(*rt) - sizeof(*dst));
	INIT_LIST_HEAD(&rt->rt6i_uncached);
}

/* allocate dst with ip6_dst_ops */
struct rt6_info *ip6_dst_alloc(struct net *net, struct net_device *dev,
			       int flags)
{
	struct rt6_info *rt = dst_alloc(&net->ipv6.ip6_dst_ops, dev,
					1, DST_OBSOLETE_FORCE_CHK, flags);

	if (rt) {
		rt6_info_init(rt);
		atomic_inc(&net->ipv6.rt6_stats->fib_rt_alloc);
	}

	return rt;
}
EXPORT_SYMBOL(ip6_dst_alloc);

static void ip6_dst_destroy(struct dst_entry *dst)
{
	struct rt6_info *rt = (struct rt6_info *)dst;
	struct fib6_info *from;
	struct inet6_dev *idev;

	dst_destroy_metrics_generic(dst);
	rt6_uncached_list_del(rt);

	idev = rt->rt6i_idev;
	if (idev) {
		rt->rt6i_idev = NULL;
		in6_dev_put(idev);
	}

	rcu_read_lock();
	from = rcu_dereference(rt->from);
	rcu_assign_pointer(rt->from, NULL);
	fib6_info_release(from);
	rcu_read_unlock();
}

static void ip6_dst_ifdown(struct dst_entry *dst, struct net_device *dev,
			   int how)
{
	struct rt6_info *rt = (struct rt6_info *)dst;
	struct inet6_dev *idev = rt->rt6i_idev;
	struct net_device *loopback_dev =
		dev_net(dev)->loopback_dev;

	if (idev && idev->dev != loopback_dev) {
		struct inet6_dev *loopback_idev = in6_dev_get(loopback_dev);
		if (loopback_idev) {
			rt->rt6i_idev = loopback_idev;
			in6_dev_put(idev);
		}
	}
}

static bool __rt6_check_expired(const struct rt6_info *rt)
{
	if (rt->rt6i_flags & RTF_EXPIRES)
		return time_after(jiffies, rt->dst.expires);
	else
		return false;
}

static bool rt6_check_expired(const struct rt6_info *rt)
{
	struct fib6_info *from;

	from = rcu_dereference(rt->from);

	if (rt->rt6i_flags & RTF_EXPIRES) {
		if (time_after(jiffies, rt->dst.expires))
			return true;
	} else if (from) {
		return rt->dst.obsolete != DST_OBSOLETE_FORCE_CHK ||
			fib6_check_expired(from);
	}
	return false;
}

struct fib6_info *fib6_multipath_select(const struct net *net,
					struct fib6_info *match,
					struct flowi6 *fl6, int oif,
					const struct sk_buff *skb,
					int strict)
{
	struct fib6_info *sibling, *next_sibling;

	/* We might have already computed the hash for ICMPv6 errors. In such
	 * case it will always be non-zero. Otherwise now is the time to do it.
	 */
	if (!fl6->mp_hash)
		fl6->mp_hash = rt6_multipath_hash(net, fl6, skb, NULL);

	if (fl6->mp_hash <= atomic_read(&match->fib6_nh.nh_upper_bound))
		return match;

	list_for_each_entry_safe(sibling, next_sibling, &match->fib6_siblings,
				 fib6_siblings) {
		int nh_upper_bound;

		nh_upper_bound = atomic_read(&sibling->fib6_nh.nh_upper_bound);
		if (fl6->mp_hash > nh_upper_bound)
			continue;
		if (rt6_score_route(sibling, oif, strict) < 0)
			break;
		match = sibling;
		break;
	}

	return match;
}

/*
 *	Route lookup. rcu_read_lock() should be held.
 */

static inline struct fib6_info *rt6_device_match(struct net *net,
						 struct fib6_info *rt,
						    const struct in6_addr *saddr,
						    int oif,
						    int flags)
{
	struct fib6_info *sprt;

	if (!oif && ipv6_addr_any(saddr) &&
	    !(rt->fib6_nh.nh_flags & RTNH_F_DEAD))
		return rt;

	for (sprt = rt; sprt; sprt = rcu_dereference(sprt->fib6_next)) {
		const struct net_device *dev = sprt->fib6_nh.nh_dev;

		if (sprt->fib6_nh.nh_flags & RTNH_F_DEAD)
			continue;

		if (oif) {
			if (dev->ifindex == oif)
				return sprt;
		} else {
			if (ipv6_chk_addr(net, saddr, dev,
					  flags & RT6_LOOKUP_F_IFACE))
				return sprt;
		}
	}

	if (oif && flags & RT6_LOOKUP_F_IFACE)
		return net->ipv6.fib6_null_entry;

	return rt->fib6_nh.nh_flags & RTNH_F_DEAD ? net->ipv6.fib6_null_entry : rt;
}

#ifdef CONFIG_IPV6_ROUTER_PREF
struct __rt6_probe_work {
	struct work_struct work;
	struct in6_addr target;
	struct net_device *dev;
};

static void rt6_probe_deferred(struct work_struct *w)
{
	struct in6_addr mcaddr;
	struct __rt6_probe_work *work =
		container_of(w, struct __rt6_probe_work, work);

	addrconf_addr_solict_mult(&work->target, &mcaddr);
	ndisc_send_ns(work->dev, &work->target, &mcaddr, NULL, 0);
	dev_put(work->dev);
	kfree(work);
}

static void rt6_probe(struct fib6_info *rt)
{
	struct __rt6_probe_work *work;
	const struct in6_addr *nh_gw;
	struct neighbour *neigh;
	struct net_device *dev;

	/*
	 * Okay, this does not seem to be appropriate
	 * for now, however, we need to check if it
	 * is really so; aka Router Reachability Probing.
	 *
	 * Router Reachability Probe MUST be rate-limited
	 * to no more than one per minute.
	 */
	if (!rt || !(rt->fib6_flags & RTF_GATEWAY))
		return;

	nh_gw = &rt->fib6_nh.nh_gw;
	dev = rt->fib6_nh.nh_dev;
	rcu_read_lock_bh();
	neigh = __ipv6_neigh_lookup_noref(dev, nh_gw);
	if (neigh) {
		struct inet6_dev *idev;

		if (neigh->nud_state & NUD_VALID)
			goto out;

		idev = __in6_dev_get(dev);
		work = NULL;
		write_lock(&neigh->lock);
		if (!(neigh->nud_state & NUD_VALID) &&
		    time_after(jiffies,
			       neigh->updated + idev->cnf.rtr_probe_interval)) {
			work = kmalloc(sizeof(*work), GFP_ATOMIC);
			if (work)
				__neigh_set_probe_once(neigh);
		}
		write_unlock(&neigh->lock);
	} else {
		work = kmalloc(sizeof(*work), GFP_ATOMIC);
	}

	if (work) {
		INIT_WORK(&work->work, rt6_probe_deferred);
		work->target = *nh_gw;
		dev_hold(dev);
		work->dev = dev;
		schedule_work(&work->work);
	}

out:
	rcu_read_unlock_bh();
}
#else
static inline void rt6_probe(struct fib6_info *rt)
{
}
#endif

/*
 * Default Router Selection (RFC 2461 6.3.6)
 */
static inline int rt6_check_dev(struct fib6_info *rt, int oif)
{
	const struct net_device *dev = rt->fib6_nh.nh_dev;

	if (!oif || dev->ifindex == oif)
		return 2;
	return 0;
}

static inline enum rt6_nud_state rt6_check_neigh(struct fib6_info *rt)
{
	enum rt6_nud_state ret = RT6_NUD_FAIL_HARD;
	struct neighbour *neigh;

	if (rt->fib6_flags & RTF_NONEXTHOP ||
	    !(rt->fib6_flags & RTF_GATEWAY))
		return RT6_NUD_SUCCEED;

	rcu_read_lock_bh();
	neigh = __ipv6_neigh_lookup_noref(rt->fib6_nh.nh_dev,
					  &rt->fib6_nh.nh_gw);
	if (neigh) {
		read_lock(&neigh->lock);
		if (neigh->nud_state & NUD_VALID)
			ret = RT6_NUD_SUCCEED;
#ifdef CONFIG_IPV6_ROUTER_PREF
		else if (!(neigh->nud_state & NUD_FAILED))
			ret = RT6_NUD_SUCCEED;
		else
			ret = RT6_NUD_FAIL_PROBE;
#endif
		read_unlock(&neigh->lock);
	} else {
		ret = IS_ENABLED(CONFIG_IPV6_ROUTER_PREF) ?
		      RT6_NUD_SUCCEED : RT6_NUD_FAIL_DO_RR;
	}
	rcu_read_unlock_bh();

	return ret;
}

static int rt6_score_route(struct fib6_info *rt, int oif, int strict)
{
	int m;

	m = rt6_check_dev(rt, oif);
	if (!m && (strict & RT6_LOOKUP_F_IFACE))
		return RT6_NUD_FAIL_HARD;
#ifdef CONFIG_IPV6_ROUTER_PREF
	m |= IPV6_DECODE_PREF(IPV6_EXTRACT_PREF(rt->fib6_flags)) << 2;
#endif
	if (strict & RT6_LOOKUP_F_REACHABLE) {
		int n = rt6_check_neigh(rt);
		if (n < 0)
			return n;
	}
	return m;
}

/* called with rc_read_lock held */
static inline bool fib6_ignore_linkdown(const struct fib6_info *f6i)
{
	const struct net_device *dev = fib6_info_nh_dev(f6i);
	bool rc = false;

	if (dev) {
		const struct inet6_dev *idev = __in6_dev_get(dev);

		rc = !!idev->cnf.ignore_routes_with_linkdown;
	}

	return rc;
}

static struct fib6_info *find_match(struct fib6_info *rt, int oif, int strict,
				   int *mpri, struct fib6_info *match,
				   bool *do_rr)
{
	int m;
	bool match_do_rr = false;

	if (rt->fib6_nh.nh_flags & RTNH_F_DEAD)
		goto out;

	if (fib6_ignore_linkdown(rt) &&
	    rt->fib6_nh.nh_flags & RTNH_F_LINKDOWN &&
	    !(strict & RT6_LOOKUP_F_IGNORE_LINKSTATE))
		goto out;

	if (fib6_check_expired(rt))
		goto out;

	m = rt6_score_route(rt, oif, strict);
	if (m == RT6_NUD_FAIL_DO_RR) {
		match_do_rr = true;
		m = 0; /* lowest valid score */
	} else if (m == RT6_NUD_FAIL_HARD) {
		goto out;
	}

	if (strict & RT6_LOOKUP_F_REACHABLE)
		rt6_probe(rt);

	/* note that m can be RT6_NUD_FAIL_PROBE at this point */
	if (m > *mpri) {
		*do_rr = match_do_rr;
		*mpri = m;
		match = rt;
	}
out:
	return match;
}

static struct fib6_info *find_rr_leaf(struct fib6_node *fn,
				     struct fib6_info *leaf,
				     struct fib6_info *rr_head,
				     u32 metric, int oif, int strict,
				     bool *do_rr)
{
	struct fib6_info *rt, *match, *cont;
	int mpri = -1;

	match = NULL;
	cont = NULL;
	for (rt = rr_head; rt; rt = rcu_dereference(rt->fib6_next)) {
		if (rt->fib6_metric != metric) {
			cont = rt;
			break;
		}

		match = find_match(rt, oif, strict, &mpri, match, do_rr);
	}

	for (rt = leaf; rt && rt != rr_head;
	     rt = rcu_dereference(rt->fib6_next)) {
		if (rt->fib6_metric != metric) {
			cont = rt;
			break;
		}

		match = find_match(rt, oif, strict, &mpri, match, do_rr);
	}

	if (match || !cont)
		return match;

	for (rt = cont; rt; rt = rcu_dereference(rt->fib6_next))
		match = find_match(rt, oif, strict, &mpri, match, do_rr);

	return match;
}

static struct fib6_info *rt6_select(struct net *net, struct fib6_node *fn,
				   int oif, int strict)
{
	struct fib6_info *leaf = rcu_dereference(fn->leaf);
	struct fib6_info *match, *rt0;
	bool do_rr = false;
	int key_plen;

	if (!leaf || leaf == net->ipv6.fib6_null_entry)
		return net->ipv6.fib6_null_entry;

	rt0 = rcu_dereference(fn->rr_ptr);
	if (!rt0)
		rt0 = leaf;

	/* Double check to make sure fn is not an intermediate node
	 * and fn->leaf does not points to its child's leaf
	 * (This might happen if all routes under fn are deleted from
	 * the tree and fib6_repair_tree() is called on the node.)
	 */
	key_plen = rt0->fib6_dst.plen;
#ifdef CONFIG_IPV6_SUBTREES
	if (rt0->fib6_src.plen)
		key_plen = rt0->fib6_src.plen;
#endif
	if (fn->fn_bit != key_plen)
		return net->ipv6.fib6_null_entry;

	match = find_rr_leaf(fn, leaf, rt0, rt0->fib6_metric, oif, strict,
			     &do_rr);

	if (do_rr) {
		struct fib6_info *next = rcu_dereference(rt0->fib6_next);

		/* no entries matched; do round-robin */
		if (!next || next->fib6_metric != rt0->fib6_metric)
			next = leaf;

		if (next != rt0) {
			spin_lock_bh(&leaf->fib6_table->tb6_lock);
			/* make sure next is not being deleted from the tree */
			if (next->fib6_node)
				rcu_assign_pointer(fn->rr_ptr, next);
			spin_unlock_bh(&leaf->fib6_table->tb6_lock);
		}
	}

	return match ? match : net->ipv6.fib6_null_entry;
}

static bool rt6_is_gw_or_nonexthop(const struct fib6_info *rt)
{
	return (rt->fib6_flags & (RTF_NONEXTHOP | RTF_GATEWAY));
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
	struct fib6_info *rt;

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

	if (rinfo->prefix_len == 0)
		rt = rt6_get_dflt_router(net, gwaddr, dev);
	else
		rt = rt6_get_route_info(net, prefix, rinfo->prefix_len,
					gwaddr, dev);

	if (rt && !lifetime) {
		ip6_del_rt(net, rt);
		rt = NULL;
	}

	if (!rt && lifetime)
		rt = rt6_add_route_info(net, prefix, rinfo->prefix_len, gwaddr,
					dev, pref);
	else if (rt)
		rt->fib6_flags = RTF_ROUTEINFO |
				 (rt->fib6_flags & ~RTF_PREF_MASK) | RTF_PREF(pref);

	if (rt) {
		if (!addrconf_finite_timeout(lifetime))
			fib6_clean_expires(rt);
		else
			fib6_set_expires(rt, jiffies + HZ * lifetime);

		fib6_info_release(rt);
	}
	return 0;
}
#endif

/*
 *	Misc support functions
 */

/* called with rcu_lock held */
static struct net_device *ip6_rt_get_dev_rcu(struct fib6_info *rt)
{
	struct net_device *dev = rt->fib6_nh.nh_dev;

	if (rt->fib6_flags & (RTF_LOCAL | RTF_ANYCAST)) {
		/* for copies of local routes, dst->dev needs to be the
		 * device if it is a master device, the master device if
		 * device is enslaved, and the loopback as the default
		 */
		if (netif_is_l3_slave(dev) &&
		    !rt6_need_strict(&rt->fib6_dst.addr))
			dev = l3mdev_master_dev_rcu(dev);
		else if (!netif_is_l3_master(dev))
			dev = dev_net(dev)->loopback_dev;
		/* last case is netif_is_l3_master(dev) is true in which
		 * case we want dev returned to be dev
		 */
	}

	return dev;
}

static const int fib6_prop[RTN_MAX + 1] = {
	[RTN_UNSPEC]	= 0,
	[RTN_UNICAST]	= 0,
	[RTN_LOCAL]	= 0,
	[RTN_BROADCAST]	= 0,
	[RTN_ANYCAST]	= 0,
	[RTN_MULTICAST]	= 0,
	[RTN_BLACKHOLE]	= -EINVAL,
	[RTN_UNREACHABLE] = -EHOSTUNREACH,
	[RTN_PROHIBIT]	= -EACCES,
	[RTN_THROW]	= -EAGAIN,
	[RTN_NAT]	= -EINVAL,
	[RTN_XRESOLVE]	= -EINVAL,
};

static int ip6_rt_type_to_error(u8 fib6_type)
{
	return fib6_prop[fib6_type];
}

static unsigned short fib6_info_dst_flags(struct fib6_info *rt)
{
	unsigned short flags = 0;

	if (rt->dst_nocount)
		flags |= DST_NOCOUNT;
	if (rt->dst_nopolicy)
		flags |= DST_NOPOLICY;
	if (rt->dst_host)
		flags |= DST_HOST;

	return flags;
}

static void ip6_rt_init_dst_reject(struct rt6_info *rt, struct fib6_info *ort)
{
	rt->dst.error = ip6_rt_type_to_error(ort->fib6_type);

	switch (ort->fib6_type) {
	case RTN_BLACKHOLE:
		rt->dst.output = dst_discard_out;
		rt->dst.input = dst_discard;
		break;
	case RTN_PROHIBIT:
		rt->dst.output = ip6_pkt_prohibit_out;
		rt->dst.input = ip6_pkt_prohibit;
		break;
	case RTN_THROW:
	case RTN_UNREACHABLE:
	default:
		rt->dst.output = ip6_pkt_discard_out;
		rt->dst.input = ip6_pkt_discard;
		break;
	}
}

static void ip6_rt_init_dst(struct rt6_info *rt, struct fib6_info *ort)
{
	if (ort->fib6_flags & RTF_REJECT) {
		ip6_rt_init_dst_reject(rt, ort);
		return;
	}

	rt->dst.error = 0;
	rt->dst.output = ip6_output;

	if (ort->fib6_type == RTN_LOCAL || ort->fib6_type == RTN_ANYCAST) {
		rt->dst.input = ip6_input;
	} else if (ipv6_addr_type(&ort->fib6_dst.addr) & IPV6_ADDR_MULTICAST) {
		rt->dst.input = ip6_mc_input;
	} else {
		rt->dst.input = ip6_forward;
	}

	if (ort->fib6_nh.nh_lwtstate) {
		rt->dst.lwtstate = lwtstate_get(ort->fib6_nh.nh_lwtstate);
		lwtunnel_set_redirect(&rt->dst);
	}

	rt->dst.lastuse = jiffies;
}

/* Caller must already hold reference to @from */
static void rt6_set_from(struct rt6_info *rt, struct fib6_info *from)
{
	rt->rt6i_flags &= ~RTF_EXPIRES;
	rcu_assign_pointer(rt->from, from);
	dst_init_metrics(&rt->dst, from->fib6_metrics->metrics, true);
}

/* Caller must already hold reference to @ort */
static void ip6_rt_copy_init(struct rt6_info *rt, struct fib6_info *ort)
{
	struct net_device *dev = fib6_info_nh_dev(ort);

	ip6_rt_init_dst(rt, ort);

	rt->rt6i_dst = ort->fib6_dst;
	rt->rt6i_idev = dev ? in6_dev_get(dev) : NULL;
	rt->rt6i_gateway = ort->fib6_nh.nh_gw;
	rt->rt6i_flags = ort->fib6_flags;
	rt6_set_from(rt, ort);
#ifdef CONFIG_IPV6_SUBTREES
	rt->rt6i_src = ort->fib6_src;
#endif
	rt->rt6i_prefsrc = ort->fib6_prefsrc;
}

static struct fib6_node* fib6_backtrack(struct fib6_node *fn,
					struct in6_addr *saddr)
{
	struct fib6_node *pn, *sn;
	while (1) {
		if (fn->fn_flags & RTN_TL_ROOT)
			return NULL;
		pn = rcu_dereference(fn->parent);
		sn = FIB6_SUBTREE(pn);
		if (sn && sn != fn)
			fn = fib6_node_lookup(sn, NULL, saddr);
		else
			fn = pn;
		if (fn->fn_flags & RTN_RTINFO)
			return fn;
	}
}

static bool ip6_hold_safe(struct net *net, struct rt6_info **prt,
			  bool null_fallback)
{
	struct rt6_info *rt = *prt;

	if (dst_hold_safe(&rt->dst))
		return true;
	if (null_fallback) {
		rt = net->ipv6.ip6_null_entry;
		dst_hold(&rt->dst);
	} else {
		rt = NULL;
	}
	*prt = rt;
	return false;
}

/* called with rcu_lock held */
static struct rt6_info *ip6_create_rt_rcu(struct fib6_info *rt)
{
	unsigned short flags = fib6_info_dst_flags(rt);
	struct net_device *dev = rt->fib6_nh.nh_dev;
	struct rt6_info *nrt;

	if (!fib6_info_hold_safe(rt))
		return NULL;

	nrt = ip6_dst_alloc(dev_net(dev), dev, flags);
	if (nrt)
		ip6_rt_copy_init(nrt, rt);
	else
		fib6_info_release(rt);

	return nrt;
}

static struct rt6_info *ip6_pol_route_lookup(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     const struct sk_buff *skb,
					     int flags)
{
	struct fib6_info *f6i;
	struct fib6_node *fn;
	struct rt6_info *rt;

	if (fl6->flowi6_flags & FLOWI_FLAG_SKIP_NH_OIF)
		flags &= ~RT6_LOOKUP_F_IFACE;

	rcu_read_lock();
	fn = fib6_node_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
restart:
	f6i = rcu_dereference(fn->leaf);
	if (!f6i) {
		f6i = net->ipv6.fib6_null_entry;
	} else {
		f6i = rt6_device_match(net, f6i, &fl6->saddr,
				      fl6->flowi6_oif, flags);
		if (f6i->fib6_nsiblings && fl6->flowi6_oif == 0)
			f6i = fib6_multipath_select(net, f6i, fl6,
						    fl6->flowi6_oif, skb,
						    flags);
	}
	if (f6i == net->ipv6.fib6_null_entry) {
		fn = fib6_backtrack(fn, &fl6->saddr);
		if (fn)
			goto restart;
	}

	trace_fib6_table_lookup(net, f6i, table, fl6);

	/* Search through exception table */
	rt = rt6_find_cached_rt(f6i, &fl6->daddr, &fl6->saddr);
	if (rt) {
		if (ip6_hold_safe(net, &rt, true))
			dst_use_noref(&rt->dst, jiffies);
	} else if (f6i == net->ipv6.fib6_null_entry) {
		rt = net->ipv6.ip6_null_entry;
		dst_hold(&rt->dst);
	} else {
		rt = ip6_create_rt_rcu(f6i);
		if (!rt) {
			rt = net->ipv6.ip6_null_entry;
			dst_hold(&rt->dst);
		}
	}

	rcu_read_unlock();

	return rt;
}

struct dst_entry *ip6_route_lookup(struct net *net, struct flowi6 *fl6,
				   const struct sk_buff *skb, int flags)
{
	return fib6_rule_lookup(net, fl6, skb, flags, ip6_pol_route_lookup);
}
EXPORT_SYMBOL_GPL(ip6_route_lookup);

struct rt6_info *rt6_lookup(struct net *net, const struct in6_addr *daddr,
			    const struct in6_addr *saddr, int oif,
			    const struct sk_buff *skb, int strict)
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

	dst = fib6_rule_lookup(net, &fl6, skb, flags, ip6_pol_route_lookup);
	if (dst->error == 0)
		return (struct rt6_info *) dst;

	dst_release(dst);

	return NULL;
}
EXPORT_SYMBOL(rt6_lookup);

/* ip6_ins_rt is called with FREE table->tb6_lock.
 * It takes new route entry, the addition fails by any reason the
 * route is released.
 * Caller must hold dst before calling it.
 */

static int __ip6_ins_rt(struct fib6_info *rt, struct nl_info *info,
			struct netlink_ext_ack *extack)
{
	int err;
	struct fib6_table *table;

	table = rt->fib6_table;
	spin_lock_bh(&table->tb6_lock);
	err = fib6_add(&table->tb6_root, rt, info, extack);
	spin_unlock_bh(&table->tb6_lock);

	return err;
}

int ip6_ins_rt(struct net *net, struct fib6_info *rt)
{
	struct nl_info info = {	.nl_net = net, };

	return __ip6_ins_rt(rt, &info, NULL);
}

static struct rt6_info *ip6_rt_cache_alloc(struct fib6_info *ort,
					   const struct in6_addr *daddr,
					   const struct in6_addr *saddr)
{
	struct net_device *dev;
	struct rt6_info *rt;

	/*
	 *	Clone the route.
	 */

	if (!fib6_info_hold_safe(ort))
		return NULL;

	dev = ip6_rt_get_dev_rcu(ort);
	rt = ip6_dst_alloc(dev_net(dev), dev, 0);
	if (!rt) {
		fib6_info_release(ort);
		return NULL;
	}

	ip6_rt_copy_init(rt, ort);
	rt->rt6i_flags |= RTF_CACHE;
	rt->dst.flags |= DST_HOST;
	rt->rt6i_dst.addr = *daddr;
	rt->rt6i_dst.plen = 128;

	if (!rt6_is_gw_or_nonexthop(ort)) {
		if (ort->fib6_dst.plen != 128 &&
		    ipv6_addr_equal(&ort->fib6_dst.addr, daddr))
			rt->rt6i_flags |= RTF_ANYCAST;
#ifdef CONFIG_IPV6_SUBTREES
		if (rt->rt6i_src.plen && saddr) {
			rt->rt6i_src.addr = *saddr;
			rt->rt6i_src.plen = 128;
		}
#endif
	}

	return rt;
}

static struct rt6_info *ip6_rt_pcpu_alloc(struct fib6_info *rt)
{
	unsigned short flags = fib6_info_dst_flags(rt);
	struct net_device *dev;
	struct rt6_info *pcpu_rt;

	if (!fib6_info_hold_safe(rt))
		return NULL;

	rcu_read_lock();
	dev = ip6_rt_get_dev_rcu(rt);
	pcpu_rt = ip6_dst_alloc(dev_net(dev), dev, flags);
	rcu_read_unlock();
	if (!pcpu_rt) {
		fib6_info_release(rt);
		return NULL;
	}
	ip6_rt_copy_init(pcpu_rt, rt);
	pcpu_rt->rt6i_flags |= RTF_PCPU;
	return pcpu_rt;
}

/* It should be called with rcu_read_lock() acquired */
static struct rt6_info *rt6_get_pcpu_route(struct fib6_info *rt)
{
	struct rt6_info *pcpu_rt, **p;

	p = this_cpu_ptr(rt->rt6i_pcpu);
	pcpu_rt = *p;

	if (pcpu_rt)
		ip6_hold_safe(NULL, &pcpu_rt, false);

	return pcpu_rt;
}

static struct rt6_info *rt6_make_pcpu_route(struct net *net,
					    struct fib6_info *rt)
{
	struct rt6_info *pcpu_rt, *prev, **p;

	pcpu_rt = ip6_rt_pcpu_alloc(rt);
	if (!pcpu_rt) {
		dst_hold(&net->ipv6.ip6_null_entry->dst);
		return net->ipv6.ip6_null_entry;
	}

	dst_hold(&pcpu_rt->dst);
	p = this_cpu_ptr(rt->rt6i_pcpu);
	prev = cmpxchg(p, NULL, pcpu_rt);
	BUG_ON(prev);

	return pcpu_rt;
}

/* exception hash table implementation
 */
static DEFINE_SPINLOCK(rt6_exception_lock);

/* Remove rt6_ex from hash table and free the memory
 * Caller must hold rt6_exception_lock
 */
static void rt6_remove_exception(struct rt6_exception_bucket *bucket,
				 struct rt6_exception *rt6_ex)
{
	struct net *net;

	if (!bucket || !rt6_ex)
		return;

	net = dev_net(rt6_ex->rt6i->dst.dev);
	hlist_del_rcu(&rt6_ex->hlist);
	dst_release(&rt6_ex->rt6i->dst);
	kfree_rcu(rt6_ex, rcu);
	WARN_ON_ONCE(!bucket->depth);
	bucket->depth--;
	net->ipv6.rt6_stats->fib_rt_cache--;
}

/* Remove oldest rt6_ex in bucket and free the memory
 * Caller must hold rt6_exception_lock
 */
static void rt6_exception_remove_oldest(struct rt6_exception_bucket *bucket)
{
	struct rt6_exception *rt6_ex, *oldest = NULL;

	if (!bucket)
		return;

	hlist_for_each_entry(rt6_ex, &bucket->chain, hlist) {
		if (!oldest || time_before(rt6_ex->stamp, oldest->stamp))
			oldest = rt6_ex;
	}
	rt6_remove_exception(bucket, oldest);
}

static u32 rt6_exception_hash(const struct in6_addr *dst,
			      const struct in6_addr *src)
{
	static u32 seed __read_mostly;
	u32 val;

	net_get_random_once(&seed, sizeof(seed));
	val = jhash(dst, sizeof(*dst), seed);

#ifdef CONFIG_IPV6_SUBTREES
	if (src)
		val = jhash(src, sizeof(*src), val);
#endif
	return hash_32(val, FIB6_EXCEPTION_BUCKET_SIZE_SHIFT);
}

/* Helper function to find the cached rt in the hash table
 * and update bucket pointer to point to the bucket for this
 * (daddr, saddr) pair
 * Caller must hold rt6_exception_lock
 */
static struct rt6_exception *
__rt6_find_exception_spinlock(struct rt6_exception_bucket **bucket,
			      const struct in6_addr *daddr,
			      const struct in6_addr *saddr)
{
	struct rt6_exception *rt6_ex;
	u32 hval;

	if (!(*bucket) || !daddr)
		return NULL;

	hval = rt6_exception_hash(daddr, saddr);
	*bucket += hval;

	hlist_for_each_entry(rt6_ex, &(*bucket)->chain, hlist) {
		struct rt6_info *rt6 = rt6_ex->rt6i;
		bool matched = ipv6_addr_equal(daddr, &rt6->rt6i_dst.addr);

#ifdef CONFIG_IPV6_SUBTREES
		if (matched && saddr)
			matched = ipv6_addr_equal(saddr, &rt6->rt6i_src.addr);
#endif
		if (matched)
			return rt6_ex;
	}
	return NULL;
}

/* Helper function to find the cached rt in the hash table
 * and update bucket pointer to point to the bucket for this
 * (daddr, saddr) pair
 * Caller must hold rcu_read_lock()
 */
static struct rt6_exception *
__rt6_find_exception_rcu(struct rt6_exception_bucket **bucket,
			 const struct in6_addr *daddr,
			 const struct in6_addr *saddr)
{
	struct rt6_exception *rt6_ex;
	u32 hval;

	WARN_ON_ONCE(!rcu_read_lock_held());

	if (!(*bucket) || !daddr)
		return NULL;

	hval = rt6_exception_hash(daddr, saddr);
	*bucket += hval;

	hlist_for_each_entry_rcu(rt6_ex, &(*bucket)->chain, hlist) {
		struct rt6_info *rt6 = rt6_ex->rt6i;
		bool matched = ipv6_addr_equal(daddr, &rt6->rt6i_dst.addr);

#ifdef CONFIG_IPV6_SUBTREES
		if (matched && saddr)
			matched = ipv6_addr_equal(saddr, &rt6->rt6i_src.addr);
#endif
		if (matched)
			return rt6_ex;
	}
	return NULL;
}

static unsigned int fib6_mtu(const struct fib6_info *rt)
{
	unsigned int mtu;

	if (rt->fib6_pmtu) {
		mtu = rt->fib6_pmtu;
	} else {
		struct net_device *dev = fib6_info_nh_dev(rt);
		struct inet6_dev *idev;

		rcu_read_lock();
		idev = __in6_dev_get(dev);
		mtu = idev->cnf.mtu6;
		rcu_read_unlock();
	}

	mtu = min_t(unsigned int, mtu, IP6_MAX_MTU);

	return mtu - lwtunnel_headroom(rt->fib6_nh.nh_lwtstate, mtu);
}

static int rt6_insert_exception(struct rt6_info *nrt,
				struct fib6_info *ort)
{
	struct net *net = dev_net(nrt->dst.dev);
	struct rt6_exception_bucket *bucket;
	struct in6_addr *src_key = NULL;
	struct rt6_exception *rt6_ex;
	int err = 0;

	spin_lock_bh(&rt6_exception_lock);

	if (ort->exception_bucket_flushed) {
		err = -EINVAL;
		goto out;
	}

	bucket = rcu_dereference_protected(ort->rt6i_exception_bucket,
					lockdep_is_held(&rt6_exception_lock));
	if (!bucket) {
		bucket = kcalloc(FIB6_EXCEPTION_BUCKET_SIZE, sizeof(*bucket),
				 GFP_ATOMIC);
		if (!bucket) {
			err = -ENOMEM;
			goto out;
		}
		rcu_assign_pointer(ort->rt6i_exception_bucket, bucket);
	}

#ifdef CONFIG_IPV6_SUBTREES
	/* rt6i_src.plen != 0 indicates ort is in subtree
	 * and exception table is indexed by a hash of
	 * both rt6i_dst and rt6i_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only rt6i_dst.
	 */
	if (ort->fib6_src.plen)
		src_key = &nrt->rt6i_src.addr;
#endif

	/* Update rt6i_prefsrc as it could be changed
	 * in rt6_remove_prefsrc()
	 */
	nrt->rt6i_prefsrc = ort->fib6_prefsrc;
	/* rt6_mtu_change() might lower mtu on ort.
	 * Only insert this exception route if its mtu
	 * is less than ort's mtu value.
	 */
	if (dst_metric_raw(&nrt->dst, RTAX_MTU) >= fib6_mtu(ort)) {
		err = -EINVAL;
		goto out;
	}

	rt6_ex = __rt6_find_exception_spinlock(&bucket, &nrt->rt6i_dst.addr,
					       src_key);
	if (rt6_ex)
		rt6_remove_exception(bucket, rt6_ex);

	rt6_ex = kzalloc(sizeof(*rt6_ex), GFP_ATOMIC);
	if (!rt6_ex) {
		err = -ENOMEM;
		goto out;
	}
	rt6_ex->rt6i = nrt;
	rt6_ex->stamp = jiffies;
	hlist_add_head_rcu(&rt6_ex->hlist, &bucket->chain);
	bucket->depth++;
	net->ipv6.rt6_stats->fib_rt_cache++;

	if (bucket->depth > FIB6_MAX_DEPTH)
		rt6_exception_remove_oldest(bucket);

out:
	spin_unlock_bh(&rt6_exception_lock);

	/* Update fn->fn_sernum to invalidate all cached dst */
	if (!err) {
		spin_lock_bh(&ort->fib6_table->tb6_lock);
		fib6_update_sernum(net, ort);
		spin_unlock_bh(&ort->fib6_table->tb6_lock);
		fib6_force_start_gc(net);
	}

	return err;
}

void rt6_flush_exceptions(struct fib6_info *rt)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct hlist_node *tmp;
	int i;

	spin_lock_bh(&rt6_exception_lock);
	/* Prevent rt6_insert_exception() to recreate the bucket list */
	rt->exception_bucket_flushed = 1;

	bucket = rcu_dereference_protected(rt->rt6i_exception_bucket,
				    lockdep_is_held(&rt6_exception_lock));
	if (!bucket)
		goto out;

	for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
		hlist_for_each_entry_safe(rt6_ex, tmp, &bucket->chain, hlist)
			rt6_remove_exception(bucket, rt6_ex);
		WARN_ON_ONCE(bucket->depth);
		bucket++;
	}

out:
	spin_unlock_bh(&rt6_exception_lock);
}

/* Find cached rt in the hash table inside passed in rt
 * Caller has to hold rcu_read_lock()
 */
static struct rt6_info *rt6_find_cached_rt(struct fib6_info *rt,
					   struct in6_addr *daddr,
					   struct in6_addr *saddr)
{
	struct rt6_exception_bucket *bucket;
	struct in6_addr *src_key = NULL;
	struct rt6_exception *rt6_ex;
	struct rt6_info *res = NULL;

	bucket = rcu_dereference(rt->rt6i_exception_bucket);

#ifdef CONFIG_IPV6_SUBTREES
	/* rt6i_src.plen != 0 indicates rt is in subtree
	 * and exception table is indexed by a hash of
	 * both rt6i_dst and rt6i_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only rt6i_dst.
	 */
	if (rt->fib6_src.plen)
		src_key = saddr;
#endif
	rt6_ex = __rt6_find_exception_rcu(&bucket, daddr, src_key);

	if (rt6_ex && !rt6_check_expired(rt6_ex->rt6i))
		res = rt6_ex->rt6i;

	return res;
}

/* Remove the passed in cached rt from the hash table that contains it */
static int rt6_remove_exception_rt(struct rt6_info *rt)
{
	struct rt6_exception_bucket *bucket;
	struct in6_addr *src_key = NULL;
	struct rt6_exception *rt6_ex;
	struct fib6_info *from;
	int err;

	from = rcu_dereference(rt->from);
	if (!from ||
	    !(rt->rt6i_flags & RTF_CACHE))
		return -EINVAL;

	if (!rcu_access_pointer(from->rt6i_exception_bucket))
		return -ENOENT;

	spin_lock_bh(&rt6_exception_lock);
	bucket = rcu_dereference_protected(from->rt6i_exception_bucket,
				    lockdep_is_held(&rt6_exception_lock));
#ifdef CONFIG_IPV6_SUBTREES
	/* rt6i_src.plen != 0 indicates 'from' is in subtree
	 * and exception table is indexed by a hash of
	 * both rt6i_dst and rt6i_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only rt6i_dst.
	 */
	if (from->fib6_src.plen)
		src_key = &rt->rt6i_src.addr;
#endif
	rt6_ex = __rt6_find_exception_spinlock(&bucket,
					       &rt->rt6i_dst.addr,
					       src_key);
	if (rt6_ex) {
		rt6_remove_exception(bucket, rt6_ex);
		err = 0;
	} else {
		err = -ENOENT;
	}

	spin_unlock_bh(&rt6_exception_lock);
	return err;
}

/* Find rt6_ex which contains the passed in rt cache and
 * refresh its stamp
 */
static void rt6_update_exception_stamp_rt(struct rt6_info *rt)
{
	struct rt6_exception_bucket *bucket;
	struct fib6_info *from = rt->from;
	struct in6_addr *src_key = NULL;
	struct rt6_exception *rt6_ex;

	if (!from ||
	    !(rt->rt6i_flags & RTF_CACHE))
		return;

	rcu_read_lock();
	bucket = rcu_dereference(from->rt6i_exception_bucket);

#ifdef CONFIG_IPV6_SUBTREES
	/* rt6i_src.plen != 0 indicates 'from' is in subtree
	 * and exception table is indexed by a hash of
	 * both rt6i_dst and rt6i_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only rt6i_dst.
	 */
	if (from->fib6_src.plen)
		src_key = &rt->rt6i_src.addr;
#endif
	rt6_ex = __rt6_find_exception_rcu(&bucket,
					  &rt->rt6i_dst.addr,
					  src_key);
	if (rt6_ex)
		rt6_ex->stamp = jiffies;

	rcu_read_unlock();
}

static void rt6_exceptions_remove_prefsrc(struct fib6_info *rt)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	int i;

	bucket = rcu_dereference_protected(rt->rt6i_exception_bucket,
					lockdep_is_held(&rt6_exception_lock));

	if (bucket) {
		for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
			hlist_for_each_entry(rt6_ex, &bucket->chain, hlist) {
				rt6_ex->rt6i->rt6i_prefsrc.plen = 0;
			}
			bucket++;
		}
	}
}

static bool rt6_mtu_change_route_allowed(struct inet6_dev *idev,
					 struct rt6_info *rt, int mtu)
{
	/* If the new MTU is lower than the route PMTU, this new MTU will be the
	 * lowest MTU in the path: always allow updating the route PMTU to
	 * reflect PMTU decreases.
	 *
	 * If the new MTU is higher, and the route PMTU is equal to the local
	 * MTU, this means the old MTU is the lowest in the path, so allow
	 * updating it: if other nodes now have lower MTUs, PMTU discovery will
	 * handle this.
	 */

	if (dst_mtu(&rt->dst) >= mtu)
		return true;

	if (dst_mtu(&rt->dst) == idev->cnf.mtu6)
		return true;

	return false;
}

static void rt6_exceptions_update_pmtu(struct inet6_dev *idev,
				       struct fib6_info *rt, int mtu)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	int i;

	bucket = rcu_dereference_protected(rt->rt6i_exception_bucket,
					lockdep_is_held(&rt6_exception_lock));

	if (!bucket)
		return;

	for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
		hlist_for_each_entry(rt6_ex, &bucket->chain, hlist) {
			struct rt6_info *entry = rt6_ex->rt6i;

			/* For RTF_CACHE with rt6i_pmtu == 0 (i.e. a redirected
			 * route), the metrics of its rt->from have already
			 * been updated.
			 */
			if (dst_metric_raw(&entry->dst, RTAX_MTU) &&
			    rt6_mtu_change_route_allowed(idev, entry, mtu))
				dst_metric_set(&entry->dst, RTAX_MTU, mtu);
		}
		bucket++;
	}
}

#define RTF_CACHE_GATEWAY	(RTF_GATEWAY | RTF_CACHE)

static void rt6_exceptions_clean_tohost(struct fib6_info *rt,
					struct in6_addr *gateway)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct hlist_node *tmp;
	int i;

	if (!rcu_access_pointer(rt->rt6i_exception_bucket))
		return;

	spin_lock_bh(&rt6_exception_lock);
	bucket = rcu_dereference_protected(rt->rt6i_exception_bucket,
				     lockdep_is_held(&rt6_exception_lock));

	if (bucket) {
		for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
			hlist_for_each_entry_safe(rt6_ex, tmp,
						  &bucket->chain, hlist) {
				struct rt6_info *entry = rt6_ex->rt6i;

				if ((entry->rt6i_flags & RTF_CACHE_GATEWAY) ==
				    RTF_CACHE_GATEWAY &&
				    ipv6_addr_equal(gateway,
						    &entry->rt6i_gateway)) {
					rt6_remove_exception(bucket, rt6_ex);
				}
			}
			bucket++;
		}
	}

	spin_unlock_bh(&rt6_exception_lock);
}

static void rt6_age_examine_exception(struct rt6_exception_bucket *bucket,
				      struct rt6_exception *rt6_ex,
				      struct fib6_gc_args *gc_args,
				      unsigned long now)
{
	struct rt6_info *rt = rt6_ex->rt6i;

	/* we are pruning and obsoleting aged-out and non gateway exceptions
	 * even if others have still references to them, so that on next
	 * dst_check() such references can be dropped.
	 * EXPIRES exceptions - e.g. pmtu-generated ones are pruned when
	 * expired, independently from their aging, as per RFC 8201 section 4
	 */
	if (!(rt->rt6i_flags & RTF_EXPIRES)) {
		if (time_after_eq(now, rt->dst.lastuse + gc_args->timeout)) {
			RT6_TRACE("aging clone %p\n", rt);
			rt6_remove_exception(bucket, rt6_ex);
			return;
		}
	} else if (time_after(jiffies, rt->dst.expires)) {
		RT6_TRACE("purging expired route %p\n", rt);
		rt6_remove_exception(bucket, rt6_ex);
		return;
	}

	if (rt->rt6i_flags & RTF_GATEWAY) {
		struct neighbour *neigh;
		__u8 neigh_flags = 0;

		neigh = __ipv6_neigh_lookup_noref(rt->dst.dev, &rt->rt6i_gateway);
		if (neigh)
			neigh_flags = neigh->flags;

		if (!(neigh_flags & NTF_ROUTER)) {
			RT6_TRACE("purging route %p via non-router but gateway\n",
				  rt);
			rt6_remove_exception(bucket, rt6_ex);
			return;
		}
	}

	gc_args->more++;
}

void rt6_age_exceptions(struct fib6_info *rt,
			struct fib6_gc_args *gc_args,
			unsigned long now)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct hlist_node *tmp;
	int i;

	if (!rcu_access_pointer(rt->rt6i_exception_bucket))
		return;

	rcu_read_lock_bh();
	spin_lock(&rt6_exception_lock);
	bucket = rcu_dereference_protected(rt->rt6i_exception_bucket,
				    lockdep_is_held(&rt6_exception_lock));

	if (bucket) {
		for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
			hlist_for_each_entry_safe(rt6_ex, tmp,
						  &bucket->chain, hlist) {
				rt6_age_examine_exception(bucket, rt6_ex,
							  gc_args, now);
			}
			bucket++;
		}
	}
	spin_unlock(&rt6_exception_lock);
	rcu_read_unlock_bh();
}

/* must be called with rcu lock held */
struct fib6_info *fib6_table_lookup(struct net *net, struct fib6_table *table,
				    int oif, struct flowi6 *fl6, int strict)
{
	struct fib6_node *fn, *saved_fn;
	struct fib6_info *f6i;

	fn = fib6_node_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
	saved_fn = fn;

	if (fl6->flowi6_flags & FLOWI_FLAG_SKIP_NH_OIF)
		oif = 0;

redo_rt6_select:
	f6i = rt6_select(net, fn, oif, strict);
	if (f6i == net->ipv6.fib6_null_entry) {
		fn = fib6_backtrack(fn, &fl6->saddr);
		if (fn)
			goto redo_rt6_select;
		else if (strict & RT6_LOOKUP_F_REACHABLE) {
			/* also consider unreachable route */
			strict &= ~RT6_LOOKUP_F_REACHABLE;
			fn = saved_fn;
			goto redo_rt6_select;
		}
	}

	trace_fib6_table_lookup(net, f6i, table, fl6);

	return f6i;
}

struct rt6_info *ip6_pol_route(struct net *net, struct fib6_table *table,
			       int oif, struct flowi6 *fl6,
			       const struct sk_buff *skb, int flags)
{
	struct fib6_info *f6i;
	struct rt6_info *rt;
	int strict = 0;

	strict |= flags & RT6_LOOKUP_F_IFACE;
	strict |= flags & RT6_LOOKUP_F_IGNORE_LINKSTATE;
	if (net->ipv6.devconf_all->forwarding == 0)
		strict |= RT6_LOOKUP_F_REACHABLE;

	rcu_read_lock();

	f6i = fib6_table_lookup(net, table, oif, fl6, strict);
	if (f6i->fib6_nsiblings)
		f6i = fib6_multipath_select(net, f6i, fl6, oif, skb, strict);

	if (f6i == net->ipv6.fib6_null_entry) {
		rt = net->ipv6.ip6_null_entry;
		rcu_read_unlock();
		dst_hold(&rt->dst);
		return rt;
	}

	/*Search through exception table */
	rt = rt6_find_cached_rt(f6i, &fl6->daddr, &fl6->saddr);
	if (rt) {
		if (ip6_hold_safe(net, &rt, true))
			dst_use_noref(&rt->dst, jiffies);

		rcu_read_unlock();
		return rt;
	} else if (unlikely((fl6->flowi6_flags & FLOWI_FLAG_KNOWN_NH) &&
			    !(f6i->fib6_flags & RTF_GATEWAY))) {
		/* Create a RTF_CACHE clone which will not be
		 * owned by the fib6 tree.  It is for the special case where
		 * the daddr in the skb during the neighbor look-up is different
		 * from the fl6->daddr used to look-up route here.
		 */
		struct rt6_info *uncached_rt;

		uncached_rt = ip6_rt_cache_alloc(f6i, &fl6->daddr, NULL);

		rcu_read_unlock();

		if (uncached_rt) {
			/* Uncached_rt's refcnt is taken during ip6_rt_cache_alloc()
			 * No need for another dst_hold()
			 */
			rt6_uncached_list_add(uncached_rt);
			atomic_inc(&net->ipv6.rt6_stats->fib_rt_uncache);
		} else {
			uncached_rt = net->ipv6.ip6_null_entry;
			dst_hold(&uncached_rt->dst);
		}

		return uncached_rt;
	} else {
		/* Get a percpu copy */

		struct rt6_info *pcpu_rt;

		local_bh_disable();
		pcpu_rt = rt6_get_pcpu_route(f6i);

		if (!pcpu_rt)
			pcpu_rt = rt6_make_pcpu_route(net, f6i);

		local_bh_enable();
		rcu_read_unlock();

		return pcpu_rt;
	}
}
EXPORT_SYMBOL_GPL(ip6_pol_route);

static struct rt6_info *ip6_pol_route_input(struct net *net,
					    struct fib6_table *table,
					    struct flowi6 *fl6,
					    const struct sk_buff *skb,
					    int flags)
{
	return ip6_pol_route(net, table, fl6->flowi6_iif, fl6, skb, flags);
}

struct dst_entry *ip6_route_input_lookup(struct net *net,
					 struct net_device *dev,
					 struct flowi6 *fl6,
					 const struct sk_buff *skb,
					 int flags)
{
	if (rt6_need_strict(&fl6->daddr) && dev->type != ARPHRD_PIMREG)
		flags |= RT6_LOOKUP_F_IFACE;

	return fib6_rule_lookup(net, fl6, skb, flags, ip6_pol_route_input);
}
EXPORT_SYMBOL_GPL(ip6_route_input_lookup);

static void ip6_multipath_l3_keys(const struct sk_buff *skb,
				  struct flow_keys *keys,
				  struct flow_keys *flkeys)
{
	const struct ipv6hdr *outer_iph = ipv6_hdr(skb);
	const struct ipv6hdr *key_iph = outer_iph;
	struct flow_keys *_flkeys = flkeys;
	const struct ipv6hdr *inner_iph;
	const struct icmp6hdr *icmph;
	struct ipv6hdr _inner_iph;
	struct icmp6hdr _icmph;

	if (likely(outer_iph->nexthdr != IPPROTO_ICMPV6))
		goto out;

	icmph = skb_header_pointer(skb, skb_transport_offset(skb),
				   sizeof(_icmph), &_icmph);
	if (!icmph)
		goto out;

	if (icmph->icmp6_type != ICMPV6_DEST_UNREACH &&
	    icmph->icmp6_type != ICMPV6_PKT_TOOBIG &&
	    icmph->icmp6_type != ICMPV6_TIME_EXCEED &&
	    icmph->icmp6_type != ICMPV6_PARAMPROB)
		goto out;

	inner_iph = skb_header_pointer(skb,
				       skb_transport_offset(skb) + sizeof(*icmph),
				       sizeof(_inner_iph), &_inner_iph);
	if (!inner_iph)
		goto out;

	key_iph = inner_iph;
	_flkeys = NULL;
out:
	if (_flkeys) {
		keys->addrs.v6addrs.src = _flkeys->addrs.v6addrs.src;
		keys->addrs.v6addrs.dst = _flkeys->addrs.v6addrs.dst;
		keys->tags.flow_label = _flkeys->tags.flow_label;
		keys->basic.ip_proto = _flkeys->basic.ip_proto;
	} else {
		keys->addrs.v6addrs.src = key_iph->saddr;
		keys->addrs.v6addrs.dst = key_iph->daddr;
		keys->tags.flow_label = ip6_flowlabel(key_iph);
		keys->basic.ip_proto = key_iph->nexthdr;
	}
}

/* if skb is set it will be used and fl6 can be NULL */
u32 rt6_multipath_hash(const struct net *net, const struct flowi6 *fl6,
		       const struct sk_buff *skb, struct flow_keys *flkeys)
{
	struct flow_keys hash_keys;
	u32 mhash;

	switch (ip6_multipath_hash_policy(net)) {
	case 0:
		memset(&hash_keys, 0, sizeof(hash_keys));
		hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		if (skb) {
			ip6_multipath_l3_keys(skb, &hash_keys, flkeys);
		} else {
			hash_keys.addrs.v6addrs.src = fl6->saddr;
			hash_keys.addrs.v6addrs.dst = fl6->daddr;
			hash_keys.tags.flow_label = (__force u32)flowi6_get_flowlabel(fl6);
			hash_keys.basic.ip_proto = fl6->flowi6_proto;
		}
		break;
	case 1:
		if (skb) {
			unsigned int flag = FLOW_DISSECTOR_F_STOP_AT_ENCAP;
			struct flow_keys keys;

			/* short-circuit if we already have L4 hash present */
			if (skb->l4_hash)
				return skb_get_hash_raw(skb) >> 1;

			memset(&hash_keys, 0, sizeof(hash_keys));

                        if (!flkeys) {
				skb_flow_dissect_flow_keys(skb, &keys, flag);
				flkeys = &keys;
			}
			hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
			hash_keys.addrs.v6addrs.src = flkeys->addrs.v6addrs.src;
			hash_keys.addrs.v6addrs.dst = flkeys->addrs.v6addrs.dst;
			hash_keys.ports.src = flkeys->ports.src;
			hash_keys.ports.dst = flkeys->ports.dst;
			hash_keys.basic.ip_proto = flkeys->basic.ip_proto;
		} else {
			memset(&hash_keys, 0, sizeof(hash_keys));
			hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
			hash_keys.addrs.v6addrs.src = fl6->saddr;
			hash_keys.addrs.v6addrs.dst = fl6->daddr;
			hash_keys.ports.src = fl6->fl6_sport;
			hash_keys.ports.dst = fl6->fl6_dport;
			hash_keys.basic.ip_proto = fl6->flowi6_proto;
		}
		break;
	}
	mhash = flow_hash_from_keys(&hash_keys);

	return mhash >> 1;
}

void ip6_route_input(struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct net *net = dev_net(skb->dev);
	int flags = RT6_LOOKUP_F_HAS_SADDR;
	struct ip_tunnel_info *tun_info;
	struct flowi6 fl6 = {
		.flowi6_iif = skb->dev->ifindex,
		.daddr = iph->daddr,
		.saddr = iph->saddr,
		.flowlabel = ip6_flowinfo(iph),
		.flowi6_mark = skb->mark,
		.flowi6_proto = iph->nexthdr,
	};
	struct flow_keys *flkeys = NULL, _flkeys;

	tun_info = skb_tunnel_info(skb);
	if (tun_info && !(tun_info->mode & IP_TUNNEL_INFO_TX))
		fl6.flowi6_tun_key.tun_id = tun_info->key.tun_id;

	if (fib6_rules_early_flow_dissect(net, skb, &fl6, &_flkeys))
		flkeys = &_flkeys;

	if (unlikely(fl6.flowi6_proto == IPPROTO_ICMPV6))
		fl6.mp_hash = rt6_multipath_hash(net, &fl6, skb, flkeys);
	skb_dst_drop(skb);
	skb_dst_set(skb,
		    ip6_route_input_lookup(net, skb->dev, &fl6, skb, flags));
}

static struct rt6_info *ip6_pol_route_output(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     const struct sk_buff *skb,
					     int flags)
{
	return ip6_pol_route(net, table, fl6->flowi6_oif, fl6, skb, flags);
}

struct dst_entry *ip6_route_output_flags(struct net *net, const struct sock *sk,
					 struct flowi6 *fl6, int flags)
{
	bool any_src;

	if (rt6_need_strict(&fl6->daddr)) {
		struct dst_entry *dst;

		dst = l3mdev_link_scope_lookup(net, fl6);
		if (dst)
			return dst;
	}

	fl6->flowi6_iif = LOOPBACK_IFINDEX;

	any_src = ipv6_addr_any(&fl6->saddr);
	if ((sk && sk->sk_bound_dev_if) || rt6_need_strict(&fl6->daddr) ||
	    (fl6->flowi6_oif && any_src))
		flags |= RT6_LOOKUP_F_IFACE;

	if (!any_src)
		flags |= RT6_LOOKUP_F_HAS_SADDR;
	else if (sk)
		flags |= rt6_srcprefs2flags(inet6_sk(sk)->srcprefs);

	return fib6_rule_lookup(net, fl6, NULL, flags, ip6_pol_route_output);
}
EXPORT_SYMBOL_GPL(ip6_route_output_flags);

struct dst_entry *ip6_blackhole_route(struct net *net, struct dst_entry *dst_orig)
{
	struct rt6_info *rt, *ort = (struct rt6_info *) dst_orig;
	struct net_device *loopback_dev = net->loopback_dev;
	struct dst_entry *new = NULL;

	rt = dst_alloc(&ip6_dst_blackhole_ops, loopback_dev, 1,
		       DST_OBSOLETE_DEAD, 0);
	if (rt) {
		rt6_info_init(rt);
		atomic_inc(&net->ipv6.rt6_stats->fib_rt_alloc);

		new = &rt->dst;
		new->__use = 1;
		new->input = dst_discard;
		new->output = dst_discard_out;

		dst_copy_metrics(new, &ort->dst);

		rt->rt6i_idev = in6_dev_get(loopback_dev);
		rt->rt6i_gateway = ort->rt6i_gateway;
		rt->rt6i_flags = ort->rt6i_flags & ~RTF_PCPU;

		memcpy(&rt->rt6i_dst, &ort->rt6i_dst, sizeof(struct rt6key));
#ifdef CONFIG_IPV6_SUBTREES
		memcpy(&rt->rt6i_src, &ort->rt6i_src, sizeof(struct rt6key));
#endif
	}

	dst_release(dst_orig);
	return new ? new : ERR_PTR(-ENOMEM);
}

/*
 *	Destination cache support functions
 */

static bool fib6_check(struct fib6_info *f6i, u32 cookie)
{
	u32 rt_cookie = 0;

	if (!fib6_get_cookie_safe(f6i, &rt_cookie) || rt_cookie != cookie)
		return false;

	if (fib6_check_expired(f6i))
		return false;

	return true;
}

static struct dst_entry *rt6_check(struct rt6_info *rt,
				   struct fib6_info *from,
				   u32 cookie)
{
	u32 rt_cookie = 0;

	if ((from && !fib6_get_cookie_safe(from, &rt_cookie)) ||
	    rt_cookie != cookie)
		return NULL;

	if (rt6_check_expired(rt))
		return NULL;

	return &rt->dst;
}

static struct dst_entry *rt6_dst_from_check(struct rt6_info *rt,
					    struct fib6_info *from,
					    u32 cookie)
{
	if (!__rt6_check_expired(rt) &&
	    rt->dst.obsolete == DST_OBSOLETE_FORCE_CHK &&
	    fib6_check(from, cookie))
		return &rt->dst;
	else
		return NULL;
}

static struct dst_entry *ip6_dst_check(struct dst_entry *dst, u32 cookie)
{
	struct dst_entry *dst_ret;
	struct fib6_info *from;
	struct rt6_info *rt;

	rt = container_of(dst, struct rt6_info, dst);

	rcu_read_lock();

	/* All IPV6 dsts are created with ->obsolete set to the value
	 * DST_OBSOLETE_FORCE_CHK which forces validation calls down
	 * into this function always.
	 */

	from = rcu_dereference(rt->from);

	if (from && (rt->rt6i_flags & RTF_PCPU ||
	    unlikely(!list_empty(&rt->rt6i_uncached))))
		dst_ret = rt6_dst_from_check(rt, from, cookie);
	else
		dst_ret = rt6_check(rt, from, cookie);

	rcu_read_unlock();

	return dst_ret;
}

static struct dst_entry *ip6_negative_advice(struct dst_entry *dst)
{
	struct rt6_info *rt = (struct rt6_info *) dst;

	if (rt) {
		if (rt->rt6i_flags & RTF_CACHE) {
			rcu_read_lock();
			if (rt6_check_expired(rt)) {
				rt6_remove_exception_rt(rt);
				dst = NULL;
			}
			rcu_read_unlock();
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
		rcu_read_lock();
		if (rt->rt6i_flags & RTF_CACHE) {
			if (dst_hold_safe(&rt->dst))
				rt6_remove_exception_rt(rt);
		} else {
			struct fib6_info *from;
			struct fib6_node *fn;

			from = rcu_dereference(rt->from);
			if (from) {
				fn = rcu_dereference(from->fib6_node);
				if (fn && (rt->rt6i_flags & RTF_DEFAULT))
					fn->fn_sernum = -1;
			}
		}
		rcu_read_unlock();
	}
}

static void rt6_update_expires(struct rt6_info *rt0, int timeout)
{
	if (!(rt0->rt6i_flags & RTF_EXPIRES)) {
		struct fib6_info *from;

		rcu_read_lock();
		from = rcu_dereference(rt0->from);
		if (from)
			rt0->dst.expires = from->expires;
		rcu_read_unlock();
	}

	dst_set_expires(&rt0->dst, timeout);
	rt0->rt6i_flags |= RTF_EXPIRES;
}

static void rt6_do_update_pmtu(struct rt6_info *rt, u32 mtu)
{
	struct net *net = dev_net(rt->dst.dev);

	dst_metric_set(&rt->dst, RTAX_MTU, mtu);
	rt->rt6i_flags |= RTF_MODIFIED;
	rt6_update_expires(rt, net->ipv6.sysctl.ip6_rt_mtu_expires);
}

static bool rt6_cache_allowed_for_pmtu(const struct rt6_info *rt)
{
	bool from_set;

	rcu_read_lock();
	from_set = !!rcu_dereference(rt->from);
	rcu_read_unlock();

	return !(rt->rt6i_flags & RTF_CACHE) &&
		(rt->rt6i_flags & RTF_PCPU || from_set);
}

static void __ip6_rt_update_pmtu(struct dst_entry *dst, const struct sock *sk,
				 const struct ipv6hdr *iph, u32 mtu)
{
	const struct in6_addr *daddr, *saddr;
	struct rt6_info *rt6 = (struct rt6_info *)dst;

	if (dst_metric_locked(dst, RTAX_MTU))
		return;

	if (iph) {
		daddr = &iph->daddr;
		saddr = &iph->saddr;
	} else if (sk) {
		daddr = &sk->sk_v6_daddr;
		saddr = &inet6_sk(sk)->saddr;
	} else {
		daddr = NULL;
		saddr = NULL;
	}
	dst_confirm_neigh(dst, daddr);
	mtu = max_t(u32, mtu, IPV6_MIN_MTU);
	if (mtu >= dst_mtu(dst))
		return;

	if (!rt6_cache_allowed_for_pmtu(rt6)) {
		rt6_do_update_pmtu(rt6, mtu);
		/* update rt6_ex->stamp for cache */
		if (rt6->rt6i_flags & RTF_CACHE)
			rt6_update_exception_stamp_rt(rt6);
	} else if (daddr) {
		struct fib6_info *from;
		struct rt6_info *nrt6;

		rcu_read_lock();
		from = rcu_dereference(rt6->from);
		nrt6 = ip6_rt_cache_alloc(from, daddr, saddr);
		if (nrt6) {
			rt6_do_update_pmtu(nrt6, mtu);
			if (rt6_insert_exception(nrt6, from))
				dst_release_immediate(&nrt6->dst);
		}
		rcu_read_unlock();
	}
}

static void ip6_rt_update_pmtu(struct dst_entry *dst, struct sock *sk,
			       struct sk_buff *skb, u32 mtu)
{
	__ip6_rt_update_pmtu(dst, sk, skb ? ipv6_hdr(skb) : NULL, mtu);
}

void ip6_update_pmtu(struct sk_buff *skb, struct net *net, __be32 mtu,
		     int oif, u32 mark, kuid_t uid)
{
	const struct ipv6hdr *iph = (struct ipv6hdr *) skb->data;
	struct dst_entry *dst;
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_oif = oif;
	fl6.flowi6_mark = mark ? mark : IP6_REPLY_MARK(net, skb->mark);
	fl6.daddr = iph->daddr;
	fl6.saddr = iph->saddr;
	fl6.flowlabel = ip6_flowinfo(iph);
	fl6.flowi6_uid = uid;

	dst = ip6_route_output(net, NULL, &fl6);
	if (!dst->error)
		__ip6_rt_update_pmtu(dst, NULL, iph, ntohl(mtu));
	dst_release(dst);
}
EXPORT_SYMBOL_GPL(ip6_update_pmtu);

void ip6_sk_update_pmtu(struct sk_buff *skb, struct sock *sk, __be32 mtu)
{
	struct dst_entry *dst;

	ip6_update_pmtu(skb, sock_net(sk), mtu,
			sk->sk_bound_dev_if, sk->sk_mark, sk->sk_uid);

	dst = __sk_dst_get(sk);
	if (!dst || !dst->obsolete ||
	    dst->ops->check(dst, inet6_sk(sk)->dst_cookie))
		return;

	bh_lock_sock(sk);
	if (!sock_owned_by_user(sk) && !ipv6_addr_v4mapped(&sk->sk_v6_daddr))
		ip6_datagram_dst_update(sk, false);
	bh_unlock_sock(sk);
}
EXPORT_SYMBOL_GPL(ip6_sk_update_pmtu);

void ip6_sk_dst_store_flow(struct sock *sk, struct dst_entry *dst,
			   const struct flowi6 *fl6)
{
#ifdef CONFIG_IPV6_SUBTREES
	struct ipv6_pinfo *np = inet6_sk(sk);
#endif

	ip6_dst_store(sk, dst,
		      ipv6_addr_equal(&fl6->daddr, &sk->sk_v6_daddr) ?
		      &sk->sk_v6_daddr : NULL,
#ifdef CONFIG_IPV6_SUBTREES
		      ipv6_addr_equal(&fl6->saddr, &np->saddr) ?
		      &np->saddr :
#endif
		      NULL);
}

/* Handle redirects */
struct ip6rd_flowi {
	struct flowi6 fl6;
	struct in6_addr gateway;
};

static struct rt6_info *__ip6_route_redirect(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     const struct sk_buff *skb,
					     int flags)
{
	struct ip6rd_flowi *rdfl = (struct ip6rd_flowi *)fl6;
	struct rt6_info *ret = NULL, *rt_cache;
	struct fib6_info *rt;
	struct fib6_node *fn;

	/* Get the "current" route for this destination and
	 * check if the redirect has come from appropriate router.
	 *
	 * RFC 4861 specifies that redirects should only be
	 * accepted if they come from the nexthop to the target.
	 * Due to the way the routes are chosen, this notion
	 * is a bit fuzzy and one might need to check all possible
	 * routes.
	 */

	rcu_read_lock();
	fn = fib6_node_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
restart:
	for_each_fib6_node_rt_rcu(fn) {
		if (rt->fib6_nh.nh_flags & RTNH_F_DEAD)
			continue;
		if (fib6_check_expired(rt))
			continue;
		if (rt->fib6_flags & RTF_REJECT)
			break;
		if (!(rt->fib6_flags & RTF_GATEWAY))
			continue;
		if (fl6->flowi6_oif != rt->fib6_nh.nh_dev->ifindex)
			continue;
		/* rt_cache's gateway might be different from its 'parent'
		 * in the case of an ip redirect.
		 * So we keep searching in the exception table if the gateway
		 * is different.
		 */
		if (!ipv6_addr_equal(&rdfl->gateway, &rt->fib6_nh.nh_gw)) {
			rt_cache = rt6_find_cached_rt(rt,
						      &fl6->daddr,
						      &fl6->saddr);
			if (rt_cache &&
			    ipv6_addr_equal(&rdfl->gateway,
					    &rt_cache->rt6i_gateway)) {
				ret = rt_cache;
				break;
			}
			continue;
		}
		break;
	}

	if (!rt)
		rt = net->ipv6.fib6_null_entry;
	else if (rt->fib6_flags & RTF_REJECT) {
		ret = net->ipv6.ip6_null_entry;
		goto out;
	}

	if (rt == net->ipv6.fib6_null_entry) {
		fn = fib6_backtrack(fn, &fl6->saddr);
		if (fn)
			goto restart;
	}

out:
	if (ret)
		ip6_hold_safe(net, &ret, true);
	else
		ret = ip6_create_rt_rcu(rt);

	rcu_read_unlock();

	trace_fib6_table_lookup(net, rt, table, fl6);
	return ret;
};

static struct dst_entry *ip6_route_redirect(struct net *net,
					    const struct flowi6 *fl6,
					    const struct sk_buff *skb,
					    const struct in6_addr *gateway)
{
	int flags = RT6_LOOKUP_F_HAS_SADDR;
	struct ip6rd_flowi rdfl;

	rdfl.fl6 = *fl6;
	rdfl.gateway = *gateway;

	return fib6_rule_lookup(net, &rdfl.fl6, skb,
				flags, __ip6_route_redirect);
}

void ip6_redirect(struct sk_buff *skb, struct net *net, int oif, u32 mark,
		  kuid_t uid)
{
	const struct ipv6hdr *iph = (struct ipv6hdr *) skb->data;
	struct dst_entry *dst;
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_iif = LOOPBACK_IFINDEX;
	fl6.flowi6_oif = oif;
	fl6.flowi6_mark = mark;
	fl6.daddr = iph->daddr;
	fl6.saddr = iph->saddr;
	fl6.flowlabel = ip6_flowinfo(iph);
	fl6.flowi6_uid = uid;

	dst = ip6_route_redirect(net, &fl6, skb, &ipv6_hdr(skb)->saddr);
	rt6_do_redirect(dst, NULL, skb);
	dst_release(dst);
}
EXPORT_SYMBOL_GPL(ip6_redirect);

void ip6_redirect_no_header(struct sk_buff *skb, struct net *net, int oif,
			    u32 mark)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct rd_msg *msg = (struct rd_msg *)icmp6_hdr(skb);
	struct dst_entry *dst;
	struct flowi6 fl6;

	memset(&fl6, 0, sizeof(fl6));
	fl6.flowi6_iif = LOOPBACK_IFINDEX;
	fl6.flowi6_oif = oif;
	fl6.flowi6_mark = mark;
	fl6.daddr = msg->dest;
	fl6.saddr = iph->daddr;
	fl6.flowi6_uid = sock_net_uid(net, NULL);

	dst = ip6_route_redirect(net, &fl6, skb, &iph->saddr);
	rt6_do_redirect(dst, NULL, skb);
	dst_release(dst);
}

void ip6_sk_redirect(struct sk_buff *skb, struct sock *sk)
{
	ip6_redirect(skb, sock_net(sk), sk->sk_bound_dev_if, sk->sk_mark,
		     sk->sk_uid);
}
EXPORT_SYMBOL_GPL(ip6_sk_redirect);

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

static unsigned int ip6_mtu(const struct dst_entry *dst)
{
	struct inet6_dev *idev;
	unsigned int mtu;

	mtu = dst_metric_raw(dst, RTAX_MTU);
	if (mtu)
		goto out;

	mtu = IPV6_MIN_MTU;

	rcu_read_lock();
	idev = __in6_dev_get(dst->dev);
	if (idev)
		mtu = idev->cnf.mtu6;
	rcu_read_unlock();

out:
	mtu = min_t(unsigned int, mtu, IP6_MAX_MTU);

	return mtu - lwtunnel_headroom(dst->lwtstate, mtu);
}

/* MTU selection:
 * 1. mtu on route is locked - use it
 * 2. mtu from nexthop exception
 * 3. mtu from egress device
 *
 * based on ip6_dst_mtu_forward and exception logic of
 * rt6_find_cached_rt; called with rcu_read_lock
 */
u32 ip6_mtu_from_fib6(struct fib6_info *f6i, struct in6_addr *daddr,
		      struct in6_addr *saddr)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct in6_addr *src_key;
	struct inet6_dev *idev;
	u32 mtu = 0;

	if (unlikely(fib6_metric_locked(f6i, RTAX_MTU))) {
		mtu = f6i->fib6_pmtu;
		if (mtu)
			goto out;
	}

	src_key = NULL;
#ifdef CONFIG_IPV6_SUBTREES
	if (f6i->fib6_src.plen)
		src_key = saddr;
#endif

	bucket = rcu_dereference(f6i->rt6i_exception_bucket);
	rt6_ex = __rt6_find_exception_rcu(&bucket, daddr, src_key);
	if (rt6_ex && !rt6_check_expired(rt6_ex->rt6i))
		mtu = dst_metric_raw(&rt6_ex->rt6i->dst, RTAX_MTU);

	if (likely(!mtu)) {
		struct net_device *dev = fib6_info_nh_dev(f6i);

		mtu = IPV6_MIN_MTU;
		idev = __in6_dev_get(dev);
		if (idev && idev->cnf.mtu6 > mtu)
			mtu = idev->cnf.mtu6;
	}

	mtu = min_t(unsigned int, mtu, IP6_MAX_MTU);
out:
	return mtu - lwtunnel_headroom(fib6_info_nh_lwt(f6i), mtu);
}

struct dst_entry *icmp6_dst_alloc(struct net_device *dev,
				  struct flowi6 *fl6)
{
	struct dst_entry *dst;
	struct rt6_info *rt;
	struct inet6_dev *idev = in6_dev_get(dev);
	struct net *net = dev_net(dev);

	if (unlikely(!idev))
		return ERR_PTR(-ENODEV);

	rt = ip6_dst_alloc(net, dev, 0);
	if (unlikely(!rt)) {
		in6_dev_put(idev);
		dst = ERR_PTR(-ENOMEM);
		goto out;
	}

	rt->dst.flags |= DST_HOST;
	rt->dst.input = ip6_input;
	rt->dst.output  = ip6_output;
	rt->rt6i_gateway  = fl6->daddr;
	rt->rt6i_dst.addr = fl6->daddr;
	rt->rt6i_dst.plen = 128;
	rt->rt6i_idev     = idev;
	dst_metric_set(&rt->dst, RTAX_HOPLIMIT, 0);

	/* Add this dst into uncached_list so that rt6_disable_ip() can
	 * do proper release of the net_device
	 */
	rt6_uncached_list_add(rt);
	atomic_inc(&net->ipv6.rt6_stats->fib_rt_uncache);

	dst = xfrm_lookup(net, &rt->dst, flowi6_to_flowi(fl6), NULL, 0);

out:
	return dst;
}

static int ip6_dst_gc(struct dst_ops *ops)
{
	struct net *net = container_of(ops, struct net, ipv6.ip6_dst_ops);
	int rt_min_interval = net->ipv6.sysctl.ip6_rt_gc_min_interval;
	int rt_max_size = net->ipv6.sysctl.ip6_rt_max_size;
	int rt_elasticity = net->ipv6.sysctl.ip6_rt_gc_elasticity;
	int rt_gc_timeout = net->ipv6.sysctl.ip6_rt_gc_timeout;
	unsigned long rt_last_gc = net->ipv6.ip6_rt_last_gc;
	int entries;

	entries = dst_entries_get_fast(ops);
	if (time_after(rt_last_gc + rt_min_interval, jiffies) &&
	    entries <= rt_max_size)
		goto out;

	net->ipv6.ip6_rt_gc_expire++;
	fib6_run_gc(net->ipv6.ip6_rt_gc_expire, net, true);
	entries = dst_entries_get_slow(ops);
	if (entries < ops->gc_thresh)
		net->ipv6.ip6_rt_gc_expire = rt_gc_timeout>>1;
out:
	net->ipv6.ip6_rt_gc_expire -= net->ipv6.ip6_rt_gc_expire>>rt_elasticity;
	return entries > rt_max_size;
}

static int ip6_convert_metrics(struct net *net, struct fib6_info *rt,
			       struct fib6_config *cfg)
{
	struct dst_metrics *p;

	if (!cfg->fc_mx)
		return 0;

	p = kzalloc(sizeof(*rt->fib6_metrics), GFP_KERNEL);
	if (unlikely(!p))
		return -ENOMEM;

	refcount_set(&p->refcnt, 1);
	rt->fib6_metrics = p;

	return ip_metrics_convert(net, cfg->fc_mx, cfg->fc_mx_len, p->metrics);
}

static struct rt6_info *ip6_nh_lookup_table(struct net *net,
					    struct fib6_config *cfg,
					    const struct in6_addr *gw_addr,
					    u32 tbid, int flags)
{
	struct flowi6 fl6 = {
		.flowi6_oif = cfg->fc_ifindex,
		.daddr = *gw_addr,
		.saddr = cfg->fc_prefsrc,
	};
	struct fib6_table *table;
	struct rt6_info *rt;

	table = fib6_get_table(net, tbid);
	if (!table)
		return NULL;

	if (!ipv6_addr_any(&cfg->fc_prefsrc))
		flags |= RT6_LOOKUP_F_HAS_SADDR;

	flags |= RT6_LOOKUP_F_IGNORE_LINKSTATE;
	rt = ip6_pol_route(net, table, cfg->fc_ifindex, &fl6, NULL, flags);

	/* if table lookup failed, fall back to full lookup */
	if (rt == net->ipv6.ip6_null_entry) {
		ip6_rt_put(rt);
		rt = NULL;
	}

	return rt;
}

static int ip6_route_check_nh_onlink(struct net *net,
				     struct fib6_config *cfg,
				     const struct net_device *dev,
				     struct netlink_ext_ack *extack)
{
	u32 tbid = l3mdev_fib_table(dev) ? : RT_TABLE_MAIN;
	const struct in6_addr *gw_addr = &cfg->fc_gateway;
	u32 flags = RTF_LOCAL | RTF_ANYCAST | RTF_REJECT;
	struct rt6_info *grt;
	int err;

	err = 0;
	grt = ip6_nh_lookup_table(net, cfg, gw_addr, tbid, 0);
	if (grt) {
		if (!grt->dst.error &&
		    (grt->rt6i_flags & flags || dev != grt->dst.dev)) {
			NL_SET_ERR_MSG(extack,
				       "Nexthop has invalid gateway or device mismatch");
			err = -EINVAL;
		}

		ip6_rt_put(grt);
	}

	return err;
}

static int ip6_route_check_nh(struct net *net,
			      struct fib6_config *cfg,
			      struct net_device **_dev,
			      struct inet6_dev **idev)
{
	const struct in6_addr *gw_addr = &cfg->fc_gateway;
	struct net_device *dev = _dev ? *_dev : NULL;
	struct rt6_info *grt = NULL;
	int err = -EHOSTUNREACH;

	if (cfg->fc_table) {
		int flags = RT6_LOOKUP_F_IFACE;

		grt = ip6_nh_lookup_table(net, cfg, gw_addr,
					  cfg->fc_table, flags);
		if (grt) {
			if (grt->rt6i_flags & RTF_GATEWAY ||
			    (dev && dev != grt->dst.dev)) {
				ip6_rt_put(grt);
				grt = NULL;
			}
		}
	}

	if (!grt)
		grt = rt6_lookup(net, gw_addr, NULL, cfg->fc_ifindex, NULL, 1);

	if (!grt)
		goto out;

	if (dev) {
		if (dev != grt->dst.dev) {
			ip6_rt_put(grt);
			goto out;
		}
	} else {
		*_dev = dev = grt->dst.dev;
		*idev = grt->rt6i_idev;
		dev_hold(dev);
		in6_dev_hold(grt->rt6i_idev);
	}

	if (!(grt->rt6i_flags & RTF_GATEWAY))
		err = 0;

	ip6_rt_put(grt);

out:
	return err;
}

static int ip6_validate_gw(struct net *net, struct fib6_config *cfg,
			   struct net_device **_dev, struct inet6_dev **idev,
			   struct netlink_ext_ack *extack)
{
	const struct in6_addr *gw_addr = &cfg->fc_gateway;
	int gwa_type = ipv6_addr_type(gw_addr);
	bool skip_dev = gwa_type & IPV6_ADDR_LINKLOCAL ? false : true;
	const struct net_device *dev = *_dev;
	bool need_addr_check = !dev;
	int err = -EINVAL;

	/* if gw_addr is local we will fail to detect this in case
	 * address is still TENTATIVE (DAD in progress). rt6_lookup()
	 * will return already-added prefix route via interface that
	 * prefix route was assigned to, which might be non-loopback.
	 */
	if (dev &&
	    ipv6_chk_addr_and_flags(net, gw_addr, dev, skip_dev, 0, 0)) {
		NL_SET_ERR_MSG(extack, "Gateway can not be a local address");
		goto out;
	}

	if (gwa_type != (IPV6_ADDR_LINKLOCAL | IPV6_ADDR_UNICAST)) {
		/* IPv6 strictly inhibits using not link-local
		 * addresses as nexthop address.
		 * Otherwise, router will not able to send redirects.
		 * It is very good, but in some (rare!) circumstances
		 * (SIT, PtP, NBMA NOARP links) it is handy to allow
		 * some exceptions. --ANK
		 * We allow IPv4-mapped nexthops to support RFC4798-type
		 * addressing
		 */
		if (!(gwa_type & (IPV6_ADDR_UNICAST | IPV6_ADDR_MAPPED))) {
			NL_SET_ERR_MSG(extack, "Invalid gateway address");
			goto out;
		}

		if (cfg->fc_flags & RTNH_F_ONLINK)
			err = ip6_route_check_nh_onlink(net, cfg, dev, extack);
		else
			err = ip6_route_check_nh(net, cfg, _dev, idev);

		if (err)
			goto out;
	}

	/* reload in case device was changed */
	dev = *_dev;

	err = -EINVAL;
	if (!dev) {
		NL_SET_ERR_MSG(extack, "Egress device not specified");
		goto out;
	} else if (dev->flags & IFF_LOOPBACK) {
		NL_SET_ERR_MSG(extack,
			       "Egress device can not be loopback device for this route");
		goto out;
	}

	/* if we did not check gw_addr above, do so now that the
	 * egress device has been resolved.
	 */
	if (need_addr_check &&
	    ipv6_chk_addr_and_flags(net, gw_addr, dev, skip_dev, 0, 0)) {
		NL_SET_ERR_MSG(extack, "Gateway can not be a local address");
		goto out;
	}

	err = 0;
out:
	return err;
}

static struct fib6_info *ip6_route_info_create(struct fib6_config *cfg,
					      gfp_t gfp_flags,
					      struct netlink_ext_ack *extack)
{
	struct net *net = cfg->fc_nlinfo.nl_net;
	struct fib6_info *rt = NULL;
	struct net_device *dev = NULL;
	struct inet6_dev *idev = NULL;
	struct fib6_table *table;
	int addr_type;
	int err = -EINVAL;

	/* RTF_PCPU is an internal flag; can not be set by userspace */
	if (cfg->fc_flags & RTF_PCPU) {
		NL_SET_ERR_MSG(extack, "Userspace can not set RTF_PCPU");
		goto out;
	}

	/* RTF_CACHE is an internal flag; can not be set by userspace */
	if (cfg->fc_flags & RTF_CACHE) {
		NL_SET_ERR_MSG(extack, "Userspace can not set RTF_CACHE");
		goto out;
	}

	if (cfg->fc_type > RTN_MAX) {
		NL_SET_ERR_MSG(extack, "Invalid route type");
		goto out;
	}

	if (cfg->fc_dst_len > 128) {
		NL_SET_ERR_MSG(extack, "Invalid prefix length");
		goto out;
	}
	if (cfg->fc_src_len > 128) {
		NL_SET_ERR_MSG(extack, "Invalid source address length");
		goto out;
	}
#ifndef CONFIG_IPV6_SUBTREES
	if (cfg->fc_src_len) {
		NL_SET_ERR_MSG(extack,
			       "Specifying source address requires IPV6_SUBTREES to be enabled");
		goto out;
	}
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

	if (cfg->fc_flags & RTNH_F_ONLINK) {
		if (!dev) {
			NL_SET_ERR_MSG(extack,
				       "Nexthop device required for onlink");
			err = -ENODEV;
			goto out;
		}

		if (!(dev->flags & IFF_UP)) {
			NL_SET_ERR_MSG(extack, "Nexthop device is not up");
			err = -ENETDOWN;
			goto out;
		}
	}

	err = -ENOBUFS;
	if (cfg->fc_nlinfo.nlh &&
	    !(cfg->fc_nlinfo.nlh->nlmsg_flags & NLM_F_CREATE)) {
		table = fib6_get_table(net, cfg->fc_table);
		if (!table) {
			pr_warn("NLM_F_CREATE should be specified when creating new route\n");
			table = fib6_new_table(net, cfg->fc_table);
		}
	} else {
		table = fib6_new_table(net, cfg->fc_table);
	}

	if (!table)
		goto out;

	err = -ENOMEM;
	rt = fib6_info_alloc(gfp_flags);
	if (!rt)
		goto out;

	if (cfg->fc_flags & RTF_ADDRCONF)
		rt->dst_nocount = true;

	err = ip6_convert_metrics(net, rt, cfg);
	if (err < 0)
		goto out;

	if (cfg->fc_flags & RTF_EXPIRES)
		fib6_set_expires(rt, jiffies +
				clock_t_to_jiffies(cfg->fc_expires));
	else
		fib6_clean_expires(rt);

	if (cfg->fc_protocol == RTPROT_UNSPEC)
		cfg->fc_protocol = RTPROT_BOOT;
	rt->fib6_protocol = cfg->fc_protocol;

	addr_type = ipv6_addr_type(&cfg->fc_dst);

	if (cfg->fc_encap) {
		struct lwtunnel_state *lwtstate;

		err = lwtunnel_build_state(cfg->fc_encap_type,
					   cfg->fc_encap, AF_INET6, cfg,
					   &lwtstate, extack);
		if (err)
			goto out;
		rt->fib6_nh.nh_lwtstate = lwtstate_get(lwtstate);
	}

	ipv6_addr_prefix(&rt->fib6_dst.addr, &cfg->fc_dst, cfg->fc_dst_len);
	rt->fib6_dst.plen = cfg->fc_dst_len;
	if (rt->fib6_dst.plen == 128)
		rt->dst_host = true;

#ifdef CONFIG_IPV6_SUBTREES
	ipv6_addr_prefix(&rt->fib6_src.addr, &cfg->fc_src, cfg->fc_src_len);
	rt->fib6_src.plen = cfg->fc_src_len;
#endif

	rt->fib6_metric = cfg->fc_metric;
	rt->fib6_nh.nh_weight = 1;

	rt->fib6_type = cfg->fc_type;

	/* We cannot add true routes via loopback here,
	   they would result in kernel looping; promote them to reject routes
	 */
	if ((cfg->fc_flags & RTF_REJECT) ||
	    (dev && (dev->flags & IFF_LOOPBACK) &&
	     !(addr_type & IPV6_ADDR_LOOPBACK) &&
	     !(cfg->fc_flags & RTF_LOCAL))) {
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
		rt->fib6_flags = RTF_REJECT|RTF_NONEXTHOP;
		goto install_route;
	}

	if (cfg->fc_flags & RTF_GATEWAY) {
		err = ip6_validate_gw(net, cfg, &dev, &idev, extack);
		if (err)
			goto out;

		rt->fib6_nh.nh_gw = cfg->fc_gateway;
	}

	err = -ENODEV;
	if (!dev)
		goto out;

	if (idev->cnf.disable_ipv6) {
		NL_SET_ERR_MSG(extack, "IPv6 is disabled on nexthop device");
		err = -EACCES;
		goto out;
	}

	if (!(dev->flags & IFF_UP)) {
		NL_SET_ERR_MSG(extack, "Nexthop device is not up");
		err = -ENETDOWN;
		goto out;
	}

	if (!ipv6_addr_any(&cfg->fc_prefsrc)) {
		if (!ipv6_chk_addr(net, &cfg->fc_prefsrc, dev, 0)) {
			NL_SET_ERR_MSG(extack, "Invalid source address");
			err = -EINVAL;
			goto out;
		}
		rt->fib6_prefsrc.addr = cfg->fc_prefsrc;
		rt->fib6_prefsrc.plen = 128;
	} else
		rt->fib6_prefsrc.plen = 0;

	rt->fib6_flags = cfg->fc_flags;

install_route:
	if (!(rt->fib6_flags & (RTF_LOCAL | RTF_ANYCAST)) &&
	    !netif_carrier_ok(dev))
		rt->fib6_nh.nh_flags |= RTNH_F_LINKDOWN;
	rt->fib6_nh.nh_flags |= (cfg->fc_flags & RTNH_F_ONLINK);
	rt->fib6_nh.nh_dev = dev;
	rt->fib6_table = table;

	cfg->fc_nlinfo.nl_net = dev_net(dev);

	if (idev)
		in6_dev_put(idev);

	return rt;
out:
	if (dev)
		dev_put(dev);
	if (idev)
		in6_dev_put(idev);

	fib6_info_release(rt);
	return ERR_PTR(err);
}

int ip6_route_add(struct fib6_config *cfg, gfp_t gfp_flags,
		  struct netlink_ext_ack *extack)
{
	struct fib6_info *rt;
	int err;

	rt = ip6_route_info_create(cfg, gfp_flags, extack);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	err = __ip6_ins_rt(rt, &cfg->fc_nlinfo, extack);
	fib6_info_release(rt);

	return err;
}

static int __ip6_del_rt(struct fib6_info *rt, struct nl_info *info)
{
	struct net *net = info->nl_net;
	struct fib6_table *table;
	int err;

	if (rt == net->ipv6.fib6_null_entry) {
		err = -ENOENT;
		goto out;
	}

	table = rt->fib6_table;
	spin_lock_bh(&table->tb6_lock);
	err = fib6_del(rt, info);
	spin_unlock_bh(&table->tb6_lock);

out:
	fib6_info_release(rt);
	return err;
}

int ip6_del_rt(struct net *net, struct fib6_info *rt)
{
	struct nl_info info = { .nl_net = net };

	return __ip6_del_rt(rt, &info);
}

static int __ip6_del_rt_siblings(struct fib6_info *rt, struct fib6_config *cfg)
{
	struct nl_info *info = &cfg->fc_nlinfo;
	struct net *net = info->nl_net;
	struct sk_buff *skb = NULL;
	struct fib6_table *table;
	int err = -ENOENT;

	if (rt == net->ipv6.fib6_null_entry)
		goto out_put;
	table = rt->fib6_table;
	spin_lock_bh(&table->tb6_lock);

	if (rt->fib6_nsiblings && cfg->fc_delete_all_nh) {
		struct fib6_info *sibling, *next_sibling;

		/* prefer to send a single notification with all hops */
		skb = nlmsg_new(rt6_nlmsg_size(rt), gfp_any());
		if (skb) {
			u32 seq = info->nlh ? info->nlh->nlmsg_seq : 0;

			if (rt6_fill_node(net, skb, rt, NULL,
					  NULL, NULL, 0, RTM_DELROUTE,
					  info->portid, seq, 0) < 0) {
				kfree_skb(skb);
				skb = NULL;
			} else
				info->skip_notify = 1;
		}

		list_for_each_entry_safe(sibling, next_sibling,
					 &rt->fib6_siblings,
					 fib6_siblings) {
			err = fib6_del(sibling, info);
			if (err)
				goto out_unlock;
		}
	}

	err = fib6_del(rt, info);
out_unlock:
	spin_unlock_bh(&table->tb6_lock);
out_put:
	fib6_info_release(rt);

	if (skb) {
		rtnl_notify(skb, net, info->portid, RTNLGRP_IPV6_ROUTE,
			    info->nlh, gfp_any());
	}
	return err;
}

static int ip6_del_cached_rt(struct rt6_info *rt, struct fib6_config *cfg)
{
	int rc = -ESRCH;

	if (cfg->fc_ifindex && rt->dst.dev->ifindex != cfg->fc_ifindex)
		goto out;

	if (cfg->fc_flags & RTF_GATEWAY &&
	    !ipv6_addr_equal(&cfg->fc_gateway, &rt->rt6i_gateway))
		goto out;
	if (dst_hold_safe(&rt->dst))
		rc = rt6_remove_exception_rt(rt);
out:
	return rc;
}

static int ip6_route_del(struct fib6_config *cfg,
			 struct netlink_ext_ack *extack)
{
	struct rt6_info *rt_cache;
	struct fib6_table *table;
	struct fib6_info *rt;
	struct fib6_node *fn;
	int err = -ESRCH;

	table = fib6_get_table(cfg->fc_nlinfo.nl_net, cfg->fc_table);
	if (!table) {
		NL_SET_ERR_MSG(extack, "FIB table does not exist");
		return err;
	}

	rcu_read_lock();

	fn = fib6_locate(&table->tb6_root,
			 &cfg->fc_dst, cfg->fc_dst_len,
			 &cfg->fc_src, cfg->fc_src_len,
			 !(cfg->fc_flags & RTF_CACHE));

	if (fn) {
		for_each_fib6_node_rt_rcu(fn) {
			if (cfg->fc_flags & RTF_CACHE) {
				int rc;

				rt_cache = rt6_find_cached_rt(rt, &cfg->fc_dst,
							      &cfg->fc_src);
				if (rt_cache) {
					rc = ip6_del_cached_rt(rt_cache, cfg);
					if (rc != -ESRCH) {
						rcu_read_unlock();
						return rc;
					}
				}
				continue;
			}
			if (cfg->fc_ifindex &&
			    (!rt->fib6_nh.nh_dev ||
			     rt->fib6_nh.nh_dev->ifindex != cfg->fc_ifindex))
				continue;
			if (cfg->fc_flags & RTF_GATEWAY &&
			    !ipv6_addr_equal(&cfg->fc_gateway, &rt->fib6_nh.nh_gw))
				continue;
			if (cfg->fc_metric && cfg->fc_metric != rt->fib6_metric)
				continue;
			if (cfg->fc_protocol && cfg->fc_protocol != rt->fib6_protocol)
				continue;
			if (!fib6_info_hold_safe(rt))
				continue;
			rcu_read_unlock();

			/* if gateway was specified only delete the one hop */
			if (cfg->fc_flags & RTF_GATEWAY)
				return __ip6_del_rt(rt, &cfg->fc_nlinfo);

			return __ip6_del_rt_siblings(rt, cfg);
		}
	}
	rcu_read_unlock();

	return err;
}

static void rt6_do_redirect(struct dst_entry *dst, struct sock *sk, struct sk_buff *skb)
{
	struct netevent_redirect netevent;
	struct rt6_info *rt, *nrt = NULL;
	struct ndisc_options ndopts;
	struct inet6_dev *in6_dev;
	struct neighbour *neigh;
	struct fib6_info *from;
	struct rd_msg *msg;
	int optlen, on_link;
	u8 *lladdr;

	optlen = skb_tail_pointer(skb) - skb_transport_header(skb);
	optlen -= sizeof(*msg);

	if (optlen < 0) {
		net_dbg_ratelimited("rt6_do_redirect: packet too short\n");
		return;
	}

	msg = (struct rd_msg *)icmp6_hdr(skb);

	if (ipv6_addr_is_multicast(&msg->dest)) {
		net_dbg_ratelimited("rt6_do_redirect: destination address is multicast\n");
		return;
	}

	on_link = 0;
	if (ipv6_addr_equal(&msg->dest, &msg->target)) {
		on_link = 1;
	} else if (ipv6_addr_type(&msg->target) !=
		   (IPV6_ADDR_UNICAST|IPV6_ADDR_LINKLOCAL)) {
		net_dbg_ratelimited("rt6_do_redirect: target address is not link-local unicast\n");
		return;
	}

	in6_dev = __in6_dev_get(skb->dev);
	if (!in6_dev)
		return;
	if (in6_dev->cnf.forwarding || !in6_dev->cnf.accept_redirects)
		return;

	/* RFC2461 8.1:
	 *	The IP source address of the Redirect MUST be the same as the current
	 *	first-hop router for the specified ICMP Destination Address.
	 */

	if (!ndisc_parse_options(skb->dev, msg->opt, optlen, &ndopts)) {
		net_dbg_ratelimited("rt6_redirect: invalid ND options\n");
		return;
	}

	lladdr = NULL;
	if (ndopts.nd_opts_tgt_lladdr) {
		lladdr = ndisc_opt_addr_data(ndopts.nd_opts_tgt_lladdr,
					     skb->dev);
		if (!lladdr) {
			net_dbg_ratelimited("rt6_redirect: invalid link-layer address length\n");
			return;
		}
	}

	rt = (struct rt6_info *) dst;
	if (rt->rt6i_flags & RTF_REJECT) {
		net_dbg_ratelimited("rt6_redirect: source isn't a valid nexthop for redirect target\n");
		return;
	}

	/* Redirect received -> path was valid.
	 * Look, redirects are sent only in response to data packets,
	 * so that this nexthop apparently is reachable. --ANK
	 */
	dst_confirm_neigh(&rt->dst, &ipv6_hdr(skb)->saddr);

	neigh = __neigh_lookup(&nd_tbl, &msg->target, skb->dev, 1);
	if (!neigh)
		return;

	/*
	 *	We have finally decided to accept it.
	 */

	ndisc_update(skb->dev, neigh, lladdr, NUD_STALE,
		     NEIGH_UPDATE_F_WEAK_OVERRIDE|
		     NEIGH_UPDATE_F_OVERRIDE|
		     (on_link ? 0 : (NEIGH_UPDATE_F_OVERRIDE_ISROUTER|
				     NEIGH_UPDATE_F_ISROUTER)),
		     NDISC_REDIRECT, &ndopts);

	rcu_read_lock();
	from = rcu_dereference(rt->from);
	/* This fib6_info_hold() is safe here because we hold reference to rt
	 * and rt already holds reference to fib6_info.
	 */
	fib6_info_hold(from);
	rcu_read_unlock();

	nrt = ip6_rt_cache_alloc(from, &msg->dest, NULL);
	if (!nrt)
		goto out;

	nrt->rt6i_flags = RTF_GATEWAY|RTF_UP|RTF_DYNAMIC|RTF_CACHE;
	if (on_link)
		nrt->rt6i_flags &= ~RTF_GATEWAY;

	nrt->rt6i_gateway = *(struct in6_addr *)neigh->primary_key;

	/* No need to remove rt from the exception table if rt is
	 * a cached route because rt6_insert_exception() will
	 * takes care of it
	 */
	if (rt6_insert_exception(nrt, from)) {
		dst_release_immediate(&nrt->dst);
		goto out;
	}

	netevent.old = &rt->dst;
	netevent.new = &nrt->dst;
	netevent.daddr = &msg->dest;
	netevent.neigh = neigh;
	call_netevent_notifiers(NETEVENT_REDIRECT, &netevent);

out:
	fib6_info_release(from);
	neigh_release(neigh);
}

#ifdef CONFIG_IPV6_ROUTE_INFO
static struct fib6_info *rt6_get_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr,
					   struct net_device *dev)
{
	u32 tb_id = l3mdev_fib_table(dev) ? : RT6_TABLE_INFO;
	int ifindex = dev->ifindex;
	struct fib6_node *fn;
	struct fib6_info *rt = NULL;
	struct fib6_table *table;

	table = fib6_get_table(net, tb_id);
	if (!table)
		return NULL;

	rcu_read_lock();
	fn = fib6_locate(&table->tb6_root, prefix, prefixlen, NULL, 0, true);
	if (!fn)
		goto out;

	for_each_fib6_node_rt_rcu(fn) {
		if (rt->fib6_nh.nh_dev->ifindex != ifindex)
			continue;
		if ((rt->fib6_flags & (RTF_ROUTEINFO|RTF_GATEWAY)) != (RTF_ROUTEINFO|RTF_GATEWAY))
			continue;
		if (!ipv6_addr_equal(&rt->fib6_nh.nh_gw, gwaddr))
			continue;
		if (!fib6_info_hold_safe(rt))
			continue;
		break;
	}
out:
	rcu_read_unlock();
	return rt;
}

static struct fib6_info *rt6_add_route_info(struct net *net,
					   const struct in6_addr *prefix, int prefixlen,
					   const struct in6_addr *gwaddr,
					   struct net_device *dev,
					   unsigned int pref)
{
	struct fib6_config cfg = {
		.fc_metric	= IP6_RT_PRIO_USER,
		.fc_ifindex	= dev->ifindex,
		.fc_dst_len	= prefixlen,
		.fc_flags	= RTF_GATEWAY | RTF_ADDRCONF | RTF_ROUTEINFO |
				  RTF_UP | RTF_PREF(pref),
		.fc_protocol = RTPROT_RA,
		.fc_type = RTN_UNICAST,
		.fc_nlinfo.portid = 0,
		.fc_nlinfo.nlh = NULL,
		.fc_nlinfo.nl_net = net,
	};

	cfg.fc_table = l3mdev_fib_table(dev) ? : RT6_TABLE_INFO,
	cfg.fc_dst = *prefix;
	cfg.fc_gateway = *gwaddr;

	/* We should treat it as a default route if prefix length is 0. */
	if (!prefixlen)
		cfg.fc_flags |= RTF_DEFAULT;

	ip6_route_add(&cfg, GFP_ATOMIC, NULL);

	return rt6_get_route_info(net, prefix, prefixlen, gwaddr, dev);
}
#endif

struct fib6_info *rt6_get_dflt_router(struct net *net,
				     const struct in6_addr *addr,
				     struct net_device *dev)
{
	u32 tb_id = l3mdev_fib_table(dev) ? : RT6_TABLE_DFLT;
	struct fib6_info *rt;
	struct fib6_table *table;

	table = fib6_get_table(net, tb_id);
	if (!table)
		return NULL;

	rcu_read_lock();
	for_each_fib6_node_rt_rcu(&table->tb6_root) {
		if (dev == rt->fib6_nh.nh_dev &&
		    ((rt->fib6_flags & (RTF_ADDRCONF | RTF_DEFAULT)) == (RTF_ADDRCONF | RTF_DEFAULT)) &&
		    ipv6_addr_equal(&rt->fib6_nh.nh_gw, addr))
			break;
	}
	if (rt && !fib6_info_hold_safe(rt))
		rt = NULL;
	rcu_read_unlock();
	return rt;
}

struct fib6_info *rt6_add_dflt_router(struct net *net,
				     const struct in6_addr *gwaddr,
				     struct net_device *dev,
				     unsigned int pref)
{
	struct fib6_config cfg = {
		.fc_table	= l3mdev_fib_table(dev) ? : RT6_TABLE_DFLT,
		.fc_metric	= IP6_RT_PRIO_USER,
		.fc_ifindex	= dev->ifindex,
		.fc_flags	= RTF_GATEWAY | RTF_ADDRCONF | RTF_DEFAULT |
				  RTF_UP | RTF_EXPIRES | RTF_PREF(pref),
		.fc_protocol = RTPROT_RA,
		.fc_type = RTN_UNICAST,
		.fc_nlinfo.portid = 0,
		.fc_nlinfo.nlh = NULL,
		.fc_nlinfo.nl_net = net,
	};

	cfg.fc_gateway = *gwaddr;

	if (!ip6_route_add(&cfg, GFP_ATOMIC, NULL)) {
		struct fib6_table *table;

		table = fib6_get_table(dev_net(dev), cfg.fc_table);
		if (table)
			table->flags |= RT6_TABLE_HAS_DFLT_ROUTER;
	}

	return rt6_get_dflt_router(net, gwaddr, dev);
}

static void __rt6_purge_dflt_routers(struct net *net,
				     struct fib6_table *table)
{
	struct fib6_info *rt;

restart:
	rcu_read_lock();
	for_each_fib6_node_rt_rcu(&table->tb6_root) {
		struct net_device *dev = fib6_info_nh_dev(rt);
		struct inet6_dev *idev = dev ? __in6_dev_get(dev) : NULL;

		if (rt->fib6_flags & (RTF_DEFAULT | RTF_ADDRCONF) &&
		    (!idev || idev->cnf.accept_ra != 2) &&
		    fib6_info_hold_safe(rt)) {
			rcu_read_unlock();
			ip6_del_rt(net, rt);
			goto restart;
		}
	}
	rcu_read_unlock();

	table->flags &= ~RT6_TABLE_HAS_DFLT_ROUTER;
}

void rt6_purge_dflt_routers(struct net *net)
{
	struct fib6_table *table;
	struct hlist_head *head;
	unsigned int h;

	rcu_read_lock();

	for (h = 0; h < FIB6_TABLE_HASHSZ; h++) {
		head = &net->ipv6.fib_table_hash[h];
		hlist_for_each_entry_rcu(table, head, tb6_hlist) {
			if (table->flags & RT6_TABLE_HAS_DFLT_ROUTER)
				__rt6_purge_dflt_routers(net, table);
		}
	}

	rcu_read_unlock();
}

static void rtmsg_to_fib6_config(struct net *net,
				 struct in6_rtmsg *rtmsg,
				 struct fib6_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	cfg->fc_table = l3mdev_fib_table_by_index(net, rtmsg->rtmsg_ifindex) ?
			 : RT6_TABLE_MAIN;
	cfg->fc_ifindex = rtmsg->rtmsg_ifindex;
	cfg->fc_metric = rtmsg->rtmsg_metric;
	cfg->fc_expires = rtmsg->rtmsg_info;
	cfg->fc_dst_len = rtmsg->rtmsg_dst_len;
	cfg->fc_src_len = rtmsg->rtmsg_src_len;
	cfg->fc_flags = rtmsg->rtmsg_flags;
	cfg->fc_type = rtmsg->rtmsg_type;

	cfg->fc_nlinfo.nl_net = net;

	cfg->fc_dst = rtmsg->rtmsg_dst;
	cfg->fc_src = rtmsg->rtmsg_src;
	cfg->fc_gateway = rtmsg->rtmsg_gateway;
}

int ipv6_route_ioctl(struct net *net, unsigned int cmd, void __user *arg)
{
	struct fib6_config cfg;
	struct in6_rtmsg rtmsg;
	int err;

	switch (cmd) {
	case SIOCADDRT:		/* Add a route */
	case SIOCDELRT:		/* Delete a route */
		if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
			return -EPERM;
		err = copy_from_user(&rtmsg, arg,
				     sizeof(struct in6_rtmsg));
		if (err)
			return -EFAULT;

		rtmsg_to_fib6_config(net, &rtmsg, &cfg);

		rtnl_lock();
		switch (cmd) {
		case SIOCADDRT:
			err = ip6_route_add(&cfg, GFP_KERNEL, NULL);
			break;
		case SIOCDELRT:
			err = ip6_route_del(&cfg, NULL);
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
			IP6_INC_STATS(dev_net(dst->dev),
				      __in6_dev_get_safely(skb->dev),
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

static int ip6_pkt_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb->dev = skb_dst(skb)->dev;
	return ip6_pkt_drop(skb, ICMPV6_NOROUTE, IPSTATS_MIB_OUTNOROUTES);
}

static int ip6_pkt_prohibit(struct sk_buff *skb)
{
	return ip6_pkt_drop(skb, ICMPV6_ADM_PROHIBITED, IPSTATS_MIB_INNOROUTES);
}

static int ip6_pkt_prohibit_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb->dev = skb_dst(skb)->dev;
	return ip6_pkt_drop(skb, ICMPV6_ADM_PROHIBITED, IPSTATS_MIB_OUTNOROUTES);
}

/*
 *	Allocate a dst for local (unicast / anycast) address.
 */

struct fib6_info *addrconf_f6i_alloc(struct net *net,
				     struct inet6_dev *idev,
				     const struct in6_addr *addr,
				     bool anycast, gfp_t gfp_flags)
{
	u32 tb_id;
	struct net_device *dev = idev->dev;
	struct fib6_info *f6i;

	f6i = fib6_info_alloc(gfp_flags);
	if (!f6i)
		return ERR_PTR(-ENOMEM);

	f6i->dst_nocount = true;
	f6i->dst_host = true;
	f6i->fib6_protocol = RTPROT_KERNEL;
	f6i->fib6_flags = RTF_UP | RTF_NONEXTHOP;
	if (anycast) {
		f6i->fib6_type = RTN_ANYCAST;
		f6i->fib6_flags |= RTF_ANYCAST;
	} else {
		f6i->fib6_type = RTN_LOCAL;
		f6i->fib6_flags |= RTF_LOCAL;
	}

	f6i->fib6_nh.nh_gw = *addr;
	dev_hold(dev);
	f6i->fib6_nh.nh_dev = dev;
	f6i->fib6_dst.addr = *addr;
	f6i->fib6_dst.plen = 128;
	tb_id = l3mdev_fib_table(idev->dev) ? : RT6_TABLE_LOCAL;
	f6i->fib6_table = fib6_get_table(net, tb_id);

	return f6i;
}

/* remove deleted ip from prefsrc entries */
struct arg_dev_net_ip {
	struct net_device *dev;
	struct net *net;
	struct in6_addr *addr;
};

static int fib6_remove_prefsrc(struct fib6_info *rt, void *arg)
{
	struct net_device *dev = ((struct arg_dev_net_ip *)arg)->dev;
	struct net *net = ((struct arg_dev_net_ip *)arg)->net;
	struct in6_addr *addr = ((struct arg_dev_net_ip *)arg)->addr;

	if (((void *)rt->fib6_nh.nh_dev == dev || !dev) &&
	    rt != net->ipv6.fib6_null_entry &&
	    ipv6_addr_equal(addr, &rt->fib6_prefsrc.addr)) {
		spin_lock_bh(&rt6_exception_lock);
		/* remove prefsrc entry */
		rt->fib6_prefsrc.plen = 0;
		/* need to update cache as well */
		rt6_exceptions_remove_prefsrc(rt);
		spin_unlock_bh(&rt6_exception_lock);
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
	fib6_clean_all(net, fib6_remove_prefsrc, &adni);
}

#define RTF_RA_ROUTER		(RTF_ADDRCONF | RTF_DEFAULT | RTF_GATEWAY)

/* Remove routers and update dst entries when gateway turn into host. */
static int fib6_clean_tohost(struct fib6_info *rt, void *arg)
{
	struct in6_addr *gateway = (struct in6_addr *)arg;

	if (((rt->fib6_flags & RTF_RA_ROUTER) == RTF_RA_ROUTER) &&
	    ipv6_addr_equal(gateway, &rt->fib6_nh.nh_gw)) {
		return -1;
	}

	/* Further clean up cached routes in exception table.
	 * This is needed because cached route may have a different
	 * gateway than its 'parent' in the case of an ip redirect.
	 */
	rt6_exceptions_clean_tohost(rt, gateway);

	return 0;
}

void rt6_clean_tohost(struct net *net, struct in6_addr *gateway)
{
	fib6_clean_all(net, fib6_clean_tohost, gateway);
}

struct arg_netdev_event {
	const struct net_device *dev;
	union {
		unsigned int nh_flags;
		unsigned long event;
	};
};

static struct fib6_info *rt6_multipath_first_sibling(const struct fib6_info *rt)
{
	struct fib6_info *iter;
	struct fib6_node *fn;

	fn = rcu_dereference_protected(rt->fib6_node,
			lockdep_is_held(&rt->fib6_table->tb6_lock));
	iter = rcu_dereference_protected(fn->leaf,
			lockdep_is_held(&rt->fib6_table->tb6_lock));
	while (iter) {
		if (iter->fib6_metric == rt->fib6_metric &&
		    rt6_qualify_for_ecmp(iter))
			return iter;
		iter = rcu_dereference_protected(iter->fib6_next,
				lockdep_is_held(&rt->fib6_table->tb6_lock));
	}

	return NULL;
}

static bool rt6_is_dead(const struct fib6_info *rt)
{
	if (rt->fib6_nh.nh_flags & RTNH_F_DEAD ||
	    (rt->fib6_nh.nh_flags & RTNH_F_LINKDOWN &&
	     fib6_ignore_linkdown(rt)))
		return true;

	return false;
}

static int rt6_multipath_total_weight(const struct fib6_info *rt)
{
	struct fib6_info *iter;
	int total = 0;

	if (!rt6_is_dead(rt))
		total += rt->fib6_nh.nh_weight;

	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (!rt6_is_dead(iter))
			total += iter->fib6_nh.nh_weight;
	}

	return total;
}

static void rt6_upper_bound_set(struct fib6_info *rt, int *weight, int total)
{
	int upper_bound = -1;

	if (!rt6_is_dead(rt)) {
		*weight += rt->fib6_nh.nh_weight;
		upper_bound = DIV_ROUND_CLOSEST_ULL((u64) (*weight) << 31,
						    total) - 1;
	}
	atomic_set(&rt->fib6_nh.nh_upper_bound, upper_bound);
}

static void rt6_multipath_upper_bound_set(struct fib6_info *rt, int total)
{
	struct fib6_info *iter;
	int weight = 0;

	rt6_upper_bound_set(rt, &weight, total);

	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		rt6_upper_bound_set(iter, &weight, total);
}

void rt6_multipath_rebalance(struct fib6_info *rt)
{
	struct fib6_info *first;
	int total;

	/* In case the entire multipath route was marked for flushing,
	 * then there is no need to rebalance upon the removal of every
	 * sibling route.
	 */
	if (!rt->fib6_nsiblings || rt->should_flush)
		return;

	/* During lookup routes are evaluated in order, so we need to
	 * make sure upper bounds are assigned from the first sibling
	 * onwards.
	 */
	first = rt6_multipath_first_sibling(rt);
	if (WARN_ON_ONCE(!first))
		return;

	total = rt6_multipath_total_weight(first);
	rt6_multipath_upper_bound_set(first, total);
}

static int fib6_ifup(struct fib6_info *rt, void *p_arg)
{
	const struct arg_netdev_event *arg = p_arg;
	struct net *net = dev_net(arg->dev);

	if (rt != net->ipv6.fib6_null_entry && rt->fib6_nh.nh_dev == arg->dev) {
		rt->fib6_nh.nh_flags &= ~arg->nh_flags;
		fib6_update_sernum_upto_root(net, rt);
		rt6_multipath_rebalance(rt);
	}

	return 0;
}

void rt6_sync_up(struct net_device *dev, unsigned int nh_flags)
{
	struct arg_netdev_event arg = {
		.dev = dev,
		{
			.nh_flags = nh_flags,
		},
	};

	if (nh_flags & RTNH_F_DEAD && netif_carrier_ok(dev))
		arg.nh_flags |= RTNH_F_LINKDOWN;

	fib6_clean_all(dev_net(dev), fib6_ifup, &arg);
}

static bool rt6_multipath_uses_dev(const struct fib6_info *rt,
				   const struct net_device *dev)
{
	struct fib6_info *iter;

	if (rt->fib6_nh.nh_dev == dev)
		return true;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		if (iter->fib6_nh.nh_dev == dev)
			return true;

	return false;
}

static void rt6_multipath_flush(struct fib6_info *rt)
{
	struct fib6_info *iter;

	rt->should_flush = 1;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		iter->should_flush = 1;
}

static unsigned int rt6_multipath_dead_count(const struct fib6_info *rt,
					     const struct net_device *down_dev)
{
	struct fib6_info *iter;
	unsigned int dead = 0;

	if (rt->fib6_nh.nh_dev == down_dev ||
	    rt->fib6_nh.nh_flags & RTNH_F_DEAD)
		dead++;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		if (iter->fib6_nh.nh_dev == down_dev ||
		    iter->fib6_nh.nh_flags & RTNH_F_DEAD)
			dead++;

	return dead;
}

static void rt6_multipath_nh_flags_set(struct fib6_info *rt,
				       const struct net_device *dev,
				       unsigned int nh_flags)
{
	struct fib6_info *iter;

	if (rt->fib6_nh.nh_dev == dev)
		rt->fib6_nh.nh_flags |= nh_flags;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		if (iter->fib6_nh.nh_dev == dev)
			iter->fib6_nh.nh_flags |= nh_flags;
}

/* called with write lock held for table with rt */
static int fib6_ifdown(struct fib6_info *rt, void *p_arg)
{
	const struct arg_netdev_event *arg = p_arg;
	const struct net_device *dev = arg->dev;
	struct net *net = dev_net(dev);

	if (rt == net->ipv6.fib6_null_entry)
		return 0;

	switch (arg->event) {
	case NETDEV_UNREGISTER:
		return rt->fib6_nh.nh_dev == dev ? -1 : 0;
	case NETDEV_DOWN:
		if (rt->should_flush)
			return -1;
		if (!rt->fib6_nsiblings)
			return rt->fib6_nh.nh_dev == dev ? -1 : 0;
		if (rt6_multipath_uses_dev(rt, dev)) {
			unsigned int count;

			count = rt6_multipath_dead_count(rt, dev);
			if (rt->fib6_nsiblings + 1 == count) {
				rt6_multipath_flush(rt);
				return -1;
			}
			rt6_multipath_nh_flags_set(rt, dev, RTNH_F_DEAD |
						   RTNH_F_LINKDOWN);
			fib6_update_sernum(net, rt);
			rt6_multipath_rebalance(rt);
		}
		return -2;
	case NETDEV_CHANGE:
		if (rt->fib6_nh.nh_dev != dev ||
		    rt->fib6_flags & (RTF_LOCAL | RTF_ANYCAST))
			break;
		rt->fib6_nh.nh_flags |= RTNH_F_LINKDOWN;
		rt6_multipath_rebalance(rt);
		break;
	}

	return 0;
}

void rt6_sync_down_dev(struct net_device *dev, unsigned long event)
{
	struct arg_netdev_event arg = {
		.dev = dev,
		{
			.event = event,
		},
	};

	fib6_clean_all(dev_net(dev), fib6_ifdown, &arg);
}

void rt6_disable_ip(struct net_device *dev, unsigned long event)
{
	rt6_sync_down_dev(dev, event);
	rt6_uncached_list_flush_dev(dev_net(dev), dev);
	neigh_ifdown(&nd_tbl, dev);
}

struct rt6_mtu_change_arg {
	struct net_device *dev;
	unsigned int mtu;
};

static int rt6_mtu_change_route(struct fib6_info *rt, void *p_arg)
{
	struct rt6_mtu_change_arg *arg = (struct rt6_mtu_change_arg *) p_arg;
	struct inet6_dev *idev;

	/* In IPv6 pmtu discovery is not optional,
	   so that RTAX_MTU lock cannot disable it.
	   We still use this lock to block changes
	   caused by addrconf/ndisc.
	*/

	idev = __in6_dev_get(arg->dev);
	if (!idev)
		return 0;

	/* For administrative MTU increase, there is no way to discover
	   IPv6 PMTU increase, so PMTU increase should be updated here.
	   Since RFC 1981 doesn't include administrative MTU increase
	   update PMTU increase is a MUST. (i.e. jumbo frame)
	 */
	if (rt->fib6_nh.nh_dev == arg->dev &&
	    !fib6_metric_locked(rt, RTAX_MTU)) {
		u32 mtu = rt->fib6_pmtu;

		if (mtu >= arg->mtu ||
		    (mtu < arg->mtu && mtu == idev->cnf.mtu6))
			fib6_metric_set(rt, RTAX_MTU, arg->mtu);

		spin_lock_bh(&rt6_exception_lock);
		rt6_exceptions_update_pmtu(idev, rt, arg->mtu);
		spin_unlock_bh(&rt6_exception_lock);
	}
	return 0;
}

void rt6_mtu_change(struct net_device *dev, unsigned int mtu)
{
	struct rt6_mtu_change_arg arg = {
		.dev = dev,
		.mtu = mtu,
	};

	fib6_clean_all(dev_net(dev), rt6_mtu_change_route, &arg);
}

static const struct nla_policy rtm_ipv6_policy[RTA_MAX+1] = {
	[RTA_GATEWAY]           = { .len = sizeof(struct in6_addr) },
	[RTA_PREFSRC]		= { .len = sizeof(struct in6_addr) },
	[RTA_OIF]               = { .type = NLA_U32 },
	[RTA_IIF]		= { .type = NLA_U32 },
	[RTA_PRIORITY]          = { .type = NLA_U32 },
	[RTA_METRICS]           = { .type = NLA_NESTED },
	[RTA_MULTIPATH]		= { .len = sizeof(struct rtnexthop) },
	[RTA_PREF]              = { .type = NLA_U8 },
	[RTA_ENCAP_TYPE]	= { .type = NLA_U16 },
	[RTA_ENCAP]		= { .type = NLA_NESTED },
	[RTA_EXPIRES]		= { .type = NLA_U32 },
	[RTA_UID]		= { .type = NLA_U32 },
	[RTA_MARK]		= { .type = NLA_U32 },
	[RTA_TABLE]		= { .type = NLA_U32 },
	[RTA_IP_PROTO]		= { .type = NLA_U8 },
	[RTA_SPORT]		= { .type = NLA_U16 },
	[RTA_DPORT]		= { .type = NLA_U16 },
};

static int rtm_to_fib6_config(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct fib6_config *cfg,
			      struct netlink_ext_ack *extack)
{
	struct rtmsg *rtm;
	struct nlattr *tb[RTA_MAX+1];
	unsigned int pref;
	int err;

	err = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, rtm_ipv6_policy,
			  NULL);
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
	cfg->fc_type = rtm->rtm_type;

	if (rtm->rtm_type == RTN_UNREACHABLE ||
	    rtm->rtm_type == RTN_BLACKHOLE ||
	    rtm->rtm_type == RTN_PROHIBIT ||
	    rtm->rtm_type == RTN_THROW)
		cfg->fc_flags |= RTF_REJECT;

	if (rtm->rtm_type == RTN_LOCAL)
		cfg->fc_flags |= RTF_LOCAL;

	if (rtm->rtm_flags & RTM_F_CLONED)
		cfg->fc_flags |= RTF_CACHE;

	cfg->fc_flags |= (rtm->rtm_flags & RTNH_F_ONLINK);

	cfg->fc_nlinfo.portid = NETLINK_CB(skb).portid;
	cfg->fc_nlinfo.nlh = nlh;
	cfg->fc_nlinfo.nl_net = sock_net(skb->sk);

	if (tb[RTA_GATEWAY]) {
		cfg->fc_gateway = nla_get_in6_addr(tb[RTA_GATEWAY]);
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
		cfg->fc_prefsrc = nla_get_in6_addr(tb[RTA_PREFSRC]);

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

	if (tb[RTA_MULTIPATH]) {
		cfg->fc_mp = nla_data(tb[RTA_MULTIPATH]);
		cfg->fc_mp_len = nla_len(tb[RTA_MULTIPATH]);

		err = lwtunnel_valid_encap_type_attr(cfg->fc_mp,
						     cfg->fc_mp_len, extack);
		if (err < 0)
			goto errout;
	}

	if (tb[RTA_PREF]) {
		pref = nla_get_u8(tb[RTA_PREF]);
		if (pref != ICMPV6_ROUTER_PREF_LOW &&
		    pref != ICMPV6_ROUTER_PREF_HIGH)
			pref = ICMPV6_ROUTER_PREF_MEDIUM;
		cfg->fc_flags |= RTF_PREF(pref);
	}

	if (tb[RTA_ENCAP])
		cfg->fc_encap = tb[RTA_ENCAP];

	if (tb[RTA_ENCAP_TYPE]) {
		cfg->fc_encap_type = nla_get_u16(tb[RTA_ENCAP_TYPE]);

		err = lwtunnel_valid_encap_type(cfg->fc_encap_type, extack);
		if (err < 0)
			goto errout;
	}

	if (tb[RTA_EXPIRES]) {
		unsigned long timeout = addrconf_timeout_fixup(nla_get_u32(tb[RTA_EXPIRES]), HZ);

		if (addrconf_finite_timeout(timeout)) {
			cfg->fc_expires = jiffies_to_clock_t(timeout * HZ);
			cfg->fc_flags |= RTF_EXPIRES;
		}
	}

	err = 0;
errout:
	return err;
}

struct rt6_nh {
	struct fib6_info *fib6_info;
	struct fib6_config r_cfg;
	struct list_head next;
};

static void ip6_print_replace_route_err(struct list_head *rt6_nh_list)
{
	struct rt6_nh *nh;

	list_for_each_entry(nh, rt6_nh_list, next) {
		pr_warn("IPV6: multipath route replace failed (check consistency of installed routes): %pI6c nexthop %pI6c ifi %d\n",
		        &nh->r_cfg.fc_dst, &nh->r_cfg.fc_gateway,
		        nh->r_cfg.fc_ifindex);
	}
}

static int ip6_route_info_append(struct net *net,
				 struct list_head *rt6_nh_list,
				 struct fib6_info *rt,
				 struct fib6_config *r_cfg)
{
	struct rt6_nh *nh;
	int err = -EEXIST;

	list_for_each_entry(nh, rt6_nh_list, next) {
		/* check if fib6_info already exists */
		if (rt6_duplicate_nexthop(nh->fib6_info, rt))
			return err;
	}

	nh = kzalloc(sizeof(*nh), GFP_KERNEL);
	if (!nh)
		return -ENOMEM;
	nh->fib6_info = rt;
	err = ip6_convert_metrics(net, rt, r_cfg);
	if (err) {
		kfree(nh);
		return err;
	}
	memcpy(&nh->r_cfg, r_cfg, sizeof(*r_cfg));
	list_add_tail(&nh->next, rt6_nh_list);

	return 0;
}

static void ip6_route_mpath_notify(struct fib6_info *rt,
				   struct fib6_info *rt_last,
				   struct nl_info *info,
				   __u16 nlflags)
{
	/* if this is an APPEND route, then rt points to the first route
	 * inserted and rt_last points to last route inserted. Userspace
	 * wants a consistent dump of the route which starts at the first
	 * nexthop. Since sibling routes are always added at the end of
	 * the list, find the first sibling of the last route appended
	 */
	if ((nlflags & NLM_F_APPEND) && rt_last && rt_last->fib6_nsiblings) {
		rt = list_first_entry(&rt_last->fib6_siblings,
				      struct fib6_info,
				      fib6_siblings);
	}

	if (rt)
		inet6_rt_notify(RTM_NEWROUTE, rt, info, nlflags);
}

static int ip6_route_multipath_add(struct fib6_config *cfg,
				   struct netlink_ext_ack *extack)
{
	struct fib6_info *rt_notif = NULL, *rt_last = NULL;
	struct nl_info *info = &cfg->fc_nlinfo;
	struct fib6_config r_cfg;
	struct rtnexthop *rtnh;
	struct fib6_info *rt;
	struct rt6_nh *err_nh;
	struct rt6_nh *nh, *nh_safe;
	__u16 nlflags;
	int remaining;
	int attrlen;
	int err = 1;
	int nhn = 0;
	int replace = (cfg->fc_nlinfo.nlh &&
		       (cfg->fc_nlinfo.nlh->nlmsg_flags & NLM_F_REPLACE));
	LIST_HEAD(rt6_nh_list);

	nlflags = replace ? NLM_F_REPLACE : NLM_F_CREATE;
	if (info->nlh && info->nlh->nlmsg_flags & NLM_F_APPEND)
		nlflags |= NLM_F_APPEND;

	remaining = cfg->fc_mp_len;
	rtnh = (struct rtnexthop *)cfg->fc_mp;

	/* Parse a Multipath Entry and build a list (rt6_nh_list) of
	 * fib6_info structs per nexthop
	 */
	while (rtnh_ok(rtnh, remaining)) {
		memcpy(&r_cfg, cfg, sizeof(*cfg));
		if (rtnh->rtnh_ifindex)
			r_cfg.fc_ifindex = rtnh->rtnh_ifindex;

		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			struct nlattr *nla, *attrs = rtnh_attrs(rtnh);

			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			if (nla) {
				r_cfg.fc_gateway = nla_get_in6_addr(nla);
				r_cfg.fc_flags |= RTF_GATEWAY;
			}
			r_cfg.fc_encap = nla_find(attrs, attrlen, RTA_ENCAP);
			nla = nla_find(attrs, attrlen, RTA_ENCAP_TYPE);
			if (nla)
				r_cfg.fc_encap_type = nla_get_u16(nla);
		}

		r_cfg.fc_flags |= (rtnh->rtnh_flags & RTNH_F_ONLINK);
		rt = ip6_route_info_create(&r_cfg, GFP_KERNEL, extack);
		if (IS_ERR(rt)) {
			err = PTR_ERR(rt);
			rt = NULL;
			goto cleanup;
		}
		if (!rt6_qualify_for_ecmp(rt)) {
			err = -EINVAL;
			NL_SET_ERR_MSG(extack,
				       "Device only routes can not be added for IPv6 using the multipath API.");
			fib6_info_release(rt);
			goto cleanup;
		}

		rt->fib6_nh.nh_weight = rtnh->rtnh_hops + 1;

		err = ip6_route_info_append(info->nl_net, &rt6_nh_list,
					    rt, &r_cfg);
		if (err) {
			fib6_info_release(rt);
			goto cleanup;
		}

		rtnh = rtnh_next(rtnh, &remaining);
	}

	/* for add and replace send one notification with all nexthops.
	 * Skip the notification in fib6_add_rt2node and send one with
	 * the full route when done
	 */
	info->skip_notify = 1;

	err_nh = NULL;
	list_for_each_entry(nh, &rt6_nh_list, next) {
		err = __ip6_ins_rt(nh->fib6_info, info, extack);
		fib6_info_release(nh->fib6_info);

		if (!err) {
			/* save reference to last route successfully inserted */
			rt_last = nh->fib6_info;

			/* save reference to first route for notification */
			if (!rt_notif)
				rt_notif = nh->fib6_info;
		}

		/* nh->fib6_info is used or freed at this point, reset to NULL*/
		nh->fib6_info = NULL;
		if (err) {
			if (replace && nhn)
				ip6_print_replace_route_err(&rt6_nh_list);
			err_nh = nh;
			goto add_errout;
		}

		/* Because each route is added like a single route we remove
		 * these flags after the first nexthop: if there is a collision,
		 * we have already failed to add the first nexthop:
		 * fib6_add_rt2node() has rejected it; when replacing, old
		 * nexthops have been replaced by first new, the rest should
		 * be added to it.
		 */
		cfg->fc_nlinfo.nlh->nlmsg_flags &= ~(NLM_F_EXCL |
						     NLM_F_REPLACE);
		nhn++;
	}

	/* success ... tell user about new route */
	ip6_route_mpath_notify(rt_notif, rt_last, info, nlflags);
	goto cleanup;

add_errout:
	/* send notification for routes that were added so that
	 * the delete notifications sent by ip6_route_del are
	 * coherent
	 */
	if (rt_notif)
		ip6_route_mpath_notify(rt_notif, rt_last, info, nlflags);

	/* Delete routes that were already added */
	list_for_each_entry(nh, &rt6_nh_list, next) {
		if (err_nh == nh)
			break;
		ip6_route_del(&nh->r_cfg, extack);
	}

cleanup:
	list_for_each_entry_safe(nh, nh_safe, &rt6_nh_list, next) {
		if (nh->fib6_info)
			fib6_info_release(nh->fib6_info);
		list_del(&nh->next);
		kfree(nh);
	}

	return err;
}

static int ip6_route_multipath_del(struct fib6_config *cfg,
				   struct netlink_ext_ack *extack)
{
	struct fib6_config r_cfg;
	struct rtnexthop *rtnh;
	int remaining;
	int attrlen;
	int err = 1, last_err = 0;

	remaining = cfg->fc_mp_len;
	rtnh = (struct rtnexthop *)cfg->fc_mp;

	/* Parse a Multipath Entry */
	while (rtnh_ok(rtnh, remaining)) {
		memcpy(&r_cfg, cfg, sizeof(*cfg));
		if (rtnh->rtnh_ifindex)
			r_cfg.fc_ifindex = rtnh->rtnh_ifindex;

		attrlen = rtnh_attrlen(rtnh);
		if (attrlen > 0) {
			struct nlattr *nla, *attrs = rtnh_attrs(rtnh);

			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			if (nla) {
				nla_memcpy(&r_cfg.fc_gateway, nla, 16);
				r_cfg.fc_flags |= RTF_GATEWAY;
			}
		}
		err = ip6_route_del(&r_cfg, extack);
		if (err)
			last_err = err;

		rtnh = rtnh_next(rtnh, &remaining);
	}

	return last_err;
}

static int inet6_rtm_delroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack)
{
	struct fib6_config cfg;
	int err;

	err = rtm_to_fib6_config(skb, nlh, &cfg, extack);
	if (err < 0)
		return err;

	if (cfg.fc_mp)
		return ip6_route_multipath_del(&cfg, extack);
	else {
		cfg.fc_delete_all_nh = 1;
		return ip6_route_del(&cfg, extack);
	}
}

static int inet6_rtm_newroute(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack)
{
	struct fib6_config cfg;
	int err;

	err = rtm_to_fib6_config(skb, nlh, &cfg, extack);
	if (err < 0)
		return err;

	if (cfg.fc_mp)
		return ip6_route_multipath_add(&cfg, extack);
	else
		return ip6_route_add(&cfg, GFP_KERNEL, extack);
}

static size_t rt6_nlmsg_size(struct fib6_info *rt)
{
	int nexthop_len = 0;

	if (rt->fib6_nsiblings) {
		nexthop_len = nla_total_size(0)	 /* RTA_MULTIPATH */
			    + NLA_ALIGN(sizeof(struct rtnexthop))
			    + nla_total_size(16) /* RTA_GATEWAY */
			    + lwtunnel_get_encap_size(rt->fib6_nh.nh_lwtstate);

		nexthop_len *= rt->fib6_nsiblings;
	}

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
	       + nla_total_size(sizeof(struct rta_cacheinfo))
	       + nla_total_size(TCP_CA_NAME_MAX) /* RTAX_CC_ALGO */
	       + nla_total_size(1) /* RTA_PREF */
	       + lwtunnel_get_encap_size(rt->fib6_nh.nh_lwtstate)
	       + nexthop_len;
}

static int rt6_nexthop_info(struct sk_buff *skb, struct fib6_info *rt,
			    unsigned int *flags, bool skip_oif)
{
	if (rt->fib6_nh.nh_flags & RTNH_F_DEAD)
		*flags |= RTNH_F_DEAD;

	if (rt->fib6_nh.nh_flags & RTNH_F_LINKDOWN) {
		*flags |= RTNH_F_LINKDOWN;

		rcu_read_lock();
		if (fib6_ignore_linkdown(rt))
			*flags |= RTNH_F_DEAD;
		rcu_read_unlock();
	}

	if (rt->fib6_flags & RTF_GATEWAY) {
		if (nla_put_in6_addr(skb, RTA_GATEWAY, &rt->fib6_nh.nh_gw) < 0)
			goto nla_put_failure;
	}

	*flags |= (rt->fib6_nh.nh_flags & RTNH_F_ONLINK);
	if (rt->fib6_nh.nh_flags & RTNH_F_OFFLOAD)
		*flags |= RTNH_F_OFFLOAD;

	/* not needed for multipath encoding b/c it has a rtnexthop struct */
	if (!skip_oif && rt->fib6_nh.nh_dev &&
	    nla_put_u32(skb, RTA_OIF, rt->fib6_nh.nh_dev->ifindex))
		goto nla_put_failure;

	if (rt->fib6_nh.nh_lwtstate &&
	    lwtunnel_fill_encap(skb, rt->fib6_nh.nh_lwtstate) < 0)
		goto nla_put_failure;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

/* add multipath next hop */
static int rt6_add_nexthop(struct sk_buff *skb, struct fib6_info *rt)
{
	const struct net_device *dev = rt->fib6_nh.nh_dev;
	struct rtnexthop *rtnh;
	unsigned int flags = 0;

	rtnh = nla_reserve_nohdr(skb, sizeof(*rtnh));
	if (!rtnh)
		goto nla_put_failure;

	rtnh->rtnh_hops = rt->fib6_nh.nh_weight - 1;
	rtnh->rtnh_ifindex = dev ? dev->ifindex : 0;

	if (rt6_nexthop_info(skb, rt, &flags, true) < 0)
		goto nla_put_failure;

	rtnh->rtnh_flags = flags;

	/* length of rtnetlink header + attributes */
	rtnh->rtnh_len = nlmsg_get_pos(skb) - (void *)rtnh;

	return 0;

nla_put_failure:
	return -EMSGSIZE;
}

static int rt6_fill_node(struct net *net, struct sk_buff *skb,
			 struct fib6_info *rt, struct dst_entry *dst,
			 struct in6_addr *dest, struct in6_addr *src,
			 int iif, int type, u32 portid, u32 seq,
			 unsigned int flags)
{
	struct rt6_info *rt6 = (struct rt6_info *)dst;
	struct rt6key *rt6_dst, *rt6_src;
	u32 *pmetrics, table, rt6_flags;
	struct nlmsghdr *nlh;
	struct rtmsg *rtm;
	long expires = 0;

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(*rtm), flags);
	if (!nlh)
		return -EMSGSIZE;

	if (rt6) {
		rt6_dst = &rt6->rt6i_dst;
		rt6_src = &rt6->rt6i_src;
		rt6_flags = rt6->rt6i_flags;
	} else {
		rt6_dst = &rt->fib6_dst;
		rt6_src = &rt->fib6_src;
		rt6_flags = rt->fib6_flags;
	}

	rtm = nlmsg_data(nlh);
	rtm->rtm_family = AF_INET6;
	rtm->rtm_dst_len = rt6_dst->plen;
	rtm->rtm_src_len = rt6_src->plen;
	rtm->rtm_tos = 0;
	if (rt->fib6_table)
		table = rt->fib6_table->tb6_id;
	else
		table = RT6_TABLE_UNSPEC;
	rtm->rtm_table = table;
	if (nla_put_u32(skb, RTA_TABLE, table))
		goto nla_put_failure;

	rtm->rtm_type = rt->fib6_type;
	rtm->rtm_flags = 0;
	rtm->rtm_scope = RT_SCOPE_UNIVERSE;
	rtm->rtm_protocol = rt->fib6_protocol;

	if (rt6_flags & RTF_CACHE)
		rtm->rtm_flags |= RTM_F_CLONED;

	if (dest) {
		if (nla_put_in6_addr(skb, RTA_DST, dest))
			goto nla_put_failure;
		rtm->rtm_dst_len = 128;
	} else if (rtm->rtm_dst_len)
		if (nla_put_in6_addr(skb, RTA_DST, &rt6_dst->addr))
			goto nla_put_failure;
#ifdef CONFIG_IPV6_SUBTREES
	if (src) {
		if (nla_put_in6_addr(skb, RTA_SRC, src))
			goto nla_put_failure;
		rtm->rtm_src_len = 128;
	} else if (rtm->rtm_src_len &&
		   nla_put_in6_addr(skb, RTA_SRC, &rt6_src->addr))
		goto nla_put_failure;
#endif
	if (iif) {
#ifdef CONFIG_IPV6_MROUTE
		if (ipv6_addr_is_multicast(&rt6_dst->addr)) {
			int err = ip6mr_get_route(net, skb, rtm, portid);

			if (err == 0)
				return 0;
			if (err < 0)
				goto nla_put_failure;
		} else
#endif
			if (nla_put_u32(skb, RTA_IIF, iif))
				goto nla_put_failure;
	} else if (dest) {
		struct in6_addr saddr_buf;
		if (ip6_route_get_saddr(net, rt, dest, 0, &saddr_buf) == 0 &&
		    nla_put_in6_addr(skb, RTA_PREFSRC, &saddr_buf))
			goto nla_put_failure;
	}

	if (rt->fib6_prefsrc.plen) {
		struct in6_addr saddr_buf;
		saddr_buf = rt->fib6_prefsrc.addr;
		if (nla_put_in6_addr(skb, RTA_PREFSRC, &saddr_buf))
			goto nla_put_failure;
	}

	pmetrics = dst ? dst_metrics_ptr(dst) : rt->fib6_metrics->metrics;
	if (rtnetlink_put_metrics(skb, pmetrics) < 0)
		goto nla_put_failure;

	if (nla_put_u32(skb, RTA_PRIORITY, rt->fib6_metric))
		goto nla_put_failure;

	/* For multipath routes, walk the siblings list and add
	 * each as a nexthop within RTA_MULTIPATH.
	 */
	if (rt6) {
		if (rt6_flags & RTF_GATEWAY &&
		    nla_put_in6_addr(skb, RTA_GATEWAY, &rt6->rt6i_gateway))
			goto nla_put_failure;

		if (dst->dev && nla_put_u32(skb, RTA_OIF, dst->dev->ifindex))
			goto nla_put_failure;
	} else if (rt->fib6_nsiblings) {
		struct fib6_info *sibling, *next_sibling;
		struct nlattr *mp;

		mp = nla_nest_start(skb, RTA_MULTIPATH);
		if (!mp)
			goto nla_put_failure;

		if (rt6_add_nexthop(skb, rt) < 0)
			goto nla_put_failure;

		list_for_each_entry_safe(sibling, next_sibling,
					 &rt->fib6_siblings, fib6_siblings) {
			if (rt6_add_nexthop(skb, sibling) < 0)
				goto nla_put_failure;
		}

		nla_nest_end(skb, mp);
	} else {
		if (rt6_nexthop_info(skb, rt, &rtm->rtm_flags, false) < 0)
			goto nla_put_failure;
	}

	if (rt6_flags & RTF_EXPIRES) {
		expires = dst ? dst->expires : rt->expires;
		expires -= jiffies;
	}

	if (rtnl_put_cacheinfo(skb, dst, 0, expires, dst ? dst->error : 0) < 0)
		goto nla_put_failure;

	if (nla_put_u8(skb, RTA_PREF, IPV6_EXTRACT_PREF(rt6_flags)))
		goto nla_put_failure;


	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

int rt6_dump_route(struct fib6_info *rt, void *p_arg)
{
	struct rt6_rtnl_dump_arg *arg = (struct rt6_rtnl_dump_arg *) p_arg;
	struct net *net = arg->net;

	if (rt == net->ipv6.fib6_null_entry)
		return 0;

	if (nlmsg_len(arg->cb->nlh) >= sizeof(struct rtmsg)) {
		struct rtmsg *rtm = nlmsg_data(arg->cb->nlh);

		/* user wants prefix routes only */
		if (rtm->rtm_flags & RTM_F_PREFIX &&
		    !(rt->fib6_flags & RTF_PREFIX_RT)) {
			/* success since this is not a prefix route */
			return 1;
		}
	}

	return rt6_fill_node(net, arg->skb, rt, NULL, NULL, NULL, 0,
			     RTM_NEWROUTE, NETLINK_CB(arg->cb->skb).portid,
			     arg->cb->nlh->nlmsg_seq, NLM_F_MULTI);
}

static int inet6_rtm_getroute(struct sk_buff *in_skb, struct nlmsghdr *nlh,
			      struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct nlattr *tb[RTA_MAX+1];
	int err, iif = 0, oif = 0;
	struct fib6_info *from;
	struct dst_entry *dst;
	struct rt6_info *rt;
	struct sk_buff *skb;
	struct rtmsg *rtm;
	struct flowi6 fl6;
	bool fibmatch;

	err = nlmsg_parse(nlh, sizeof(*rtm), tb, RTA_MAX, rtm_ipv6_policy,
			  extack);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	memset(&fl6, 0, sizeof(fl6));
	rtm = nlmsg_data(nlh);
	fl6.flowlabel = ip6_make_flowinfo(rtm->rtm_tos, 0);
	fibmatch = !!(rtm->rtm_flags & RTM_F_FIB_MATCH);

	if (tb[RTA_SRC]) {
		if (nla_len(tb[RTA_SRC]) < sizeof(struct in6_addr))
			goto errout;

		fl6.saddr = *(struct in6_addr *)nla_data(tb[RTA_SRC]);
	}

	if (tb[RTA_DST]) {
		if (nla_len(tb[RTA_DST]) < sizeof(struct in6_addr))
			goto errout;

		fl6.daddr = *(struct in6_addr *)nla_data(tb[RTA_DST]);
	}

	if (tb[RTA_IIF])
		iif = nla_get_u32(tb[RTA_IIF]);

	if (tb[RTA_OIF])
		oif = nla_get_u32(tb[RTA_OIF]);

	if (tb[RTA_MARK])
		fl6.flowi6_mark = nla_get_u32(tb[RTA_MARK]);

	if (tb[RTA_UID])
		fl6.flowi6_uid = make_kuid(current_user_ns(),
					   nla_get_u32(tb[RTA_UID]));
	else
		fl6.flowi6_uid = iif ? INVALID_UID : current_uid();

	if (tb[RTA_SPORT])
		fl6.fl6_sport = nla_get_be16(tb[RTA_SPORT]);

	if (tb[RTA_DPORT])
		fl6.fl6_dport = nla_get_be16(tb[RTA_DPORT]);

	if (tb[RTA_IP_PROTO]) {
		err = rtm_getroute_parse_ip_proto(tb[RTA_IP_PROTO],
						  &fl6.flowi6_proto, extack);
		if (err)
			goto errout;
	}

	if (iif) {
		struct net_device *dev;
		int flags = 0;

		rcu_read_lock();

		dev = dev_get_by_index_rcu(net, iif);
		if (!dev) {
			rcu_read_unlock();
			err = -ENODEV;
			goto errout;
		}

		fl6.flowi6_iif = iif;

		if (!ipv6_addr_any(&fl6.saddr))
			flags |= RT6_LOOKUP_F_HAS_SADDR;

		dst = ip6_route_input_lookup(net, dev, &fl6, NULL, flags);

		rcu_read_unlock();
	} else {
		fl6.flowi6_oif = oif;

		dst = ip6_route_output(net, NULL, &fl6);
	}


	rt = container_of(dst, struct rt6_info, dst);
	if (rt->dst.error) {
		err = rt->dst.error;
		ip6_rt_put(rt);
		goto errout;
	}

	if (rt == net->ipv6.ip6_null_entry) {
		err = rt->dst.error;
		ip6_rt_put(rt);
		goto errout;
	}

	skb = alloc_skb(NLMSG_GOODSIZE, GFP_KERNEL);
	if (!skb) {
		ip6_rt_put(rt);
		err = -ENOBUFS;
		goto errout;
	}

	skb_dst_set(skb, &rt->dst);

	rcu_read_lock();
	from = rcu_dereference(rt->from);

	if (fibmatch)
		err = rt6_fill_node(net, skb, from, NULL, NULL, NULL, iif,
				    RTM_NEWROUTE, NETLINK_CB(in_skb).portid,
				    nlh->nlmsg_seq, 0);
	else
		err = rt6_fill_node(net, skb, from, dst, &fl6.daddr,
				    &fl6.saddr, iif, RTM_NEWROUTE,
				    NETLINK_CB(in_skb).portid, nlh->nlmsg_seq,
				    0);
	rcu_read_unlock();

	if (err < 0) {
		kfree_skb(skb);
		goto errout;
	}

	err = rtnl_unicast(skb, net, NETLINK_CB(in_skb).portid);
errout:
	return err;
}

void inet6_rt_notify(int event, struct fib6_info *rt, struct nl_info *info,
		     unsigned int nlm_flags)
{
	struct sk_buff *skb;
	struct net *net = info->nl_net;
	u32 seq;
	int err;

	err = -ENOBUFS;
	seq = info->nlh ? info->nlh->nlmsg_seq : 0;

	skb = nlmsg_new(rt6_nlmsg_size(rt), gfp_any());
	if (!skb)
		goto errout;

	err = rt6_fill_node(net, skb, rt, NULL, NULL, NULL, 0,
			    event, info->portid, seq, nlm_flags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in rt6_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, info->portid, RTNLGRP_IPV6_ROUTE,
		    info->nlh, gfp_any());
	return;
errout:
	if (err < 0)
		rtnl_set_sk_err(net, RTNLGRP_IPV6_ROUTE, err);
}

static int ip6_route_dev_notify(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (!(dev->flags & IFF_LOOPBACK))
		return NOTIFY_OK;

	if (event == NETDEV_REGISTER) {
		net->ipv6.fib6_null_entry->fib6_nh.nh_dev = dev;
		net->ipv6.ip6_null_entry->dst.dev = dev;
		net->ipv6.ip6_null_entry->rt6i_idev = in6_dev_get(dev);
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
		net->ipv6.ip6_prohibit_entry->dst.dev = dev;
		net->ipv6.ip6_prohibit_entry->rt6i_idev = in6_dev_get(dev);
		net->ipv6.ip6_blk_hole_entry->dst.dev = dev;
		net->ipv6.ip6_blk_hole_entry->rt6i_idev = in6_dev_get(dev);
#endif
	 } else if (event == NETDEV_UNREGISTER &&
		    dev->reg_state != NETREG_UNREGISTERED) {
		/* NETDEV_UNREGISTER could be fired for multiple times by
		 * netdev_wait_allrefs(). Make sure we only call this once.
		 */
		in6_dev_put_clear(&net->ipv6.ip6_null_entry->rt6i_idev);
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
		in6_dev_put_clear(&net->ipv6.ip6_prohibit_entry->rt6i_idev);
		in6_dev_put_clear(&net->ipv6.ip6_blk_hole_entry->rt6i_idev);
#endif
	}

	return NOTIFY_OK;
}

/*
 *	/proc
 */

#ifdef CONFIG_PROC_FS
static int rt6_stats_seq_show(struct seq_file *seq, void *v)
{
	struct net *net = (struct net *)seq->private;
	seq_printf(seq, "%04x %04x %04x %04x %04x %04x %04x\n",
		   net->ipv6.rt6_stats->fib_nodes,
		   net->ipv6.rt6_stats->fib_route_nodes,
		   atomic_read(&net->ipv6.rt6_stats->fib_rt_alloc),
		   net->ipv6.rt6_stats->fib_rt_entries,
		   net->ipv6.rt6_stats->fib_rt_cache,
		   dst_entries_get_slow(&net->ipv6.ip6_dst_ops),
		   net->ipv6.rt6_stats->fib_discarded_routes);

	return 0;
}
#endif	/* CONFIG_PROC_FS */

#ifdef CONFIG_SYSCTL

static
int ipv6_sysctl_rtcache_flush(struct ctl_table *ctl, int write,
			      void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net;
	int delay;
	if (!write)
		return -EINVAL;

	net = (struct net *)ctl->extra1;
	delay = net->ipv6.sysctl.flush_delay;
	proc_dointvec(ctl, write, buffer, lenp, ppos);
	fib6_run_gc(delay <= 0 ? 0 : (unsigned long)delay, net, delay > 0);
	return 0;
}

struct ctl_table ipv6_route_table_template[] = {
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

		/* Don't export sysctls to unprivileged users */
		if (net->user_ns != &init_user_ns)
			table[0].procname = NULL;
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

	net->ipv6.fib6_null_entry = kmemdup(&fib6_null_entry_template,
					    sizeof(*net->ipv6.fib6_null_entry),
					    GFP_KERNEL);
	if (!net->ipv6.fib6_null_entry)
		goto out_ip6_dst_entries;

	net->ipv6.ip6_null_entry = kmemdup(&ip6_null_entry_template,
					   sizeof(*net->ipv6.ip6_null_entry),
					   GFP_KERNEL);
	if (!net->ipv6.ip6_null_entry)
		goto out_fib6_null_entry;
	net->ipv6.ip6_null_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_null_entry->dst,
			 ip6_template_metrics, true);

#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	net->ipv6.fib6_has_custom_rules = false;
	net->ipv6.ip6_prohibit_entry = kmemdup(&ip6_prohibit_entry_template,
					       sizeof(*net->ipv6.ip6_prohibit_entry),
					       GFP_KERNEL);
	if (!net->ipv6.ip6_prohibit_entry)
		goto out_ip6_null_entry;
	net->ipv6.ip6_prohibit_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_prohibit_entry->dst,
			 ip6_template_metrics, true);

	net->ipv6.ip6_blk_hole_entry = kmemdup(&ip6_blk_hole_entry_template,
					       sizeof(*net->ipv6.ip6_blk_hole_entry),
					       GFP_KERNEL);
	if (!net->ipv6.ip6_blk_hole_entry)
		goto out_ip6_prohibit_entry;
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
out_fib6_null_entry:
	kfree(net->ipv6.fib6_null_entry);
out_ip6_dst_entries:
	dst_entries_destroy(&net->ipv6.ip6_dst_ops);
out_ip6_dst_ops:
	goto out;
}

static void __net_exit ip6_route_net_exit(struct net *net)
{
	kfree(net->ipv6.fib6_null_entry);
	kfree(net->ipv6.ip6_null_entry);
#ifdef CONFIG_IPV6_MULTIPLE_TABLES
	kfree(net->ipv6.ip6_prohibit_entry);
	kfree(net->ipv6.ip6_blk_hole_entry);
#endif
	dst_entries_destroy(&net->ipv6.ip6_dst_ops);
}

static int __net_init ip6_route_net_init_late(struct net *net)
{
#ifdef CONFIG_PROC_FS
	proc_create_net("ipv6_route", 0, net->proc_net, &ipv6_route_seq_ops,
			sizeof(struct ipv6_route_iter));
	proc_create_net_single("rt6_stats", 0444, net->proc_net,
			rt6_stats_seq_show, NULL);
#endif
	return 0;
}

static void __net_exit ip6_route_net_exit_late(struct net *net)
{
#ifdef CONFIG_PROC_FS
	remove_proc_entry("ipv6_route", net->proc_net);
	remove_proc_entry("rt6_stats", net->proc_net);
#endif
}

static struct pernet_operations ip6_route_net_ops = {
	.init = ip6_route_net_init,
	.exit = ip6_route_net_exit,
};

static int __net_init ipv6_inetpeer_init(struct net *net)
{
	struct inet_peer_base *bp = kmalloc(sizeof(*bp), GFP_KERNEL);

	if (!bp)
		return -ENOMEM;
	inet_peer_base_init(bp);
	net->ipv6.peers = bp;
	return 0;
}

static void __net_exit ipv6_inetpeer_exit(struct net *net)
{
	struct inet_peer_base *bp = net->ipv6.peers;

	net->ipv6.peers = NULL;
	inetpeer_invalidate_tree(bp);
	kfree(bp);
}

static struct pernet_operations ipv6_inetpeer_ops = {
	.init	=	ipv6_inetpeer_init,
	.exit	=	ipv6_inetpeer_exit,
};

static struct pernet_operations ip6_route_net_late_ops = {
	.init = ip6_route_net_init_late,
	.exit = ip6_route_net_exit_late,
};

static struct notifier_block ip6_route_dev_notifier = {
	.notifier_call = ip6_route_dev_notify,
	.priority = ADDRCONF_NOTIFY_PRIORITY - 10,
};

void __init ip6_route_init_special_entries(void)
{
	/* Registering of the loopback is done before this portion of code,
	 * the loopback reference in rt6_info will not be taken, do it
	 * manually for init_net */
	init_net.ipv6.fib6_null_entry->fib6_nh.nh_dev = init_net.loopback_dev;
	init_net.ipv6.ip6_null_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_null_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
  #ifdef CONFIG_IPV6_MULTIPLE_TABLES
	init_net.ipv6.ip6_prohibit_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_prohibit_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
	init_net.ipv6.ip6_blk_hole_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_blk_hole_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
  #endif
}

int __init ip6_route_init(void)
{
	int ret;
	int cpu;

	ret = -ENOMEM;
	ip6_dst_ops_template.kmem_cachep =
		kmem_cache_create("ip6_dst_cache", sizeof(struct rt6_info), 0,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!ip6_dst_ops_template.kmem_cachep)
		goto out;

	ret = dst_entries_init(&ip6_dst_blackhole_ops);
	if (ret)
		goto out_kmem_cache;

	ret = register_pernet_subsys(&ipv6_inetpeer_ops);
	if (ret)
		goto out_dst_entries;

	ret = register_pernet_subsys(&ip6_route_net_ops);
	if (ret)
		goto out_register_inetpeer;

	ip6_dst_blackhole_ops.kmem_cachep = ip6_dst_ops_template.kmem_cachep;

	ret = fib6_init();
	if (ret)
		goto out_register_subsys;

	ret = xfrm6_init();
	if (ret)
		goto out_fib6_init;

	ret = fib6_rules_init();
	if (ret)
		goto xfrm6_init;

	ret = register_pernet_subsys(&ip6_route_net_late_ops);
	if (ret)
		goto fib6_rules_init;

	ret = rtnl_register_module(THIS_MODULE, PF_INET6, RTM_NEWROUTE,
				   inet6_rtm_newroute, NULL, 0);
	if (ret < 0)
		goto out_register_late_subsys;

	ret = rtnl_register_module(THIS_MODULE, PF_INET6, RTM_DELROUTE,
				   inet6_rtm_delroute, NULL, 0);
	if (ret < 0)
		goto out_register_late_subsys;

	ret = rtnl_register_module(THIS_MODULE, PF_INET6, RTM_GETROUTE,
				   inet6_rtm_getroute, NULL,
				   RTNL_FLAG_DOIT_UNLOCKED);
	if (ret < 0)
		goto out_register_late_subsys;

	ret = register_netdevice_notifier(&ip6_route_dev_notifier);
	if (ret)
		goto out_register_late_subsys;

	for_each_possible_cpu(cpu) {
		struct uncached_list *ul = per_cpu_ptr(&rt6_uncached_list, cpu);

		INIT_LIST_HEAD(&ul->head);
		spin_lock_init(&ul->lock);
	}

out:
	return ret;

out_register_late_subsys:
	rtnl_unregister_all(PF_INET6);
	unregister_pernet_subsys(&ip6_route_net_late_ops);
fib6_rules_init:
	fib6_rules_cleanup();
xfrm6_init:
	xfrm6_fini();
out_fib6_init:
	fib6_gc_cleanup();
out_register_subsys:
	unregister_pernet_subsys(&ip6_route_net_ops);
out_register_inetpeer:
	unregister_pernet_subsys(&ipv6_inetpeer_ops);
out_dst_entries:
	dst_entries_destroy(&ip6_dst_blackhole_ops);
out_kmem_cache:
	kmem_cache_destroy(ip6_dst_ops_template.kmem_cachep);
	goto out;
}

void ip6_route_cleanup(void)
{
	unregister_netdevice_notifier(&ip6_route_dev_notifier);
	unregister_pernet_subsys(&ip6_route_net_late_ops);
	fib6_rules_cleanup();
	xfrm6_fini();
	fib6_gc_cleanup();
	unregister_pernet_subsys(&ipv6_inetpeer_ops);
	unregister_pernet_subsys(&ip6_route_net_ops);
	dst_entries_destroy(&ip6_dst_blackhole_ops);
	kmem_cache_destroy(ip6_dst_ops_template.kmem_cachep);
}
