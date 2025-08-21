// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Linux INET6 implementation
 *	FIB front-end.
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
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
#include <linux/siphash.h>
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
#include <net/rtnh.h>
#include <net/lwtunnel.h>
#include <net/ip_tunnels.h>
#include <net/l3mdev.h>
#include <net/ip.h>
#include <linux/uaccess.h>
#include <linux/btf_ids.h>

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

INDIRECT_CALLABLE_SCOPE
struct dst_entry	*ip6_dst_check(struct dst_entry *dst, u32 cookie);
static unsigned int	 ip6_default_advmss(const struct dst_entry *dst);
INDIRECT_CALLABLE_SCOPE
unsigned int		ip6_mtu(const struct dst_entry *dst);
static void		ip6_negative_advice(struct sock *sk,
					    struct dst_entry *dst);
static void		ip6_dst_destroy(struct dst_entry *);
static void		ip6_dst_ifdown(struct dst_entry *,
				       struct net_device *dev);
static void		 ip6_dst_gc(struct dst_ops *ops);

static int		ip6_pkt_discard(struct sk_buff *skb);
static int		ip6_pkt_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb);
static int		ip6_pkt_prohibit(struct sk_buff *skb);
static int		ip6_pkt_prohibit_out(struct net *net, struct sock *sk, struct sk_buff *skb);
static void		ip6_link_failure(struct sk_buff *skb);
static void		ip6_rt_update_pmtu(struct dst_entry *dst, struct sock *sk,
					   struct sk_buff *skb, u32 mtu,
					   bool confirm_neigh);
static void		rt6_do_redirect(struct dst_entry *dst, struct sock *sk,
					struct sk_buff *skb);
static int rt6_score_route(const struct fib6_nh *nh, u32 fib6_flags, int oif,
			   int strict);
static size_t rt6_nlmsg_size(struct fib6_info *f6i);
static int rt6_fill_node(struct net *net, struct sk_buff *skb,
			 struct fib6_info *rt, struct dst_entry *dst,
			 struct in6_addr *dest, struct in6_addr *src,
			 int iif, int type, u32 portid, u32 seq,
			 unsigned int flags);
static struct rt6_info *rt6_find_cached_rt(const struct fib6_result *res,
					   const struct in6_addr *daddr,
					   const struct in6_addr *saddr);

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

	rt->dst.rt_uncached_list = ul;

	spin_lock_bh(&ul->lock);
	list_add_tail(&rt->dst.rt_uncached, &ul->head);
	spin_unlock_bh(&ul->lock);
}

void rt6_uncached_list_del(struct rt6_info *rt)
{
	if (!list_empty(&rt->dst.rt_uncached)) {
		struct uncached_list *ul = rt->dst.rt_uncached_list;

		spin_lock_bh(&ul->lock);
		list_del_init(&rt->dst.rt_uncached);
		spin_unlock_bh(&ul->lock);
	}
}

static void rt6_uncached_list_flush_dev(struct net_device *dev)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct uncached_list *ul = per_cpu_ptr(&rt6_uncached_list, cpu);
		struct rt6_info *rt, *safe;

		if (list_empty(&ul->head))
			continue;

		spin_lock_bh(&ul->lock);
		list_for_each_entry_safe(rt, safe, &ul->head, dst.rt_uncached) {
			struct inet6_dev *rt_idev = rt->rt6i_idev;
			struct net_device *rt_dev = rt->dst.dev;
			bool handled = false;

			if (rt_idev && rt_idev->dev == dev) {
				rt->rt6i_idev = in6_dev_get(blackhole_netdev);
				in6_dev_put(rt_idev);
				handled = true;
			}

			if (rt_dev == dev) {
				rt->dst.dev = blackhole_netdev;
				netdev_ref_replace(rt_dev, blackhole_netdev,
						   &rt->dst.dev_tracker,
						   GFP_ATOMIC);
				handled = true;
			}
			if (handled)
				list_del_init(&rt->dst.rt_uncached);
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

	n = neigh_create(&nd_tbl, daddr, dev);
	return IS_ERR(n) ? NULL : n;
}

static struct neighbour *ip6_dst_neigh_lookup(const struct dst_entry *dst,
					      struct sk_buff *skb,
					      const void *daddr)
{
	const struct rt6_info *rt = dst_rt6_info(dst);

	return ip6_neigh_lookup(rt6_nexthop(rt, &in6addr_any),
				dst_dev(dst), skb, daddr);
}

static void ip6_confirm_neigh(const struct dst_entry *dst, const void *daddr)
{
	const struct rt6_info *rt = dst_rt6_info(dst);
	struct net_device *dev = dst_dev(dst);

	daddr = choose_neigh_daddr(rt6_nexthop(rt, &in6addr_any), NULL, daddr);
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

static struct dst_ops ip6_dst_blackhole_ops = {
	.family			= AF_INET6,
	.default_advmss		= ip6_default_advmss,
	.neigh_lookup		= ip6_dst_neigh_lookup,
	.check			= ip6_dst_check,
	.destroy		= ip6_dst_destroy,
	.cow_metrics		= dst_cow_metrics_generic,
	.update_pmtu		= dst_blackhole_update_pmtu,
	.redirect		= dst_blackhole_redirect,
	.mtu			= dst_blackhole_mtu,
};

static const u32 ip6_template_metrics[RTAX_MAX] = {
	[RTAX_HOPLIMIT - 1] = 0,
};

static const struct fib6_info fib6_null_entry_template = {
	.fib6_flags	= (RTF_REJECT | RTF_NONEXTHOP),
	.fib6_protocol  = RTPROT_KERNEL,
	.fib6_metric	= ~(u32)0,
	.fib6_ref	= REFCOUNT_INIT(1),
	.fib6_type	= RTN_UNREACHABLE,
	.fib6_metrics	= (struct dst_metrics *)&dst_default_metrics,
};

static const struct rt6_info ip6_null_entry_template = {
	.dst = {
		.__rcuref	= RCUREF_INIT(1),
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
		.__rcuref	= RCUREF_INIT(1),
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
		.__rcuref	= RCUREF_INIT(1),
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
	memset_after(rt, 0, dst);
}

/* allocate dst with ip6_dst_ops */
struct rt6_info *ip6_dst_alloc(struct net *net, struct net_device *dev,
			       int flags)
{
	struct rt6_info *rt = dst_alloc(&net->ipv6.ip6_dst_ops, dev,
					DST_OBSOLETE_FORCE_CHK, flags);

	if (rt) {
		rt6_info_init(rt);
		atomic_inc(&net->ipv6.rt6_stats->fib_rt_alloc);
	}

	return rt;
}
EXPORT_SYMBOL(ip6_dst_alloc);

static void ip6_dst_destroy(struct dst_entry *dst)
{
	struct rt6_info *rt = dst_rt6_info(dst);
	struct fib6_info *from;
	struct inet6_dev *idev;

	ip_dst_metrics_put(dst);
	rt6_uncached_list_del(rt);

	idev = rt->rt6i_idev;
	if (idev) {
		rt->rt6i_idev = NULL;
		in6_dev_put(idev);
	}

	from = unrcu_pointer(xchg(&rt->from, NULL));
	fib6_info_release(from);
}

static void ip6_dst_ifdown(struct dst_entry *dst, struct net_device *dev)
{
	struct rt6_info *rt = dst_rt6_info(dst);
	struct inet6_dev *idev = rt->rt6i_idev;
	struct fib6_info *from;

	if (idev && idev->dev != blackhole_netdev) {
		struct inet6_dev *blackhole_idev = in6_dev_get(blackhole_netdev);

		if (blackhole_idev) {
			rt->rt6i_idev = blackhole_idev;
			in6_dev_put(idev);
		}
	}
	from = unrcu_pointer(xchg(&rt->from, NULL));
	fib6_info_release(from);
}

static bool __rt6_check_expired(const struct rt6_info *rt)
{
	if (rt->rt6i_flags & RTF_EXPIRES)
		return time_after(jiffies, READ_ONCE(rt->dst.expires));
	return false;
}

static bool rt6_check_expired(const struct rt6_info *rt)
{
	struct fib6_info *from;

	from = rcu_dereference(rt->from);

	if (rt->rt6i_flags & RTF_EXPIRES) {
		if (time_after(jiffies, READ_ONCE(rt->dst.expires)))
			return true;
	} else if (from) {
		return READ_ONCE(rt->dst.obsolete) != DST_OBSOLETE_FORCE_CHK ||
			fib6_check_expired(from);
	}
	return false;
}

static struct fib6_info *
rt6_multipath_first_sibling_rcu(const struct fib6_info *rt)
{
	struct fib6_info *iter;
	struct fib6_node *fn;

	fn = rcu_dereference(rt->fib6_node);
	if (!fn)
		goto out;
	iter = rcu_dereference(fn->leaf);
	if (!iter)
		goto out;

	while (iter) {
		if (iter->fib6_metric == rt->fib6_metric &&
		    rt6_qualify_for_ecmp(iter))
			return iter;
		iter = rcu_dereference(iter->fib6_next);
	}

out:
	return NULL;
}

void fib6_select_path(const struct net *net, struct fib6_result *res,
		      struct flowi6 *fl6, int oif, bool have_oif_match,
		      const struct sk_buff *skb, int strict)
{
	struct fib6_info *first, *match = res->f6i;
	struct fib6_info *sibling;
	int hash;

	if (!match->nh && (!match->fib6_nsiblings || have_oif_match))
		goto out;

	if (match->nh && have_oif_match && res->nh)
		return;

	if (skb)
		IP6CB(skb)->flags |= IP6SKB_MULTIPATH;

	/* We might have already computed the hash for ICMPv6 errors. In such
	 * case it will always be non-zero. Otherwise now is the time to do it.
	 */
	if (!fl6->mp_hash &&
	    (!match->nh || nexthop_is_multipath(match->nh)))
		fl6->mp_hash = rt6_multipath_hash(net, fl6, skb, NULL);

	if (unlikely(match->nh)) {
		nexthop_path_fib6_result(res, fl6->mp_hash);
		return;
	}

	first = rt6_multipath_first_sibling_rcu(match);
	if (!first)
		goto out;

	hash = fl6->mp_hash;
	if (hash <= atomic_read(&first->fib6_nh->fib_nh_upper_bound)) {
		if (rt6_score_route(first->fib6_nh, first->fib6_flags, oif,
				    strict) >= 0)
			match = first;
		goto out;
	}

	list_for_each_entry_rcu(sibling, &first->fib6_siblings,
				fib6_siblings) {
		const struct fib6_nh *nh = sibling->fib6_nh;
		int nh_upper_bound;

		nh_upper_bound = atomic_read(&nh->fib_nh_upper_bound);
		if (hash > nh_upper_bound)
			continue;
		if (rt6_score_route(nh, sibling->fib6_flags, oif, strict) < 0)
			break;
		match = sibling;
		break;
	}

out:
	res->f6i = match;
	res->nh = match->fib6_nh;
}

/*
 *	Route lookup. rcu_read_lock() should be held.
 */

static bool __rt6_device_match(struct net *net, const struct fib6_nh *nh,
			       const struct in6_addr *saddr, int oif, int flags)
{
	const struct net_device *dev;

	if (nh->fib_nh_flags & RTNH_F_DEAD)
		return false;

	dev = nh->fib_nh_dev;
	if (oif) {
		if (dev->ifindex == oif)
			return true;
	} else {
		if (ipv6_chk_addr(net, saddr, dev,
				  flags & RT6_LOOKUP_F_IFACE))
			return true;
	}

	return false;
}

struct fib6_nh_dm_arg {
	struct net		*net;
	const struct in6_addr	*saddr;
	int			oif;
	int			flags;
	struct fib6_nh		*nh;
};

static int __rt6_nh_dev_match(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_dm_arg *arg = _arg;

	arg->nh = nh;
	return __rt6_device_match(arg->net, nh, arg->saddr, arg->oif,
				  arg->flags);
}

/* returns fib6_nh from nexthop or NULL */
static struct fib6_nh *rt6_nh_dev_match(struct net *net, struct nexthop *nh,
					struct fib6_result *res,
					const struct in6_addr *saddr,
					int oif, int flags)
{
	struct fib6_nh_dm_arg arg = {
		.net   = net,
		.saddr = saddr,
		.oif   = oif,
		.flags = flags,
	};

	if (nexthop_is_blackhole(nh))
		return NULL;

	if (nexthop_for_each_fib6_nh(nh, __rt6_nh_dev_match, &arg))
		return arg.nh;

	return NULL;
}

static void rt6_device_match(struct net *net, struct fib6_result *res,
			     const struct in6_addr *saddr, int oif, int flags)
{
	struct fib6_info *f6i = res->f6i;
	struct fib6_info *spf6i;
	struct fib6_nh *nh;

	if (!oif && ipv6_addr_any(saddr)) {
		if (unlikely(f6i->nh)) {
			nh = nexthop_fib6_nh(f6i->nh);
			if (nexthop_is_blackhole(f6i->nh))
				goto out_blackhole;
		} else {
			nh = f6i->fib6_nh;
		}
		if (!(nh->fib_nh_flags & RTNH_F_DEAD))
			goto out;
	}

	for (spf6i = f6i; spf6i; spf6i = rcu_dereference(spf6i->fib6_next)) {
		bool matched = false;

		if (unlikely(spf6i->nh)) {
			nh = rt6_nh_dev_match(net, spf6i->nh, res, saddr,
					      oif, flags);
			if (nh)
				matched = true;
		} else {
			nh = spf6i->fib6_nh;
			if (__rt6_device_match(net, nh, saddr, oif, flags))
				matched = true;
		}
		if (matched) {
			res->f6i = spf6i;
			goto out;
		}
	}

	if (oif && flags & RT6_LOOKUP_F_IFACE) {
		res->f6i = net->ipv6.fib6_null_entry;
		nh = res->f6i->fib6_nh;
		goto out;
	}

	if (unlikely(f6i->nh)) {
		nh = nexthop_fib6_nh(f6i->nh);
		if (nexthop_is_blackhole(f6i->nh))
			goto out_blackhole;
	} else {
		nh = f6i->fib6_nh;
	}

	if (nh->fib_nh_flags & RTNH_F_DEAD) {
		res->f6i = net->ipv6.fib6_null_entry;
		nh = res->f6i->fib6_nh;
	}
out:
	res->nh = nh;
	res->fib6_type = res->f6i->fib6_type;
	res->fib6_flags = res->f6i->fib6_flags;
	return;

out_blackhole:
	res->fib6_flags |= RTF_REJECT;
	res->fib6_type = RTN_BLACKHOLE;
	res->nh = nh;
}

#ifdef CONFIG_IPV6_ROUTER_PREF
struct __rt6_probe_work {
	struct work_struct work;
	struct in6_addr target;
	struct net_device *dev;
	netdevice_tracker dev_tracker;
};

static void rt6_probe_deferred(struct work_struct *w)
{
	struct in6_addr mcaddr;
	struct __rt6_probe_work *work =
		container_of(w, struct __rt6_probe_work, work);

	addrconf_addr_solict_mult(&work->target, &mcaddr);
	ndisc_send_ns(work->dev, &work->target, &mcaddr, NULL, 0);
	netdev_put(work->dev, &work->dev_tracker);
	kfree(work);
}

static void rt6_probe(struct fib6_nh *fib6_nh)
{
	struct __rt6_probe_work *work = NULL;
	const struct in6_addr *nh_gw;
	unsigned long last_probe;
	struct neighbour *neigh;
	struct net_device *dev;
	struct inet6_dev *idev;

	/*
	 * Okay, this does not seem to be appropriate
	 * for now, however, we need to check if it
	 * is really so; aka Router Reachability Probing.
	 *
	 * Router Reachability Probe MUST be rate-limited
	 * to no more than one per minute.
	 */
	if (!fib6_nh->fib_nh_gw_family)
		return;

	nh_gw = &fib6_nh->fib_nh_gw6;
	dev = fib6_nh->fib_nh_dev;
	rcu_read_lock();
	last_probe = READ_ONCE(fib6_nh->last_probe);
	idev = __in6_dev_get(dev);
	if (!idev)
		goto out;
	neigh = __ipv6_neigh_lookup_noref(dev, nh_gw);
	if (neigh) {
		if (READ_ONCE(neigh->nud_state) & NUD_VALID)
			goto out;

		write_lock_bh(&neigh->lock);
		if (!(neigh->nud_state & NUD_VALID) &&
		    time_after(jiffies,
			       neigh->updated +
			       READ_ONCE(idev->cnf.rtr_probe_interval))) {
			work = kmalloc(sizeof(*work), GFP_ATOMIC);
			if (work)
				__neigh_set_probe_once(neigh);
		}
		write_unlock_bh(&neigh->lock);
	} else if (time_after(jiffies, last_probe +
				       READ_ONCE(idev->cnf.rtr_probe_interval))) {
		work = kmalloc(sizeof(*work), GFP_ATOMIC);
	}

	if (!work || cmpxchg(&fib6_nh->last_probe,
			     last_probe, jiffies) != last_probe) {
		kfree(work);
	} else {
		INIT_WORK(&work->work, rt6_probe_deferred);
		work->target = *nh_gw;
		netdev_hold(dev, &work->dev_tracker, GFP_ATOMIC);
		work->dev = dev;
		schedule_work(&work->work);
	}

out:
	rcu_read_unlock();
}
#else
static inline void rt6_probe(struct fib6_nh *fib6_nh)
{
}
#endif

/*
 * Default Router Selection (RFC 2461 6.3.6)
 */
static enum rt6_nud_state rt6_check_neigh(const struct fib6_nh *fib6_nh)
{
	enum rt6_nud_state ret = RT6_NUD_FAIL_HARD;
	struct neighbour *neigh;

	rcu_read_lock();
	neigh = __ipv6_neigh_lookup_noref(fib6_nh->fib_nh_dev,
					  &fib6_nh->fib_nh_gw6);
	if (neigh) {
		u8 nud_state = READ_ONCE(neigh->nud_state);

		if (nud_state & NUD_VALID)
			ret = RT6_NUD_SUCCEED;
#ifdef CONFIG_IPV6_ROUTER_PREF
		else if (!(nud_state & NUD_FAILED))
			ret = RT6_NUD_SUCCEED;
		else
			ret = RT6_NUD_FAIL_PROBE;
#endif
	} else {
		ret = IS_ENABLED(CONFIG_IPV6_ROUTER_PREF) ?
		      RT6_NUD_SUCCEED : RT6_NUD_FAIL_DO_RR;
	}
	rcu_read_unlock();

	return ret;
}

static int rt6_score_route(const struct fib6_nh *nh, u32 fib6_flags, int oif,
			   int strict)
{
	int m = 0;

	if (!oif || nh->fib_nh_dev->ifindex == oif)
		m = 2;

	if (!m && (strict & RT6_LOOKUP_F_IFACE))
		return RT6_NUD_FAIL_HARD;
#ifdef CONFIG_IPV6_ROUTER_PREF
	m |= IPV6_DECODE_PREF(IPV6_EXTRACT_PREF(fib6_flags)) << 2;
#endif
	if ((strict & RT6_LOOKUP_F_REACHABLE) &&
	    !(fib6_flags & RTF_NONEXTHOP) && nh->fib_nh_gw_family) {
		int n = rt6_check_neigh(nh);
		if (n < 0)
			return n;
	}
	return m;
}

static bool find_match(struct fib6_nh *nh, u32 fib6_flags,
		       int oif, int strict, int *mpri, bool *do_rr)
{
	bool match_do_rr = false;
	bool rc = false;
	int m;

	if (nh->fib_nh_flags & RTNH_F_DEAD)
		goto out;

	if (ip6_ignore_linkdown(nh->fib_nh_dev) &&
	    nh->fib_nh_flags & RTNH_F_LINKDOWN &&
	    !(strict & RT6_LOOKUP_F_IGNORE_LINKSTATE))
		goto out;

	m = rt6_score_route(nh, fib6_flags, oif, strict);
	if (m == RT6_NUD_FAIL_DO_RR) {
		match_do_rr = true;
		m = 0; /* lowest valid score */
	} else if (m == RT6_NUD_FAIL_HARD) {
		goto out;
	}

	if (strict & RT6_LOOKUP_F_REACHABLE)
		rt6_probe(nh);

	/* note that m can be RT6_NUD_FAIL_PROBE at this point */
	if (m > *mpri) {
		*do_rr = match_do_rr;
		*mpri = m;
		rc = true;
	}
out:
	return rc;
}

struct fib6_nh_frl_arg {
	u32		flags;
	int		oif;
	int		strict;
	int		*mpri;
	bool		*do_rr;
	struct fib6_nh	*nh;
};

static int rt6_nh_find_match(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_frl_arg *arg = _arg;

	arg->nh = nh;
	return find_match(nh, arg->flags, arg->oif, arg->strict,
			  arg->mpri, arg->do_rr);
}

static void __find_rr_leaf(struct fib6_info *f6i_start,
			   struct fib6_info *nomatch, u32 metric,
			   struct fib6_result *res, struct fib6_info **cont,
			   int oif, int strict, bool *do_rr, int *mpri)
{
	struct fib6_info *f6i;

	for (f6i = f6i_start;
	     f6i && f6i != nomatch;
	     f6i = rcu_dereference(f6i->fib6_next)) {
		bool matched = false;
		struct fib6_nh *nh;

		if (cont && f6i->fib6_metric != metric) {
			*cont = f6i;
			return;
		}

		if (fib6_check_expired(f6i))
			continue;

		if (unlikely(f6i->nh)) {
			struct fib6_nh_frl_arg arg = {
				.flags  = f6i->fib6_flags,
				.oif    = oif,
				.strict = strict,
				.mpri   = mpri,
				.do_rr  = do_rr
			};

			if (nexthop_is_blackhole(f6i->nh)) {
				res->fib6_flags = RTF_REJECT;
				res->fib6_type = RTN_BLACKHOLE;
				res->f6i = f6i;
				res->nh = nexthop_fib6_nh(f6i->nh);
				return;
			}
			if (nexthop_for_each_fib6_nh(f6i->nh, rt6_nh_find_match,
						     &arg)) {
				matched = true;
				nh = arg.nh;
			}
		} else {
			nh = f6i->fib6_nh;
			if (find_match(nh, f6i->fib6_flags, oif, strict,
				       mpri, do_rr))
				matched = true;
		}
		if (matched) {
			res->f6i = f6i;
			res->nh = nh;
			res->fib6_flags = f6i->fib6_flags;
			res->fib6_type = f6i->fib6_type;
		}
	}
}

static void find_rr_leaf(struct fib6_node *fn, struct fib6_info *leaf,
			 struct fib6_info *rr_head, int oif, int strict,
			 bool *do_rr, struct fib6_result *res)
{
	u32 metric = rr_head->fib6_metric;
	struct fib6_info *cont = NULL;
	int mpri = -1;

	__find_rr_leaf(rr_head, NULL, metric, res, &cont,
		       oif, strict, do_rr, &mpri);

	__find_rr_leaf(leaf, rr_head, metric, res, &cont,
		       oif, strict, do_rr, &mpri);

	if (res->f6i || !cont)
		return;

	__find_rr_leaf(cont, NULL, metric, res, NULL,
		       oif, strict, do_rr, &mpri);
}

static void rt6_select(struct net *net, struct fib6_node *fn, int oif,
		       struct fib6_result *res, int strict)
{
	struct fib6_info *leaf = rcu_dereference(fn->leaf);
	struct fib6_info *rt0;
	bool do_rr = false;
	int key_plen;

	/* make sure this function or its helpers sets f6i */
	res->f6i = NULL;

	if (!leaf || leaf == net->ipv6.fib6_null_entry)
		goto out;

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
		goto out;

	find_rr_leaf(fn, leaf, rt0, oif, strict, &do_rr, res);
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

out:
	if (!res->f6i) {
		res->f6i = net->ipv6.fib6_null_entry;
		res->nh = res->f6i->fib6_nh;
		res->fib6_flags = res->f6i->fib6_flags;
		res->fib6_type = res->f6i->fib6_type;
	}
}

static bool rt6_is_gw_or_nonexthop(const struct fib6_result *res)
{
	return (res->f6i->fib6_flags & RTF_NONEXTHOP) ||
	       res->nh->fib_nh_gw_family;
}

#ifdef CONFIG_IPV6_ROUTE_INFO
int rt6_route_rcv(struct net_device *dev, u8 *opt, int len,
		  const struct in6_addr *gwaddr)
{
	struct net *net = dev_net(dev);
	struct route_info *rinfo = (struct route_info *) opt;
	struct in6_addr prefix_buf, *prefix;
	struct fib6_table *table;
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
		ip6_del_rt(net, rt, false);
		rt = NULL;
	}

	if (!rt && lifetime)
		rt = rt6_add_route_info(net, prefix, rinfo->prefix_len, gwaddr,
					dev, pref);
	else if (rt)
		rt->fib6_flags = RTF_ROUTEINFO |
				 (rt->fib6_flags & ~RTF_PREF_MASK) | RTF_PREF(pref);

	if (rt) {
		table = rt->fib6_table;
		spin_lock_bh(&table->tb6_lock);

		if (!addrconf_finite_timeout(lifetime)) {
			fib6_clean_expires(rt);
			fib6_remove_gc_list(rt);
		} else {
			fib6_set_expires(rt, jiffies + HZ * lifetime);
			fib6_add_gc_list(rt);
		}

		spin_unlock_bh(&table->tb6_lock);

		fib6_info_release(rt);
	}
	return 0;
}
#endif

/*
 *	Misc support functions
 */

/* called with rcu_lock held */
static struct net_device *ip6_rt_get_dev_rcu(const struct fib6_result *res)
{
	struct net_device *dev = res->nh->fib_nh_dev;

	if (res->fib6_flags & (RTF_LOCAL | RTF_ANYCAST)) {
		/* for copies of local routes, dst->dev needs to be the
		 * device if it is a master device, the master device if
		 * device is enslaved, and the loopback as the default
		 */
		if (netif_is_l3_slave(dev) &&
		    !rt6_need_strict(&res->f6i->fib6_dst.addr))
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

	return flags;
}

static void ip6_rt_init_dst_reject(struct rt6_info *rt, u8 fib6_type)
{
	rt->dst.error = ip6_rt_type_to_error(fib6_type);

	switch (fib6_type) {
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

static void ip6_rt_init_dst(struct rt6_info *rt, const struct fib6_result *res)
{
	struct fib6_info *f6i = res->f6i;

	if (res->fib6_flags & RTF_REJECT) {
		ip6_rt_init_dst_reject(rt, res->fib6_type);
		return;
	}

	rt->dst.error = 0;
	rt->dst.output = ip6_output;

	if (res->fib6_type == RTN_LOCAL || res->fib6_type == RTN_ANYCAST) {
		rt->dst.input = ip6_input;
	} else if (ipv6_addr_type(&f6i->fib6_dst.addr) & IPV6_ADDR_MULTICAST) {
		rt->dst.input = ip6_mc_input;
		rt->dst.output = ip6_mr_output;
	} else {
		rt->dst.input = ip6_forward;
	}

	if (res->nh->fib_nh_lws) {
		rt->dst.lwtstate = lwtstate_get(res->nh->fib_nh_lws);
		lwtunnel_set_redirect(&rt->dst);
	}

	rt->dst.lastuse = jiffies;
}

/* Caller must already hold reference to @from */
static void rt6_set_from(struct rt6_info *rt, struct fib6_info *from)
{
	rt->rt6i_flags &= ~RTF_EXPIRES;
	rcu_assign_pointer(rt->from, from);
	ip_dst_init_metrics(&rt->dst, from->fib6_metrics);
}

/* Caller must already hold reference to f6i in result */
static void ip6_rt_copy_init(struct rt6_info *rt, const struct fib6_result *res)
{
	const struct fib6_nh *nh = res->nh;
	const struct net_device *dev = nh->fib_nh_dev;
	struct fib6_info *f6i = res->f6i;

	ip6_rt_init_dst(rt, res);

	rt->rt6i_dst = f6i->fib6_dst;
	rt->rt6i_idev = dev ? in6_dev_get(dev) : NULL;
	rt->rt6i_flags = res->fib6_flags;
	if (nh->fib_nh_gw_family) {
		rt->rt6i_gateway = nh->fib_nh_gw6;
		rt->rt6i_flags |= RTF_GATEWAY;
	}
	rt6_set_from(rt, f6i);
#ifdef CONFIG_IPV6_SUBTREES
	rt->rt6i_src = f6i->fib6_src;
#endif
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

static bool ip6_hold_safe(struct net *net, struct rt6_info **prt)
{
	struct rt6_info *rt = *prt;

	if (dst_hold_safe(&rt->dst))
		return true;
	if (net) {
		rt = net->ipv6.ip6_null_entry;
		dst_hold(&rt->dst);
	} else {
		rt = NULL;
	}
	*prt = rt;
	return false;
}

/* called with rcu_lock held */
static struct rt6_info *ip6_create_rt_rcu(const struct fib6_result *res)
{
	struct net_device *dev = res->nh->fib_nh_dev;
	struct fib6_info *f6i = res->f6i;
	unsigned short flags;
	struct rt6_info *nrt;

	if (!fib6_info_hold_safe(f6i))
		goto fallback;

	flags = fib6_info_dst_flags(f6i);
	nrt = ip6_dst_alloc(dev_net(dev), dev, flags);
	if (!nrt) {
		fib6_info_release(f6i);
		goto fallback;
	}

	ip6_rt_copy_init(nrt, res);
	return nrt;

fallback:
	nrt = dev_net(dev)->ipv6.ip6_null_entry;
	dst_hold(&nrt->dst);
	return nrt;
}

INDIRECT_CALLABLE_SCOPE struct rt6_info *ip6_pol_route_lookup(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     const struct sk_buff *skb,
					     int flags)
{
	struct fib6_result res = {};
	struct fib6_node *fn;
	struct rt6_info *rt;

	rcu_read_lock();
	fn = fib6_node_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
restart:
	res.f6i = rcu_dereference(fn->leaf);
	if (!res.f6i)
		res.f6i = net->ipv6.fib6_null_entry;
	else
		rt6_device_match(net, &res, &fl6->saddr, fl6->flowi6_oif,
				 flags);

	if (res.f6i == net->ipv6.fib6_null_entry) {
		fn = fib6_backtrack(fn, &fl6->saddr);
		if (fn)
			goto restart;

		rt = net->ipv6.ip6_null_entry;
		dst_hold(&rt->dst);
		goto out;
	} else if (res.fib6_flags & RTF_REJECT) {
		goto do_create;
	}

	fib6_select_path(net, &res, fl6, fl6->flowi6_oif,
			 fl6->flowi6_oif != 0, skb, flags);

	/* Search through exception table */
	rt = rt6_find_cached_rt(&res, &fl6->daddr, &fl6->saddr);
	if (rt) {
		if (ip6_hold_safe(net, &rt))
			dst_use_noref(&rt->dst, jiffies);
	} else {
do_create:
		rt = ip6_create_rt_rcu(&res);
	}

out:
	trace_fib6_table_lookup(net, &res, table, fl6);

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
		return dst_rt6_info(dst);

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

static struct rt6_info *ip6_rt_cache_alloc(const struct fib6_result *res,
					   const struct in6_addr *daddr,
					   const struct in6_addr *saddr)
{
	struct fib6_info *f6i = res->f6i;
	struct net_device *dev;
	struct rt6_info *rt;

	/*
	 *	Clone the route.
	 */

	if (!fib6_info_hold_safe(f6i))
		return NULL;

	dev = ip6_rt_get_dev_rcu(res);
	rt = ip6_dst_alloc(dev_net(dev), dev, 0);
	if (!rt) {
		fib6_info_release(f6i);
		return NULL;
	}

	ip6_rt_copy_init(rt, res);
	rt->rt6i_flags |= RTF_CACHE;
	rt->rt6i_dst.addr = *daddr;
	rt->rt6i_dst.plen = 128;

	if (!rt6_is_gw_or_nonexthop(res)) {
		if (f6i->fib6_dst.plen != 128 &&
		    ipv6_addr_equal(&f6i->fib6_dst.addr, daddr))
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

static struct rt6_info *ip6_rt_pcpu_alloc(const struct fib6_result *res)
{
	struct fib6_info *f6i = res->f6i;
	unsigned short flags = fib6_info_dst_flags(f6i);
	struct net_device *dev;
	struct rt6_info *pcpu_rt;

	if (!fib6_info_hold_safe(f6i))
		return NULL;

	rcu_read_lock();
	dev = ip6_rt_get_dev_rcu(res);
	pcpu_rt = ip6_dst_alloc(dev_net(dev), dev, flags | DST_NOCOUNT);
	rcu_read_unlock();
	if (!pcpu_rt) {
		fib6_info_release(f6i);
		return NULL;
	}
	ip6_rt_copy_init(pcpu_rt, res);
	pcpu_rt->rt6i_flags |= RTF_PCPU;

	if (f6i->nh)
		pcpu_rt->sernum = rt_genid_ipv6(dev_net(dev));

	return pcpu_rt;
}

static bool rt6_is_valid(const struct rt6_info *rt6)
{
	return rt6->sernum == rt_genid_ipv6(dev_net(rt6->dst.dev));
}

/* It should be called with rcu_read_lock() acquired */
static struct rt6_info *rt6_get_pcpu_route(const struct fib6_result *res)
{
	struct rt6_info *pcpu_rt;

	pcpu_rt = this_cpu_read(*res->nh->rt6i_pcpu);

	if (pcpu_rt && pcpu_rt->sernum && !rt6_is_valid(pcpu_rt)) {
		struct rt6_info *prev, **p;

		p = this_cpu_ptr(res->nh->rt6i_pcpu);
		/* Paired with READ_ONCE() in __fib6_drop_pcpu_from() */
		prev = xchg(p, NULL);
		if (prev) {
			dst_dev_put(&prev->dst);
			dst_release(&prev->dst);
		}

		pcpu_rt = NULL;
	}

	return pcpu_rt;
}

static struct rt6_info *rt6_make_pcpu_route(struct net *net,
					    const struct fib6_result *res)
{
	struct rt6_info *pcpu_rt, *prev, **p;

	pcpu_rt = ip6_rt_pcpu_alloc(res);
	if (!pcpu_rt)
		return NULL;

	p = this_cpu_ptr(res->nh->rt6i_pcpu);
	prev = cmpxchg(p, NULL, pcpu_rt);
	BUG_ON(prev);

	if (res->f6i->fib6_destroying) {
		struct fib6_info *from;

		from = unrcu_pointer(xchg(&pcpu_rt->from, NULL));
		fib6_info_release(from);
	}

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
	net->ipv6.rt6_stats->fib_rt_cache--;

	/* purge completely the exception to allow releasing the held resources:
	 * some [sk] cache may keep the dst around for unlimited time
	 */
	dst_dev_put(&rt6_ex->rt6i->dst);

	hlist_del_rcu(&rt6_ex->hlist);
	dst_release(&rt6_ex->rt6i->dst);
	kfree_rcu(rt6_ex, rcu);
	WARN_ON_ONCE(!bucket->depth);
	bucket->depth--;
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
	static siphash_aligned_key_t rt6_exception_key;
	struct {
		struct in6_addr dst;
		struct in6_addr src;
	} __aligned(SIPHASH_ALIGNMENT) combined = {
		.dst = *dst,
	};
	u64 val;

	net_get_random_once(&rt6_exception_key, sizeof(rt6_exception_key));

#ifdef CONFIG_IPV6_SUBTREES
	if (src)
		combined.src = *src;
#endif
	val = siphash(&combined, sizeof(combined), &rt6_exception_key);

	return hash_64(val, FIB6_EXCEPTION_BUCKET_SIZE_SHIFT);
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

static unsigned int fib6_mtu(const struct fib6_result *res)
{
	const struct fib6_nh *nh = res->nh;
	unsigned int mtu;

	if (res->f6i->fib6_pmtu) {
		mtu = res->f6i->fib6_pmtu;
	} else {
		struct net_device *dev = nh->fib_nh_dev;
		struct inet6_dev *idev;

		rcu_read_lock();
		idev = __in6_dev_get(dev);
		mtu = READ_ONCE(idev->cnf.mtu6);
		rcu_read_unlock();
	}

	mtu = min_t(unsigned int, mtu, IP6_MAX_MTU);

	return mtu - lwtunnel_headroom(nh->fib_nh_lws, mtu);
}

#define FIB6_EXCEPTION_BUCKET_FLUSHED  0x1UL

/* used when the flushed bit is not relevant, only access to the bucket
 * (ie., all bucket users except rt6_insert_exception);
 *
 * called under rcu lock; sometimes called with rt6_exception_lock held
 */
static
struct rt6_exception_bucket *fib6_nh_get_excptn_bucket(const struct fib6_nh *nh,
						       spinlock_t *lock)
{
	struct rt6_exception_bucket *bucket;

	if (lock)
		bucket = rcu_dereference_protected(nh->rt6i_exception_bucket,
						   lockdep_is_held(lock));
	else
		bucket = rcu_dereference(nh->rt6i_exception_bucket);

	/* remove bucket flushed bit if set */
	if (bucket) {
		unsigned long p = (unsigned long)bucket;

		p &= ~FIB6_EXCEPTION_BUCKET_FLUSHED;
		bucket = (struct rt6_exception_bucket *)p;
	}

	return bucket;
}

static bool fib6_nh_excptn_bucket_flushed(struct rt6_exception_bucket *bucket)
{
	unsigned long p = (unsigned long)bucket;

	return !!(p & FIB6_EXCEPTION_BUCKET_FLUSHED);
}

/* called with rt6_exception_lock held */
static void fib6_nh_excptn_bucket_set_flushed(struct fib6_nh *nh,
					      spinlock_t *lock)
{
	struct rt6_exception_bucket *bucket;
	unsigned long p;

	bucket = rcu_dereference_protected(nh->rt6i_exception_bucket,
					   lockdep_is_held(lock));

	p = (unsigned long)bucket;
	p |= FIB6_EXCEPTION_BUCKET_FLUSHED;
	bucket = (struct rt6_exception_bucket *)p;
	rcu_assign_pointer(nh->rt6i_exception_bucket, bucket);
}

static int rt6_insert_exception(struct rt6_info *nrt,
				const struct fib6_result *res)
{
	struct net *net = dev_net(nrt->dst.dev);
	struct rt6_exception_bucket *bucket;
	struct fib6_info *f6i = res->f6i;
	struct in6_addr *src_key = NULL;
	struct rt6_exception *rt6_ex;
	struct fib6_nh *nh = res->nh;
	int max_depth;
	int err = 0;

	spin_lock_bh(&rt6_exception_lock);

	bucket = rcu_dereference_protected(nh->rt6i_exception_bucket,
					  lockdep_is_held(&rt6_exception_lock));
	if (!bucket) {
		bucket = kcalloc(FIB6_EXCEPTION_BUCKET_SIZE, sizeof(*bucket),
				 GFP_ATOMIC);
		if (!bucket) {
			err = -ENOMEM;
			goto out;
		}
		rcu_assign_pointer(nh->rt6i_exception_bucket, bucket);
	} else if (fib6_nh_excptn_bucket_flushed(bucket)) {
		err = -EINVAL;
		goto out;
	}

#ifdef CONFIG_IPV6_SUBTREES
	/* fib6_src.plen != 0 indicates f6i is in subtree
	 * and exception table is indexed by a hash of
	 * both fib6_dst and fib6_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only fib6_dst.
	 */
	if (f6i->fib6_src.plen)
		src_key = &nrt->rt6i_src.addr;
#endif
	/* rt6_mtu_change() might lower mtu on f6i.
	 * Only insert this exception route if its mtu
	 * is less than f6i's mtu value.
	 */
	if (dst_metric_raw(&nrt->dst, RTAX_MTU) >= fib6_mtu(res)) {
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

	/* Randomize max depth to avoid some side channels attacks. */
	max_depth = FIB6_MAX_DEPTH + get_random_u32_below(FIB6_MAX_DEPTH);
	while (bucket->depth > max_depth)
		rt6_exception_remove_oldest(bucket);

out:
	spin_unlock_bh(&rt6_exception_lock);

	/* Update fn->fn_sernum to invalidate all cached dst */
	if (!err) {
		spin_lock_bh(&f6i->fib6_table->tb6_lock);
		fib6_update_sernum(net, f6i);
		fib6_add_gc_list(f6i);
		spin_unlock_bh(&f6i->fib6_table->tb6_lock);
		fib6_force_start_gc(net);
	}

	return err;
}

static void fib6_nh_flush_exceptions(struct fib6_nh *nh, struct fib6_info *from)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct hlist_node *tmp;
	int i;

	spin_lock_bh(&rt6_exception_lock);

	bucket = fib6_nh_get_excptn_bucket(nh, &rt6_exception_lock);
	if (!bucket)
		goto out;

	/* Prevent rt6_insert_exception() to recreate the bucket list */
	if (!from)
		fib6_nh_excptn_bucket_set_flushed(nh, &rt6_exception_lock);

	for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
		hlist_for_each_entry_safe(rt6_ex, tmp, &bucket->chain, hlist) {
			if (!from ||
			    rcu_access_pointer(rt6_ex->rt6i->from) == from)
				rt6_remove_exception(bucket, rt6_ex);
		}
		WARN_ON_ONCE(!from && bucket->depth);
		bucket++;
	}
out:
	spin_unlock_bh(&rt6_exception_lock);
}

static int rt6_nh_flush_exceptions(struct fib6_nh *nh, void *arg)
{
	struct fib6_info *f6i = arg;

	fib6_nh_flush_exceptions(nh, f6i);

	return 0;
}

void rt6_flush_exceptions(struct fib6_info *f6i)
{
	if (f6i->nh) {
		rcu_read_lock();
		nexthop_for_each_fib6_nh(f6i->nh, rt6_nh_flush_exceptions, f6i);
		rcu_read_unlock();
	} else {
		fib6_nh_flush_exceptions(f6i->fib6_nh, f6i);
	}
}

/* Find cached rt in the hash table inside passed in rt
 * Caller has to hold rcu_read_lock()
 */
static struct rt6_info *rt6_find_cached_rt(const struct fib6_result *res,
					   const struct in6_addr *daddr,
					   const struct in6_addr *saddr)
{
	const struct in6_addr *src_key = NULL;
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct rt6_info *ret = NULL;

#ifdef CONFIG_IPV6_SUBTREES
	/* fib6i_src.plen != 0 indicates f6i is in subtree
	 * and exception table is indexed by a hash of
	 * both fib6_dst and fib6_src.
	 * However, the src addr used to create the hash
	 * might not be exactly the passed in saddr which
	 * is a /128 addr from the flow.
	 * So we need to use f6i->fib6_src to redo lookup
	 * if the passed in saddr does not find anything.
	 * (See the logic in ip6_rt_cache_alloc() on how
	 * rt->rt6i_src is updated.)
	 */
	if (res->f6i->fib6_src.plen)
		src_key = saddr;
find_ex:
#endif
	bucket = fib6_nh_get_excptn_bucket(res->nh, NULL);
	rt6_ex = __rt6_find_exception_rcu(&bucket, daddr, src_key);

	if (rt6_ex && !rt6_check_expired(rt6_ex->rt6i))
		ret = rt6_ex->rt6i;

#ifdef CONFIG_IPV6_SUBTREES
	/* Use fib6_src as src_key and redo lookup */
	if (!ret && src_key && src_key != &res->f6i->fib6_src.addr) {
		src_key = &res->f6i->fib6_src.addr;
		goto find_ex;
	}
#endif

	return ret;
}

/* Remove the passed in cached rt from the hash table that contains it */
static int fib6_nh_remove_exception(const struct fib6_nh *nh, int plen,
				    const struct rt6_info *rt)
{
	const struct in6_addr *src_key = NULL;
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	int err;

	if (!rcu_access_pointer(nh->rt6i_exception_bucket))
		return -ENOENT;

	spin_lock_bh(&rt6_exception_lock);
	bucket = fib6_nh_get_excptn_bucket(nh, &rt6_exception_lock);

#ifdef CONFIG_IPV6_SUBTREES
	/* rt6i_src.plen != 0 indicates 'from' is in subtree
	 * and exception table is indexed by a hash of
	 * both rt6i_dst and rt6i_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only rt6i_dst.
	 */
	if (plen)
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

struct fib6_nh_excptn_arg {
	struct rt6_info	*rt;
	int		plen;
};

static int rt6_nh_remove_exception_rt(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_excptn_arg *arg = _arg;
	int err;

	err = fib6_nh_remove_exception(nh, arg->plen, arg->rt);
	if (err == 0)
		return 1;

	return 0;
}

static int rt6_remove_exception_rt(struct rt6_info *rt)
{
	struct fib6_info *from;

	from = rcu_dereference(rt->from);
	if (!from || !(rt->rt6i_flags & RTF_CACHE))
		return -EINVAL;

	if (from->nh) {
		struct fib6_nh_excptn_arg arg = {
			.rt = rt,
			.plen = from->fib6_src.plen
		};
		int rc;

		/* rc = 1 means an entry was found */
		rc = nexthop_for_each_fib6_nh(from->nh,
					      rt6_nh_remove_exception_rt,
					      &arg);
		return rc ? 0 : -ENOENT;
	}

	return fib6_nh_remove_exception(from->fib6_nh,
					from->fib6_src.plen, rt);
}

/* Find rt6_ex which contains the passed in rt cache and
 * refresh its stamp
 */
static void fib6_nh_update_exception(const struct fib6_nh *nh, int plen,
				     const struct rt6_info *rt)
{
	const struct in6_addr *src_key = NULL;
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;

	bucket = fib6_nh_get_excptn_bucket(nh, NULL);
#ifdef CONFIG_IPV6_SUBTREES
	/* rt6i_src.plen != 0 indicates 'from' is in subtree
	 * and exception table is indexed by a hash of
	 * both rt6i_dst and rt6i_src.
	 * Otherwise, the exception table is indexed by
	 * a hash of only rt6i_dst.
	 */
	if (plen)
		src_key = &rt->rt6i_src.addr;
#endif
	rt6_ex = __rt6_find_exception_rcu(&bucket, &rt->rt6i_dst.addr, src_key);
	if (rt6_ex)
		rt6_ex->stamp = jiffies;
}

struct fib6_nh_match_arg {
	const struct net_device *dev;
	const struct in6_addr	*gw;
	struct fib6_nh		*match;
};

/* determine if fib6_nh has given device and gateway */
static int fib6_nh_find_match(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_match_arg *arg = _arg;

	if (arg->dev != nh->fib_nh_dev ||
	    (arg->gw && !nh->fib_nh_gw_family) ||
	    (!arg->gw && nh->fib_nh_gw_family) ||
	    (arg->gw && !ipv6_addr_equal(arg->gw, &nh->fib_nh_gw6)))
		return 0;

	arg->match = nh;

	/* found a match, break the loop */
	return 1;
}

static void rt6_update_exception_stamp_rt(struct rt6_info *rt)
{
	struct fib6_info *from;
	struct fib6_nh *fib6_nh;

	rcu_read_lock();

	from = rcu_dereference(rt->from);
	if (!from || !(rt->rt6i_flags & RTF_CACHE))
		goto unlock;

	if (from->nh) {
		struct fib6_nh_match_arg arg = {
			.dev = rt->dst.dev,
			.gw = &rt->rt6i_gateway,
		};

		nexthop_for_each_fib6_nh(from->nh, fib6_nh_find_match, &arg);

		if (!arg.match)
			goto unlock;
		fib6_nh = arg.match;
	} else {
		fib6_nh = from->fib6_nh;
	}
	fib6_nh_update_exception(fib6_nh, from->fib6_src.plen, rt);
unlock:
	rcu_read_unlock();
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
				       const struct fib6_nh *nh, int mtu)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	int i;

	bucket = fib6_nh_get_excptn_bucket(nh, &rt6_exception_lock);
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

static void fib6_nh_exceptions_clean_tohost(const struct fib6_nh *nh,
					    const struct in6_addr *gateway)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct hlist_node *tmp;
	int i;

	if (!rcu_access_pointer(nh->rt6i_exception_bucket))
		return;

	spin_lock_bh(&rt6_exception_lock);
	bucket = fib6_nh_get_excptn_bucket(nh, &rt6_exception_lock);
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
		if (time_after_eq(now, READ_ONCE(rt->dst.lastuse) +
				       gc_args->timeout)) {
			pr_debug("aging clone %p\n", rt);
			rt6_remove_exception(bucket, rt6_ex);
			return;
		}
	} else if (time_after(jiffies, READ_ONCE(rt->dst.expires))) {
		pr_debug("purging expired route %p\n", rt);
		rt6_remove_exception(bucket, rt6_ex);
		return;
	}

	if (rt->rt6i_flags & RTF_GATEWAY) {
		struct neighbour *neigh;

		neigh = __ipv6_neigh_lookup_noref(rt->dst.dev, &rt->rt6i_gateway);

		if (!(neigh && (neigh->flags & NTF_ROUTER))) {
			pr_debug("purging route %p via non-router but gateway\n",
				 rt);
			rt6_remove_exception(bucket, rt6_ex);
			return;
		}
	}

	gc_args->more++;
}

static void fib6_nh_age_exceptions(const struct fib6_nh *nh,
				   struct fib6_gc_args *gc_args,
				   unsigned long now)
{
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	struct hlist_node *tmp;
	int i;

	if (!rcu_access_pointer(nh->rt6i_exception_bucket))
		return;

	rcu_read_lock_bh();
	spin_lock(&rt6_exception_lock);
	bucket = fib6_nh_get_excptn_bucket(nh, &rt6_exception_lock);
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

struct fib6_nh_age_excptn_arg {
	struct fib6_gc_args	*gc_args;
	unsigned long		now;
};

static int rt6_nh_age_exceptions(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_age_excptn_arg *arg = _arg;

	fib6_nh_age_exceptions(nh, arg->gc_args, arg->now);
	return 0;
}

void rt6_age_exceptions(struct fib6_info *f6i,
			struct fib6_gc_args *gc_args,
			unsigned long now)
{
	if (f6i->nh) {
		struct fib6_nh_age_excptn_arg arg = {
			.gc_args = gc_args,
			.now = now
		};

		nexthop_for_each_fib6_nh(f6i->nh, rt6_nh_age_exceptions,
					 &arg);
	} else {
		fib6_nh_age_exceptions(f6i->fib6_nh, gc_args, now);
	}
}

/* must be called with rcu lock held */
int fib6_table_lookup(struct net *net, struct fib6_table *table, int oif,
		      struct flowi6 *fl6, struct fib6_result *res, int strict)
{
	struct fib6_node *fn, *saved_fn;

	fn = fib6_node_lookup(&table->tb6_root, &fl6->daddr, &fl6->saddr);
	saved_fn = fn;

redo_rt6_select:
	rt6_select(net, fn, oif, res, strict);
	if (res->f6i == net->ipv6.fib6_null_entry) {
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

	trace_fib6_table_lookup(net, res, table, fl6);

	return 0;
}

struct rt6_info *ip6_pol_route(struct net *net, struct fib6_table *table,
			       int oif, struct flowi6 *fl6,
			       const struct sk_buff *skb, int flags)
{
	struct fib6_result res = {};
	struct rt6_info *rt = NULL;
	int strict = 0;

	WARN_ON_ONCE((flags & RT6_LOOKUP_F_DST_NOREF) &&
		     !rcu_read_lock_held());

	strict |= flags & RT6_LOOKUP_F_IFACE;
	strict |= flags & RT6_LOOKUP_F_IGNORE_LINKSTATE;
	if (READ_ONCE(net->ipv6.devconf_all->forwarding) == 0)
		strict |= RT6_LOOKUP_F_REACHABLE;

	rcu_read_lock();

	fib6_table_lookup(net, table, oif, fl6, &res, strict);
	if (res.f6i == net->ipv6.fib6_null_entry)
		goto out;

	fib6_select_path(net, &res, fl6, oif, false, skb, strict);

	/*Search through exception table */
	rt = rt6_find_cached_rt(&res, &fl6->daddr, &fl6->saddr);
	if (rt) {
		goto out;
	} else if (unlikely((fl6->flowi6_flags & FLOWI_FLAG_KNOWN_NH) &&
			    !res.nh->fib_nh_gw_family)) {
		/* Create a RTF_CACHE clone which will not be
		 * owned by the fib6 tree.  It is for the special case where
		 * the daddr in the skb during the neighbor look-up is different
		 * from the fl6->daddr used to look-up route here.
		 */
		rt = ip6_rt_cache_alloc(&res, &fl6->daddr, NULL);

		if (rt) {
			/* 1 refcnt is taken during ip6_rt_cache_alloc().
			 * As rt6_uncached_list_add() does not consume refcnt,
			 * this refcnt is always returned to the caller even
			 * if caller sets RT6_LOOKUP_F_DST_NOREF flag.
			 */
			rt6_uncached_list_add(rt);
			rcu_read_unlock();

			return rt;
		}
	} else {
		/* Get a percpu copy */
		local_bh_disable();
		rt = rt6_get_pcpu_route(&res);

		if (!rt)
			rt = rt6_make_pcpu_route(net, &res);

		local_bh_enable();
	}
out:
	if (!rt)
		rt = net->ipv6.ip6_null_entry;
	if (!(flags & RT6_LOOKUP_F_DST_NOREF))
		ip6_hold_safe(net, &rt);
	rcu_read_unlock();

	return rt;
}
EXPORT_SYMBOL_GPL(ip6_pol_route);

INDIRECT_CALLABLE_SCOPE struct rt6_info *ip6_pol_route_input(struct net *net,
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

	if (!icmpv6_is_err(icmph->icmp6_type))
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

static u32 rt6_multipath_custom_hash_outer(const struct net *net,
					   const struct sk_buff *skb,
					   bool *p_has_inner)
{
	u32 hash_fields = ip6_multipath_hash_fields(net);
	struct flow_keys keys, hash_keys;

	if (!(hash_fields & FIB_MULTIPATH_HASH_FIELD_OUTER_MASK))
		return 0;

	memset(&hash_keys, 0, sizeof(hash_keys));
	skb_flow_dissect_flow_keys(skb, &keys, FLOW_DISSECTOR_F_STOP_AT_ENCAP);

	hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_IP)
		hash_keys.addrs.v6addrs.src = keys.addrs.v6addrs.src;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_IP)
		hash_keys.addrs.v6addrs.dst = keys.addrs.v6addrs.dst;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_IP_PROTO)
		hash_keys.basic.ip_proto = keys.basic.ip_proto;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_FLOWLABEL)
		hash_keys.tags.flow_label = keys.tags.flow_label;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_PORT)
		hash_keys.ports.src = keys.ports.src;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_PORT)
		hash_keys.ports.dst = keys.ports.dst;

	*p_has_inner = !!(keys.control.flags & FLOW_DIS_ENCAPSULATION);
	return fib_multipath_hash_from_keys(net, &hash_keys);
}

static u32 rt6_multipath_custom_hash_inner(const struct net *net,
					   const struct sk_buff *skb,
					   bool has_inner)
{
	u32 hash_fields = ip6_multipath_hash_fields(net);
	struct flow_keys keys, hash_keys;

	/* We assume the packet carries an encapsulation, but if none was
	 * encountered during dissection of the outer flow, then there is no
	 * point in calling the flow dissector again.
	 */
	if (!has_inner)
		return 0;

	if (!(hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_MASK))
		return 0;

	memset(&hash_keys, 0, sizeof(hash_keys));
	skb_flow_dissect_flow_keys(skb, &keys, 0);

	if (!(keys.control.flags & FLOW_DIS_ENCAPSULATION))
		return 0;

	if (keys.control.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
		hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_SRC_IP)
			hash_keys.addrs.v4addrs.src = keys.addrs.v4addrs.src;
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_DST_IP)
			hash_keys.addrs.v4addrs.dst = keys.addrs.v4addrs.dst;
	} else if (keys.control.addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
		hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_SRC_IP)
			hash_keys.addrs.v6addrs.src = keys.addrs.v6addrs.src;
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_DST_IP)
			hash_keys.addrs.v6addrs.dst = keys.addrs.v6addrs.dst;
		if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_FLOWLABEL)
			hash_keys.tags.flow_label = keys.tags.flow_label;
	}

	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_IP_PROTO)
		hash_keys.basic.ip_proto = keys.basic.ip_proto;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_SRC_PORT)
		hash_keys.ports.src = keys.ports.src;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_INNER_DST_PORT)
		hash_keys.ports.dst = keys.ports.dst;

	return fib_multipath_hash_from_keys(net, &hash_keys);
}

static u32 rt6_multipath_custom_hash_skb(const struct net *net,
					 const struct sk_buff *skb)
{
	u32 mhash, mhash_inner;
	bool has_inner = true;

	mhash = rt6_multipath_custom_hash_outer(net, skb, &has_inner);
	mhash_inner = rt6_multipath_custom_hash_inner(net, skb, has_inner);

	return jhash_2words(mhash, mhash_inner, 0);
}

static u32 rt6_multipath_custom_hash_fl6(const struct net *net,
					 const struct flowi6 *fl6)
{
	u32 hash_fields = ip6_multipath_hash_fields(net);
	struct flow_keys hash_keys;

	if (!(hash_fields & FIB_MULTIPATH_HASH_FIELD_OUTER_MASK))
		return 0;

	memset(&hash_keys, 0, sizeof(hash_keys));
	hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_IP)
		hash_keys.addrs.v6addrs.src = fl6->saddr;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_IP)
		hash_keys.addrs.v6addrs.dst = fl6->daddr;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_IP_PROTO)
		hash_keys.basic.ip_proto = fl6->flowi6_proto;
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_FLOWLABEL)
		hash_keys.tags.flow_label = (__force u32)flowi6_get_flowlabel(fl6);
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_SRC_PORT) {
		if (fl6->flowi6_flags & FLOWI_FLAG_ANY_SPORT)
			hash_keys.ports.src = (__force __be16)get_random_u16();
		else
			hash_keys.ports.src = fl6->fl6_sport;
	}
	if (hash_fields & FIB_MULTIPATH_HASH_FIELD_DST_PORT)
		hash_keys.ports.dst = fl6->fl6_dport;

	return fib_multipath_hash_from_keys(net, &hash_keys);
}

/* if skb is set it will be used and fl6 can be NULL */
u32 rt6_multipath_hash(const struct net *net, const struct flowi6 *fl6,
		       const struct sk_buff *skb, struct flow_keys *flkeys)
{
	struct flow_keys hash_keys;
	u32 mhash = 0;

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
		mhash = fib_multipath_hash_from_keys(net, &hash_keys);
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
			if (fl6->flowi6_flags & FLOWI_FLAG_ANY_SPORT)
				hash_keys.ports.src = (__force __be16)get_random_u16();
			else
				hash_keys.ports.src = fl6->fl6_sport;
			hash_keys.ports.dst = fl6->fl6_dport;
			hash_keys.basic.ip_proto = fl6->flowi6_proto;
		}
		mhash = fib_multipath_hash_from_keys(net, &hash_keys);
		break;
	case 2:
		memset(&hash_keys, 0, sizeof(hash_keys));
		hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
		if (skb) {
			struct flow_keys keys;

			if (!flkeys) {
				skb_flow_dissect_flow_keys(skb, &keys, 0);
				flkeys = &keys;
			}

			/* Inner can be v4 or v6 */
			if (flkeys->control.addr_type == FLOW_DISSECTOR_KEY_IPV4_ADDRS) {
				hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV4_ADDRS;
				hash_keys.addrs.v4addrs.src = flkeys->addrs.v4addrs.src;
				hash_keys.addrs.v4addrs.dst = flkeys->addrs.v4addrs.dst;
			} else if (flkeys->control.addr_type == FLOW_DISSECTOR_KEY_IPV6_ADDRS) {
				hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
				hash_keys.addrs.v6addrs.src = flkeys->addrs.v6addrs.src;
				hash_keys.addrs.v6addrs.dst = flkeys->addrs.v6addrs.dst;
				hash_keys.tags.flow_label = flkeys->tags.flow_label;
				hash_keys.basic.ip_proto = flkeys->basic.ip_proto;
			} else {
				/* Same as case 0 */
				hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
				ip6_multipath_l3_keys(skb, &hash_keys, flkeys);
			}
		} else {
			/* Same as case 0 */
			hash_keys.control.addr_type = FLOW_DISSECTOR_KEY_IPV6_ADDRS;
			hash_keys.addrs.v6addrs.src = fl6->saddr;
			hash_keys.addrs.v6addrs.dst = fl6->daddr;
			hash_keys.tags.flow_label = (__force u32)flowi6_get_flowlabel(fl6);
			hash_keys.basic.ip_proto = fl6->flowi6_proto;
		}
		mhash = fib_multipath_hash_from_keys(net, &hash_keys);
		break;
	case 3:
		if (skb)
			mhash = rt6_multipath_custom_hash_skb(net, skb);
		else
			mhash = rt6_multipath_custom_hash_fl6(net, fl6);
		break;
	}

	return mhash >> 1;
}

/* Called with rcu held */
void ip6_route_input(struct sk_buff *skb)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	struct net *net = dev_net(skb->dev);
	int flags = RT6_LOOKUP_F_HAS_SADDR | RT6_LOOKUP_F_DST_NOREF;
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
	skb_dst_set_noref(skb, ip6_route_input_lookup(net, skb->dev,
						      &fl6, skb, flags));
}

INDIRECT_CALLABLE_SCOPE struct rt6_info *ip6_pol_route_output(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     const struct sk_buff *skb,
					     int flags)
{
	return ip6_pol_route(net, table, fl6->flowi6_oif, fl6, skb, flags);
}

static struct dst_entry *ip6_route_output_flags_noref(struct net *net,
						      const struct sock *sk,
						      struct flowi6 *fl6,
						      int flags)
{
	bool any_src;

	if (ipv6_addr_type(&fl6->daddr) &
	    (IPV6_ADDR_MULTICAST | IPV6_ADDR_LINKLOCAL)) {
		struct dst_entry *dst;

		/* This function does not take refcnt on the dst */
		dst = l3mdev_link_scope_lookup(net, fl6);
		if (dst)
			return dst;
	}

	fl6->flowi6_iif = LOOPBACK_IFINDEX;

	flags |= RT6_LOOKUP_F_DST_NOREF;
	any_src = ipv6_addr_any(&fl6->saddr);
	if ((sk && sk->sk_bound_dev_if) || rt6_need_strict(&fl6->daddr) ||
	    (fl6->flowi6_oif && any_src))
		flags |= RT6_LOOKUP_F_IFACE;

	if (!any_src)
		flags |= RT6_LOOKUP_F_HAS_SADDR;
	else if (sk)
		flags |= rt6_srcprefs2flags(READ_ONCE(inet6_sk(sk)->srcprefs));

	return fib6_rule_lookup(net, fl6, NULL, flags, ip6_pol_route_output);
}

struct dst_entry *ip6_route_output_flags(struct net *net,
					 const struct sock *sk,
					 struct flowi6 *fl6,
					 int flags)
{
	struct dst_entry *dst;
	struct rt6_info *rt6;

	rcu_read_lock();
	dst = ip6_route_output_flags_noref(net, sk, fl6, flags);
	rt6 = dst_rt6_info(dst);
	/* For dst cached in uncached_list, refcnt is already taken. */
	if (list_empty(&rt6->dst.rt_uncached) && !dst_hold_safe(dst)) {
		dst = &net->ipv6.ip6_null_entry->dst;
		dst_hold(dst);
	}
	rcu_read_unlock();

	return dst;
}
EXPORT_SYMBOL_GPL(ip6_route_output_flags);

struct dst_entry *ip6_blackhole_route(struct net *net, struct dst_entry *dst_orig)
{
	struct rt6_info *rt, *ort = dst_rt6_info(dst_orig);
	struct net_device *loopback_dev = net->loopback_dev;
	struct dst_entry *new = NULL;

	rt = dst_alloc(&ip6_dst_blackhole_ops, loopback_dev,
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

	if (!from || !fib6_get_cookie_safe(from, &rt_cookie) ||
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
	    READ_ONCE(rt->dst.obsolete) == DST_OBSOLETE_FORCE_CHK &&
	    fib6_check(from, cookie))
		return &rt->dst;
	return NULL;
}

INDIRECT_CALLABLE_SCOPE struct dst_entry *ip6_dst_check(struct dst_entry *dst,
							u32 cookie)
{
	struct dst_entry *dst_ret;
	struct fib6_info *from;
	struct rt6_info *rt;

	rt = dst_rt6_info(dst);

	if (rt->sernum)
		return rt6_is_valid(rt) ? dst : NULL;

	rcu_read_lock();

	/* All IPV6 dsts are created with ->obsolete set to the value
	 * DST_OBSOLETE_FORCE_CHK which forces validation calls down
	 * into this function always.
	 */

	from = rcu_dereference(rt->from);

	if (from && (rt->rt6i_flags & RTF_PCPU ||
	    unlikely(!list_empty(&rt->dst.rt_uncached))))
		dst_ret = rt6_dst_from_check(rt, from, cookie);
	else
		dst_ret = rt6_check(rt, from, cookie);

	rcu_read_unlock();

	return dst_ret;
}
EXPORT_INDIRECT_CALLABLE(ip6_dst_check);

static void ip6_negative_advice(struct sock *sk,
				struct dst_entry *dst)
{
	struct rt6_info *rt = dst_rt6_info(dst);

	if (rt->rt6i_flags & RTF_CACHE) {
		rcu_read_lock();
		if (rt6_check_expired(rt)) {
			/* rt/dst can not be destroyed yet,
			 * because of rcu_read_lock()
			 */
			sk_dst_reset(sk);
			rt6_remove_exception_rt(rt);
		}
		rcu_read_unlock();
		return;
	}
	sk_dst_reset(sk);
}

static void ip6_link_failure(struct sk_buff *skb)
{
	struct rt6_info *rt;

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_ADDR_UNREACH, 0);

	rt = dst_rt6_info(skb_dst(skb));
	if (rt) {
		rcu_read_lock();
		if (rt->rt6i_flags & RTF_CACHE) {
			rt6_remove_exception_rt(rt);
		} else {
			struct fib6_info *from;
			struct fib6_node *fn;

			from = rcu_dereference(rt->from);
			if (from) {
				fn = rcu_dereference(from->fib6_node);
				if (fn && (rt->rt6i_flags & RTF_DEFAULT))
					WRITE_ONCE(fn->fn_sernum, -1);
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
			WRITE_ONCE(rt0->dst.expires, from->expires);
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
	return !(rt->rt6i_flags & RTF_CACHE) &&
		(rt->rt6i_flags & RTF_PCPU || rcu_access_pointer(rt->from));
}

static void __ip6_rt_update_pmtu(struct dst_entry *dst, const struct sock *sk,
				 const struct ipv6hdr *iph, u32 mtu,
				 bool confirm_neigh)
{
	const struct in6_addr *daddr, *saddr;
	struct rt6_info *rt6 = dst_rt6_info(dst);

	/* Note: do *NOT* check dst_metric_locked(dst, RTAX_MTU)
	 * IPv6 pmtu discovery isn't optional, so 'mtu lock' cannot disable it.
	 * [see also comment in rt6_mtu_change_route()]
	 */

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

	if (confirm_neigh)
		dst_confirm_neigh(dst, daddr);

	if (mtu < IPV6_MIN_MTU)
		return;
	if (mtu >= dst_mtu(dst))
		return;

	if (!rt6_cache_allowed_for_pmtu(rt6)) {
		rt6_do_update_pmtu(rt6, mtu);
		/* update rt6_ex->stamp for cache */
		if (rt6->rt6i_flags & RTF_CACHE)
			rt6_update_exception_stamp_rt(rt6);
	} else if (daddr) {
		struct fib6_result res = {};
		struct rt6_info *nrt6;

		rcu_read_lock();
		res.f6i = rcu_dereference(rt6->from);
		if (!res.f6i)
			goto out_unlock;

		res.fib6_flags = res.f6i->fib6_flags;
		res.fib6_type = res.f6i->fib6_type;

		if (res.f6i->nh) {
			struct fib6_nh_match_arg arg = {
				.dev = dst_dev(dst),
				.gw = &rt6->rt6i_gateway,
			};

			nexthop_for_each_fib6_nh(res.f6i->nh,
						 fib6_nh_find_match, &arg);

			/* fib6_info uses a nexthop that does not have fib6_nh
			 * using the dst->dev + gw. Should be impossible.
			 */
			if (!arg.match)
				goto out_unlock;

			res.nh = arg.match;
		} else {
			res.nh = res.f6i->fib6_nh;
		}

		nrt6 = ip6_rt_cache_alloc(&res, daddr, saddr);
		if (nrt6) {
			rt6_do_update_pmtu(nrt6, mtu);
			if (rt6_insert_exception(nrt6, &res))
				dst_release_immediate(&nrt6->dst);
		}
out_unlock:
		rcu_read_unlock();
	}
}

static void ip6_rt_update_pmtu(struct dst_entry *dst, struct sock *sk,
			       struct sk_buff *skb, u32 mtu,
			       bool confirm_neigh)
{
	__ip6_rt_update_pmtu(dst, sk, skb ? ipv6_hdr(skb) : NULL, mtu,
			     confirm_neigh);
}

void ip6_update_pmtu(struct sk_buff *skb, struct net *net, __be32 mtu,
		     int oif, u32 mark, kuid_t uid)
{
	const struct ipv6hdr *iph = (struct ipv6hdr *) skb->data;
	struct dst_entry *dst;
	struct flowi6 fl6 = {
		.flowi6_oif = oif,
		.flowi6_mark = mark ? mark : IP6_REPLY_MARK(net, skb->mark),
		.daddr = iph->daddr,
		.saddr = iph->saddr,
		.flowlabel = ip6_flowinfo(iph),
		.flowi6_uid = uid,
	};

	dst = ip6_route_output(net, NULL, &fl6);
	if (!dst->error)
		__ip6_rt_update_pmtu(dst, NULL, iph, ntohl(mtu), true);
	dst_release(dst);
}
EXPORT_SYMBOL_GPL(ip6_update_pmtu);

void ip6_sk_update_pmtu(struct sk_buff *skb, struct sock *sk, __be32 mtu)
{
	int oif = sk->sk_bound_dev_if;
	struct dst_entry *dst;

	if (!oif && skb->dev)
		oif = l3mdev_master_ifindex(skb->dev);

	ip6_update_pmtu(skb, sock_net(sk), mtu, oif, READ_ONCE(sk->sk_mark),
			sk_uid(sk));

	dst = __sk_dst_get(sk);
	if (!dst || !READ_ONCE(dst->obsolete) ||
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

static bool ip6_redirect_nh_match(const struct fib6_result *res,
				  struct flowi6 *fl6,
				  const struct in6_addr *gw,
				  struct rt6_info **ret)
{
	const struct fib6_nh *nh = res->nh;

	if (nh->fib_nh_flags & RTNH_F_DEAD || !nh->fib_nh_gw_family ||
	    fl6->flowi6_oif != nh->fib_nh_dev->ifindex)
		return false;

	/* rt_cache's gateway might be different from its 'parent'
	 * in the case of an ip redirect.
	 * So we keep searching in the exception table if the gateway
	 * is different.
	 */
	if (!ipv6_addr_equal(gw, &nh->fib_nh_gw6)) {
		struct rt6_info *rt_cache;

		rt_cache = rt6_find_cached_rt(res, &fl6->daddr, &fl6->saddr);
		if (rt_cache &&
		    ipv6_addr_equal(gw, &rt_cache->rt6i_gateway)) {
			*ret = rt_cache;
			return true;
		}
		return false;
	}
	return true;
}

struct fib6_nh_rd_arg {
	struct fib6_result	*res;
	struct flowi6		*fl6;
	const struct in6_addr	*gw;
	struct rt6_info		**ret;
};

static int fib6_nh_redirect_match(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_rd_arg *arg = _arg;

	arg->res->nh = nh;
	return ip6_redirect_nh_match(arg->res, arg->fl6, arg->gw, arg->ret);
}

/* Handle redirects */
struct ip6rd_flowi {
	struct flowi6 fl6;
	struct in6_addr gateway;
};

INDIRECT_CALLABLE_SCOPE struct rt6_info *__ip6_route_redirect(struct net *net,
					     struct fib6_table *table,
					     struct flowi6 *fl6,
					     const struct sk_buff *skb,
					     int flags)
{
	struct ip6rd_flowi *rdfl = (struct ip6rd_flowi *)fl6;
	struct rt6_info *ret = NULL;
	struct fib6_result res = {};
	struct fib6_nh_rd_arg arg = {
		.res = &res,
		.fl6 = fl6,
		.gw  = &rdfl->gateway,
		.ret = &ret
	};
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
		res.f6i = rt;
		if (fib6_check_expired(rt))
			continue;
		if (rt->fib6_flags & RTF_REJECT)
			break;
		if (unlikely(rt->nh)) {
			if (nexthop_is_blackhole(rt->nh))
				continue;
			/* on match, res->nh is filled in and potentially ret */
			if (nexthop_for_each_fib6_nh(rt->nh,
						     fib6_nh_redirect_match,
						     &arg))
				goto out;
		} else {
			res.nh = rt->fib6_nh;
			if (ip6_redirect_nh_match(&res, fl6, &rdfl->gateway,
						  &ret))
				goto out;
		}
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

	res.f6i = rt;
	res.nh = rt->fib6_nh;
out:
	if (ret) {
		ip6_hold_safe(net, &ret);
	} else {
		res.fib6_flags = res.f6i->fib6_flags;
		res.fib6_type = res.f6i->fib6_type;
		ret = ip6_create_rt_rcu(&res);
	}

	rcu_read_unlock();

	trace_fib6_table_lookup(net, &res, table, fl6);
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
	struct flowi6 fl6 = {
		.flowi6_iif = LOOPBACK_IFINDEX,
		.flowi6_oif = oif,
		.flowi6_mark = mark,
		.daddr = iph->daddr,
		.saddr = iph->saddr,
		.flowlabel = ip6_flowinfo(iph),
		.flowi6_uid = uid,
	};

	dst = ip6_route_redirect(net, &fl6, skb, &ipv6_hdr(skb)->saddr);
	rt6_do_redirect(dst, NULL, skb);
	dst_release(dst);
}
EXPORT_SYMBOL_GPL(ip6_redirect);

void ip6_redirect_no_header(struct sk_buff *skb, struct net *net, int oif)
{
	const struct ipv6hdr *iph = ipv6_hdr(skb);
	const struct rd_msg *msg = (struct rd_msg *)icmp6_hdr(skb);
	struct dst_entry *dst;
	struct flowi6 fl6 = {
		.flowi6_iif = LOOPBACK_IFINDEX,
		.flowi6_oif = oif,
		.daddr = msg->dest,
		.saddr = iph->daddr,
		.flowi6_uid = sock_net_uid(net, NULL),
	};

	dst = ip6_route_redirect(net, &fl6, skb, &iph->saddr);
	rt6_do_redirect(dst, NULL, skb);
	dst_release(dst);
}

void ip6_sk_redirect(struct sk_buff *skb, struct sock *sk)
{
	ip6_redirect(skb, sock_net(sk), sk->sk_bound_dev_if,
		     READ_ONCE(sk->sk_mark), sk_uid(sk));
}
EXPORT_SYMBOL_GPL(ip6_sk_redirect);

static unsigned int ip6_default_advmss(const struct dst_entry *dst)
{
	struct net_device *dev = dst_dev(dst);
	unsigned int mtu = dst_mtu(dst);
	struct net *net;

	mtu -= sizeof(struct ipv6hdr) + sizeof(struct tcphdr);

	rcu_read_lock();

	net = dev_net_rcu(dev);
	if (mtu < net->ipv6.sysctl.ip6_rt_min_advmss)
		mtu = net->ipv6.sysctl.ip6_rt_min_advmss;

	rcu_read_unlock();

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

INDIRECT_CALLABLE_SCOPE unsigned int ip6_mtu(const struct dst_entry *dst)
{
	return ip6_dst_mtu_maybe_forward(dst, false);
}
EXPORT_INDIRECT_CALLABLE(ip6_mtu);

/* MTU selection:
 * 1. mtu on route is locked - use it
 * 2. mtu from nexthop exception
 * 3. mtu from egress device
 *
 * based on ip6_dst_mtu_forward and exception logic of
 * rt6_find_cached_rt; called with rcu_read_lock
 */
u32 ip6_mtu_from_fib6(const struct fib6_result *res,
		      const struct in6_addr *daddr,
		      const struct in6_addr *saddr)
{
	const struct fib6_nh *nh = res->nh;
	struct fib6_info *f6i = res->f6i;
	struct inet6_dev *idev;
	struct rt6_info *rt;
	u32 mtu = 0;

	if (unlikely(fib6_metric_locked(f6i, RTAX_MTU))) {
		mtu = f6i->fib6_pmtu;
		if (mtu)
			goto out;
	}

	rt = rt6_find_cached_rt(res, daddr, saddr);
	if (unlikely(rt)) {
		mtu = dst_metric_raw(&rt->dst, RTAX_MTU);
	} else {
		struct net_device *dev = nh->fib_nh_dev;

		mtu = IPV6_MIN_MTU;
		idev = __in6_dev_get(dev);
		if (idev)
			mtu = max_t(u32, mtu, READ_ONCE(idev->cnf.mtu6));
	}

	mtu = min_t(unsigned int, mtu, IP6_MAX_MTU);
out:
	return mtu - lwtunnel_headroom(nh->fib_nh_lws, mtu);
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

	dst = xfrm_lookup(net, &rt->dst, flowi6_to_flowi(fl6), NULL, 0);

out:
	return dst;
}

static void ip6_dst_gc(struct dst_ops *ops)
{
	struct net *net = container_of(ops, struct net, ipv6.ip6_dst_ops);
	int rt_min_interval = net->ipv6.sysctl.ip6_rt_gc_min_interval;
	int rt_elasticity = net->ipv6.sysctl.ip6_rt_gc_elasticity;
	int rt_gc_timeout = net->ipv6.sysctl.ip6_rt_gc_timeout;
	unsigned long rt_last_gc = net->ipv6.ip6_rt_last_gc;
	unsigned int val;
	int entries;

	if (time_after(rt_last_gc + rt_min_interval, jiffies))
		goto out;

	fib6_run_gc(atomic_inc_return(&net->ipv6.ip6_rt_gc_expire), net, true);
	entries = dst_entries_get_slow(ops);
	if (entries < ops->gc_thresh)
		atomic_set(&net->ipv6.ip6_rt_gc_expire, rt_gc_timeout >> 1);
out:
	val = atomic_read(&net->ipv6.ip6_rt_gc_expire);
	atomic_set(&net->ipv6.ip6_rt_gc_expire, val - (val >> rt_elasticity));
}

static int ip6_nh_lookup_table(struct net *net, struct fib6_config *cfg,
			       const struct in6_addr *gw_addr, u32 tbid,
			       int flags, struct fib6_result *res)
{
	struct flowi6 fl6 = {
		.flowi6_oif = cfg->fc_ifindex,
		.daddr = *gw_addr,
		.saddr = cfg->fc_prefsrc,
	};
	struct fib6_table *table;
	int err;

	table = fib6_get_table(net, tbid);
	if (!table)
		return -EINVAL;

	if (!ipv6_addr_any(&cfg->fc_prefsrc))
		flags |= RT6_LOOKUP_F_HAS_SADDR;

	flags |= RT6_LOOKUP_F_IGNORE_LINKSTATE;

	err = fib6_table_lookup(net, table, cfg->fc_ifindex, &fl6, res, flags);
	if (!err && res->f6i != net->ipv6.fib6_null_entry)
		fib6_select_path(net, res, &fl6, cfg->fc_ifindex,
				 cfg->fc_ifindex != 0, NULL, flags);

	return err;
}

static int ip6_route_check_nh_onlink(struct net *net,
				     struct fib6_config *cfg,
				     const struct net_device *dev,
				     struct netlink_ext_ack *extack)
{
	u32 tbid = l3mdev_fib_table_rcu(dev) ? : RT_TABLE_MAIN;
	const struct in6_addr *gw_addr = &cfg->fc_gateway;
	struct fib6_result res = {};
	int err;

	err = ip6_nh_lookup_table(net, cfg, gw_addr, tbid, 0, &res);
	if (!err && !(res.fib6_flags & RTF_REJECT) &&
	    /* ignore match if it is the default route */
	    !ipv6_addr_any(&res.f6i->fib6_dst.addr) &&
	    (res.fib6_type != RTN_UNICAST || dev != res.nh->fib_nh_dev)) {
		NL_SET_ERR_MSG(extack,
			       "Nexthop has invalid gateway or device mismatch");
		err = -EINVAL;
	}

	return err;
}

static int ip6_route_check_nh(struct net *net,
			      struct fib6_config *cfg,
			      struct net_device **_dev,
			      netdevice_tracker *dev_tracker,
			      struct inet6_dev **idev)
{
	const struct in6_addr *gw_addr = &cfg->fc_gateway;
	struct net_device *dev = _dev ? *_dev : NULL;
	int flags = RT6_LOOKUP_F_IFACE;
	struct fib6_result res = {};
	int err = -EHOSTUNREACH;

	if (cfg->fc_table) {
		err = ip6_nh_lookup_table(net, cfg, gw_addr,
					  cfg->fc_table, flags, &res);
		/* gw_addr can not require a gateway or resolve to a reject
		 * route. If a device is given, it must match the result.
		 */
		if (err || res.fib6_flags & RTF_REJECT ||
		    res.nh->fib_nh_gw_family ||
		    (dev && dev != res.nh->fib_nh_dev))
			err = -EHOSTUNREACH;
	}

	if (err < 0) {
		struct flowi6 fl6 = {
			.flowi6_oif = cfg->fc_ifindex,
			.daddr = *gw_addr,
		};

		err = fib6_lookup(net, cfg->fc_ifindex, &fl6, &res, flags);
		if (err || res.fib6_flags & RTF_REJECT ||
		    res.nh->fib_nh_gw_family)
			err = -EHOSTUNREACH;

		if (err)
			return err;

		fib6_select_path(net, &res, &fl6, cfg->fc_ifindex,
				 cfg->fc_ifindex != 0, NULL, flags);
	}

	err = 0;
	if (dev) {
		if (dev != res.nh->fib_nh_dev)
			err = -EHOSTUNREACH;
	} else {
		*_dev = dev = res.nh->fib_nh_dev;
		netdev_hold(dev, dev_tracker, GFP_ATOMIC);
		*idev = in6_dev_get(dev);
	}

	return err;
}

static int ip6_validate_gw(struct net *net, struct fib6_config *cfg,
			   struct net_device **_dev,
			   netdevice_tracker *dev_tracker,
			   struct inet6_dev **idev,
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

		rcu_read_lock();

		if (cfg->fc_flags & RTNH_F_ONLINK)
			err = ip6_route_check_nh_onlink(net, cfg, dev, extack);
		else
			err = ip6_route_check_nh(net, cfg, _dev, dev_tracker,
						 idev);

		rcu_read_unlock();

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

static bool fib6_is_reject(u32 flags, struct net_device *dev, int addr_type)
{
	if ((flags & RTF_REJECT) ||
	    (dev && (dev->flags & IFF_LOOPBACK) &&
	     !(addr_type & IPV6_ADDR_LOOPBACK) &&
	     !(flags & (RTF_ANYCAST | RTF_LOCAL))))
		return true;

	return false;
}

int fib6_nh_init(struct net *net, struct fib6_nh *fib6_nh,
		 struct fib6_config *cfg, gfp_t gfp_flags,
		 struct netlink_ext_ack *extack)
{
	netdevice_tracker *dev_tracker = &fib6_nh->fib_nh_dev_tracker;
	struct net_device *dev = NULL;
	struct inet6_dev *idev = NULL;
	int addr_type;
	int err;

	fib6_nh->fib_nh_family = AF_INET6;
#ifdef CONFIG_IPV6_ROUTER_PREF
	fib6_nh->last_probe = jiffies;
#endif
	if (cfg->fc_is_fdb) {
		fib6_nh->fib_nh_gw6 = cfg->fc_gateway;
		fib6_nh->fib_nh_gw_family = AF_INET6;
		return 0;
	}

	err = -ENODEV;
	if (cfg->fc_ifindex) {
		dev = netdev_get_by_index(net, cfg->fc_ifindex,
					  dev_tracker, gfp_flags);
		if (!dev)
			goto out;
		idev = in6_dev_get(dev);
		if (!idev)
			goto out;
	}

	if (cfg->fc_flags & RTNH_F_ONLINK) {
		if (!dev) {
			NL_SET_ERR_MSG(extack,
				       "Nexthop device required for onlink");
			goto out;
		}

		if (!(dev->flags & IFF_UP)) {
			NL_SET_ERR_MSG(extack, "Nexthop device is not up");
			err = -ENETDOWN;
			goto out;
		}

		fib6_nh->fib_nh_flags |= RTNH_F_ONLINK;
	}

	fib6_nh->fib_nh_weight = 1;

	/* We cannot add true routes via loopback here,
	 * they would result in kernel looping; promote them to reject routes
	 */
	addr_type = ipv6_addr_type(&cfg->fc_dst);
	if (fib6_is_reject(cfg->fc_flags, dev, addr_type)) {
		/* hold loopback dev/idev if we haven't done so. */
		if (dev != net->loopback_dev) {
			if (dev) {
				netdev_put(dev, dev_tracker);
				in6_dev_put(idev);
			}
			dev = net->loopback_dev;
			netdev_hold(dev, dev_tracker, gfp_flags);
			idev = in6_dev_get(dev);
			if (!idev) {
				err = -ENODEV;
				goto out;
			}
		}
		goto pcpu_alloc;
	}

	if (cfg->fc_flags & RTF_GATEWAY) {
		err = ip6_validate_gw(net, cfg, &dev, dev_tracker,
				      &idev, extack);
		if (err)
			goto out;

		fib6_nh->fib_nh_gw6 = cfg->fc_gateway;
		fib6_nh->fib_nh_gw_family = AF_INET6;
	}

	err = -ENODEV;
	if (!dev)
		goto out;

	if (!idev || idev->cnf.disable_ipv6) {
		NL_SET_ERR_MSG(extack, "IPv6 is disabled on nexthop device");
		err = -EACCES;
		goto out;
	}

	if (!(dev->flags & IFF_UP) && !cfg->fc_ignore_dev_down) {
		NL_SET_ERR_MSG(extack, "Nexthop device is not up");
		err = -ENETDOWN;
		goto out;
	}

	if (!(cfg->fc_flags & (RTF_LOCAL | RTF_ANYCAST)) &&
	    !netif_carrier_ok(dev))
		fib6_nh->fib_nh_flags |= RTNH_F_LINKDOWN;

	err = fib_nh_common_init(net, &fib6_nh->nh_common, cfg->fc_encap,
				 cfg->fc_encap_type, cfg, gfp_flags, extack);
	if (err)
		goto out;

pcpu_alloc:
	fib6_nh->rt6i_pcpu = alloc_percpu_gfp(struct rt6_info *, gfp_flags);
	if (!fib6_nh->rt6i_pcpu) {
		err = -ENOMEM;
		goto out;
	}

	fib6_nh->fib_nh_dev = dev;
	fib6_nh->fib_nh_oif = dev->ifindex;
	err = 0;
out:
	if (idev)
		in6_dev_put(idev);

	if (err) {
		fib_nh_common_release(&fib6_nh->nh_common);
		fib6_nh->nh_common.nhc_pcpu_rth_output = NULL;
		fib6_nh->fib_nh_lws = NULL;
		netdev_put(dev, dev_tracker);
	}

	return err;
}

void fib6_nh_release(struct fib6_nh *fib6_nh)
{
	struct rt6_exception_bucket *bucket;

	rcu_read_lock();

	fib6_nh_flush_exceptions(fib6_nh, NULL);
	bucket = fib6_nh_get_excptn_bucket(fib6_nh, NULL);
	if (bucket) {
		rcu_assign_pointer(fib6_nh->rt6i_exception_bucket, NULL);
		kfree(bucket);
	}

	rcu_read_unlock();

	fib6_nh_release_dsts(fib6_nh);
	free_percpu(fib6_nh->rt6i_pcpu);

	fib_nh_common_release(&fib6_nh->nh_common);
}

void fib6_nh_release_dsts(struct fib6_nh *fib6_nh)
{
	int cpu;

	if (!fib6_nh->rt6i_pcpu)
		return;

	for_each_possible_cpu(cpu) {
		struct rt6_info *pcpu_rt, **ppcpu_rt;

		ppcpu_rt = per_cpu_ptr(fib6_nh->rt6i_pcpu, cpu);
		pcpu_rt = xchg(ppcpu_rt, NULL);
		if (pcpu_rt) {
			dst_dev_put(&pcpu_rt->dst);
			dst_release(&pcpu_rt->dst);
		}
	}
}

static int fib6_config_validate(struct fib6_config *cfg,
				struct netlink_ext_ack *extack)
{
	/* RTF_PCPU is an internal flag; can not be set by userspace */
	if (cfg->fc_flags & RTF_PCPU) {
		NL_SET_ERR_MSG(extack, "Userspace can not set RTF_PCPU");
		goto errout;
	}

	/* RTF_CACHE is an internal flag; can not be set by userspace */
	if (cfg->fc_flags & RTF_CACHE) {
		NL_SET_ERR_MSG(extack, "Userspace can not set RTF_CACHE");
		goto errout;
	}

	if (cfg->fc_type > RTN_MAX) {
		NL_SET_ERR_MSG(extack, "Invalid route type");
		goto errout;
	}

	if (cfg->fc_dst_len > 128) {
		NL_SET_ERR_MSG(extack, "Invalid prefix length");
		goto errout;
	}

#ifdef CONFIG_IPV6_SUBTREES
	if (cfg->fc_src_len > 128) {
		NL_SET_ERR_MSG(extack, "Invalid source address length");
		goto errout;
	}

	if (cfg->fc_nh_id && cfg->fc_src_len) {
		NL_SET_ERR_MSG(extack, "Nexthops can not be used with source routing");
		goto errout;
	}
#else
	if (cfg->fc_src_len) {
		NL_SET_ERR_MSG(extack,
			       "Specifying source address requires IPV6_SUBTREES to be enabled");
		goto errout;
	}
#endif
	return 0;
errout:
	return -EINVAL;
}

static struct fib6_info *ip6_route_info_create(struct fib6_config *cfg,
					       gfp_t gfp_flags,
					       struct netlink_ext_ack *extack)
{
	struct net *net = cfg->fc_nlinfo.nl_net;
	struct fib6_table *table;
	struct fib6_info *rt;
	int err;

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
	if (!table) {
		err = -ENOBUFS;
		goto err;
	}

	rt = fib6_info_alloc(gfp_flags, !cfg->fc_nh_id);
	if (!rt) {
		err = -ENOMEM;
		goto err;
	}

	rt->fib6_metrics = ip_fib_metrics_init(cfg->fc_mx, cfg->fc_mx_len,
					       extack);
	if (IS_ERR(rt->fib6_metrics)) {
		err = PTR_ERR(rt->fib6_metrics);
		goto free;
	}

	if (cfg->fc_flags & RTF_ADDRCONF)
		rt->dst_nocount = true;

	if (cfg->fc_flags & RTF_EXPIRES)
		fib6_set_expires(rt, jiffies +
				 clock_t_to_jiffies(cfg->fc_expires));

	if (cfg->fc_protocol == RTPROT_UNSPEC)
		cfg->fc_protocol = RTPROT_BOOT;

	rt->fib6_protocol = cfg->fc_protocol;
	rt->fib6_table = table;
	rt->fib6_metric = cfg->fc_metric;
	rt->fib6_type = cfg->fc_type ? : RTN_UNICAST;
	rt->fib6_flags = cfg->fc_flags & ~RTF_GATEWAY;

	ipv6_addr_prefix(&rt->fib6_dst.addr, &cfg->fc_dst, cfg->fc_dst_len);
	rt->fib6_dst.plen = cfg->fc_dst_len;

#ifdef CONFIG_IPV6_SUBTREES
	ipv6_addr_prefix(&rt->fib6_src.addr, &cfg->fc_src, cfg->fc_src_len);
	rt->fib6_src.plen = cfg->fc_src_len;
#endif
	return rt;
free:
	kfree(rt);
err:
	return ERR_PTR(err);
}

static int ip6_route_info_create_nh(struct fib6_info *rt,
				    struct fib6_config *cfg,
				    gfp_t gfp_flags,
				    struct netlink_ext_ack *extack)
{
	struct net *net = cfg->fc_nlinfo.nl_net;
	struct fib6_nh *fib6_nh;
	int err;

	if (cfg->fc_nh_id) {
		struct nexthop *nh;

		rcu_read_lock();

		nh = nexthop_find_by_id(net, cfg->fc_nh_id);
		if (!nh) {
			err = -EINVAL;
			NL_SET_ERR_MSG(extack, "Nexthop id does not exist");
			goto out_free;
		}

		err = fib6_check_nexthop(nh, cfg, extack);
		if (err)
			goto out_free;

		if (!nexthop_get(nh)) {
			NL_SET_ERR_MSG(extack, "Nexthop has been deleted");
			err = -ENOENT;
			goto out_free;
		}

		rt->nh = nh;
		fib6_nh = nexthop_fib6_nh(rt->nh);

		rcu_read_unlock();
	} else {
		int addr_type;

		err = fib6_nh_init(net, rt->fib6_nh, cfg, gfp_flags, extack);
		if (err)
			goto out_release;

		fib6_nh = rt->fib6_nh;

		/* We cannot add true routes via loopback here, they would
		 * result in kernel looping; promote them to reject routes
		 */
		addr_type = ipv6_addr_type(&cfg->fc_dst);
		if (fib6_is_reject(cfg->fc_flags, rt->fib6_nh->fib_nh_dev,
				   addr_type))
			rt->fib6_flags = RTF_REJECT | RTF_NONEXTHOP;
	}

	if (!ipv6_addr_any(&cfg->fc_prefsrc)) {
		struct net_device *dev = fib6_nh->fib_nh_dev;

		if (!ipv6_chk_addr(net, &cfg->fc_prefsrc, dev, 0)) {
			NL_SET_ERR_MSG(extack, "Invalid source address");
			err = -EINVAL;
			goto out_release;
		}
		rt->fib6_prefsrc.addr = cfg->fc_prefsrc;
		rt->fib6_prefsrc.plen = 128;
	}

	return 0;
out_release:
	fib6_info_release(rt);
	return err;
out_free:
	rcu_read_unlock();
	ip_fib_metrics_put(rt->fib6_metrics);
	kfree(rt);
	return err;
}

int ip6_route_add(struct fib6_config *cfg, gfp_t gfp_flags,
		  struct netlink_ext_ack *extack)
{
	struct fib6_info *rt;
	int err;

	err = fib6_config_validate(cfg, extack);
	if (err)
		return err;

	rt = ip6_route_info_create(cfg, gfp_flags, extack);
	if (IS_ERR(rt))
		return PTR_ERR(rt);

	err = ip6_route_info_create_nh(rt, cfg, gfp_flags, extack);
	if (err)
		return err;

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

int ip6_del_rt(struct net *net, struct fib6_info *rt, bool skip_notify)
{
	struct nl_info info = {
		.nl_net = net,
		.skip_notify = skip_notify
	};

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
		struct fib6_node *fn;

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

		/* 'rt' points to the first sibling route. If it is not the
		 * leaf, then we do not need to send a notification. Otherwise,
		 * we need to check if the last sibling has a next route or not
		 * and emit a replace or delete notification, respectively.
		 */
		info->skip_notify_kernel = 1;
		fn = rcu_dereference_protected(rt->fib6_node,
					    lockdep_is_held(&table->tb6_lock));
		if (rcu_access_pointer(fn->leaf) == rt) {
			struct fib6_info *last_sibling, *replace_rt;

			last_sibling = list_last_entry(&rt->fib6_siblings,
						       struct fib6_info,
						       fib6_siblings);
			replace_rt = rcu_dereference_protected(
					    last_sibling->fib6_next,
					    lockdep_is_held(&table->tb6_lock));
			if (replace_rt)
				call_fib6_entry_notifiers_replace(net,
								  replace_rt);
			else
				call_fib6_multipath_entry_notifiers(net,
						       FIB_EVENT_ENTRY_DEL,
						       rt, rt->fib6_nsiblings,
						       NULL);
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

static int __ip6_del_cached_rt(struct rt6_info *rt, struct fib6_config *cfg)
{
	int rc = -ESRCH;

	if (cfg->fc_ifindex && rt->dst.dev->ifindex != cfg->fc_ifindex)
		goto out;

	if (cfg->fc_flags & RTF_GATEWAY &&
	    !ipv6_addr_equal(&cfg->fc_gateway, &rt->rt6i_gateway))
		goto out;

	rc = rt6_remove_exception_rt(rt);
out:
	return rc;
}

static int ip6_del_cached_rt(struct fib6_config *cfg, struct fib6_info *rt,
			     struct fib6_nh *nh)
{
	struct fib6_result res = {
		.f6i = rt,
		.nh = nh,
	};
	struct rt6_info *rt_cache;

	rt_cache = rt6_find_cached_rt(&res, &cfg->fc_dst, &cfg->fc_src);
	if (rt_cache)
		return __ip6_del_cached_rt(rt_cache, cfg);

	return 0;
}

struct fib6_nh_del_cached_rt_arg {
	struct fib6_config *cfg;
	struct fib6_info *f6i;
};

static int fib6_nh_del_cached_rt(struct fib6_nh *nh, void *_arg)
{
	struct fib6_nh_del_cached_rt_arg *arg = _arg;
	int rc;

	rc = ip6_del_cached_rt(arg->cfg, arg->f6i, nh);
	return rc != -ESRCH ? rc : 0;
}

static int ip6_del_cached_rt_nh(struct fib6_config *cfg, struct fib6_info *f6i)
{
	struct fib6_nh_del_cached_rt_arg arg = {
		.cfg = cfg,
		.f6i = f6i
	};

	return nexthop_for_each_fib6_nh(f6i->nh, fib6_nh_del_cached_rt, &arg);
}

static int ip6_route_del(struct fib6_config *cfg,
			 struct netlink_ext_ack *extack)
{
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
			struct fib6_nh *nh;

			if (rt->nh && cfg->fc_nh_id &&
			    rt->nh->id != cfg->fc_nh_id)
				continue;

			if (cfg->fc_flags & RTF_CACHE) {
				int rc = 0;

				if (rt->nh) {
					rc = ip6_del_cached_rt_nh(cfg, rt);
				} else if (cfg->fc_nh_id) {
					continue;
				} else {
					nh = rt->fib6_nh;
					rc = ip6_del_cached_rt(cfg, rt, nh);
				}
				if (rc != -ESRCH) {
					rcu_read_unlock();
					return rc;
				}
				continue;
			}

			if (cfg->fc_metric && cfg->fc_metric != rt->fib6_metric)
				continue;
			if (cfg->fc_protocol &&
			    cfg->fc_protocol != rt->fib6_protocol)
				continue;

			if (rt->nh) {
				if (!fib6_info_hold_safe(rt))
					continue;

				err =  __ip6_del_rt(rt, &cfg->fc_nlinfo);
				break;
			}
			if (cfg->fc_nh_id)
				continue;

			nh = rt->fib6_nh;
			if (cfg->fc_ifindex &&
			    (!nh->fib_nh_dev ||
			     nh->fib_nh_dev->ifindex != cfg->fc_ifindex))
				continue;
			if (cfg->fc_flags & RTF_GATEWAY &&
			    !ipv6_addr_equal(&cfg->fc_gateway, &nh->fib_nh_gw6))
				continue;
			if (!fib6_info_hold_safe(rt))
				continue;

			/* if gateway was specified only delete the one hop */
			if (cfg->fc_flags & RTF_GATEWAY)
				err = __ip6_del_rt(rt, &cfg->fc_nlinfo);
			else
				err = __ip6_del_rt_siblings(rt, cfg);
			break;
		}
	}
	rcu_read_unlock();

	return err;
}

static void rt6_do_redirect(struct dst_entry *dst, struct sock *sk, struct sk_buff *skb)
{
	struct netevent_redirect netevent;
	struct rt6_info *rt, *nrt = NULL;
	struct fib6_result res = {};
	struct ndisc_options ndopts;
	struct inet6_dev *in6_dev;
	struct neighbour *neigh;
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
	if (READ_ONCE(in6_dev->cnf.forwarding) ||
	    !READ_ONCE(in6_dev->cnf.accept_redirects))
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

	rt = dst_rt6_info(dst);
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
	res.f6i = rcu_dereference(rt->from);
	if (!res.f6i)
		goto out;

	if (res.f6i->nh) {
		struct fib6_nh_match_arg arg = {
			.dev = dst_dev(dst),
			.gw = &rt->rt6i_gateway,
		};

		nexthop_for_each_fib6_nh(res.f6i->nh,
					 fib6_nh_find_match, &arg);

		/* fib6_info uses a nexthop that does not have fib6_nh
		 * using the dst->dev. Should be impossible
		 */
		if (!arg.match)
			goto out;
		res.nh = arg.match;
	} else {
		res.nh = res.f6i->fib6_nh;
	}

	res.fib6_flags = res.f6i->fib6_flags;
	res.fib6_type = res.f6i->fib6_type;
	nrt = ip6_rt_cache_alloc(&res, &msg->dest, NULL);
	if (!nrt)
		goto out;

	nrt->rt6i_flags = RTF_GATEWAY|RTF_UP|RTF_DYNAMIC|RTF_CACHE;
	if (on_link)
		nrt->rt6i_flags &= ~RTF_GATEWAY;

	nrt->rt6i_gateway = *(struct in6_addr *)neigh->primary_key;

	/* rt6_insert_exception() will take care of duplicated exceptions */
	if (rt6_insert_exception(nrt, &res)) {
		dst_release_immediate(&nrt->dst);
		goto out;
	}

	netevent.old = &rt->dst;
	netevent.new = &nrt->dst;
	netevent.daddr = &msg->dest;
	netevent.neigh = neigh;
	call_netevent_notifiers(NETEVENT_REDIRECT, &netevent);

out:
	rcu_read_unlock();
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
		/* these routes do not use nexthops */
		if (rt->nh)
			continue;
		if (rt->fib6_nh->fib_nh_dev->ifindex != ifindex)
			continue;
		if (!(rt->fib6_flags & RTF_ROUTEINFO) ||
		    !rt->fib6_nh->fib_nh_gw_family)
			continue;
		if (!ipv6_addr_equal(&rt->fib6_nh->fib_nh_gw6, gwaddr))
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

	cfg.fc_table = l3mdev_fib_table(dev) ? : RT6_TABLE_INFO;
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
		struct fib6_nh *nh;

		/* RA routes do not use nexthops */
		if (rt->nh)
			continue;

		nh = rt->fib6_nh;
		if (dev == nh->fib_nh_dev &&
		    ((rt->fib6_flags & (RTF_ADDRCONF | RTF_DEFAULT)) == (RTF_ADDRCONF | RTF_DEFAULT)) &&
		    ipv6_addr_equal(&nh->fib_nh_gw6, addr))
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
				     unsigned int pref,
				     u32 defrtr_usr_metric,
				     int lifetime)
{
	struct fib6_config cfg = {
		.fc_table	= l3mdev_fib_table(dev) ? : RT6_TABLE_DFLT,
		.fc_metric	= defrtr_usr_metric,
		.fc_ifindex	= dev->ifindex,
		.fc_flags	= RTF_GATEWAY | RTF_ADDRCONF | RTF_DEFAULT |
				  RTF_UP | RTF_EXPIRES | RTF_PREF(pref),
		.fc_protocol = RTPROT_RA,
		.fc_type = RTN_UNICAST,
		.fc_nlinfo.portid = 0,
		.fc_nlinfo.nlh = NULL,
		.fc_nlinfo.nl_net = net,
		.fc_expires = jiffies_to_clock_t(lifetime * HZ),
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
			ip6_del_rt(net, rt, false);
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
	*cfg = (struct fib6_config){
		.fc_table = l3mdev_fib_table_by_index(net, rtmsg->rtmsg_ifindex) ?
			 : RT6_TABLE_MAIN,
		.fc_ifindex = rtmsg->rtmsg_ifindex,
		.fc_metric = rtmsg->rtmsg_metric,
		.fc_expires = rtmsg->rtmsg_info,
		.fc_dst_len = rtmsg->rtmsg_dst_len,
		.fc_src_len = rtmsg->rtmsg_src_len,
		.fc_flags = rtmsg->rtmsg_flags,
		.fc_type = rtmsg->rtmsg_type,

		.fc_nlinfo.nl_net = net,

		.fc_dst = rtmsg->rtmsg_dst,
		.fc_src = rtmsg->rtmsg_src,
		.fc_gateway = rtmsg->rtmsg_gateway,
	};
}

int ipv6_route_ioctl(struct net *net, unsigned int cmd, struct in6_rtmsg *rtmsg)
{
	struct fib6_config cfg;
	int err;

	if (cmd != SIOCADDRT && cmd != SIOCDELRT)
		return -EINVAL;
	if (!ns_capable(net->user_ns, CAP_NET_ADMIN))
		return -EPERM;

	rtmsg_to_fib6_config(net, rtmsg, &cfg);

	switch (cmd) {
	case SIOCADDRT:
		/* Only do the default setting of fc_metric in route adding */
		if (cfg.fc_metric == 0)
			cfg.fc_metric = IP6_RT_PRIO_USER;
		err = ip6_route_add(&cfg, GFP_KERNEL, NULL);
		break;
	case SIOCDELRT:
		err = ip6_route_del(&cfg, NULL);
		break;
	}

	return err;
}

/*
 *	Drop the packet on the floor
 */

static int ip6_pkt_drop(struct sk_buff *skb, u8 code, int ipstats_mib_noroutes)
{
	struct dst_entry *dst = skb_dst(skb);
	struct net_device *dev = dst_dev(dst);
	struct net *net = dev_net(dev);
	struct inet6_dev *idev;
	SKB_DR(reason);
	int type;

	if (netif_is_l3_master(skb->dev) ||
	    dev == net->loopback_dev)
		idev = __in6_dev_get_safely(dev_get_by_index_rcu(net, IP6CB(skb)->iif));
	else
		idev = ip6_dst_idev(dst);

	switch (ipstats_mib_noroutes) {
	case IPSTATS_MIB_INNOROUTES:
		type = ipv6_addr_type(&ipv6_hdr(skb)->daddr);
		if (type == IPV6_ADDR_ANY) {
			SKB_DR_SET(reason, IP_INADDRERRORS);
			IP6_INC_STATS(net, idev, IPSTATS_MIB_INADDRERRORS);
			break;
		}
		SKB_DR_SET(reason, IP_INNOROUTES);
		fallthrough;
	case IPSTATS_MIB_OUTNOROUTES:
		SKB_DR_OR(reason, IP_OUTNOROUTES);
		IP6_INC_STATS(net, idev, ipstats_mib_noroutes);
		break;
	}

	/* Start over by dropping the dst for l3mdev case */
	if (netif_is_l3_master(skb->dev))
		skb_dst_drop(skb);

	icmpv6_send(skb, ICMPV6_DEST_UNREACH, code, 0);
	kfree_skb_reason(skb, reason);
	return 0;
}

static int ip6_pkt_discard(struct sk_buff *skb)
{
	return ip6_pkt_drop(skb, ICMPV6_NOROUTE, IPSTATS_MIB_INNOROUTES);
}

static int ip6_pkt_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb->dev = skb_dst_dev(skb);
	return ip6_pkt_drop(skb, ICMPV6_NOROUTE, IPSTATS_MIB_OUTNOROUTES);
}

static int ip6_pkt_prohibit(struct sk_buff *skb)
{
	return ip6_pkt_drop(skb, ICMPV6_ADM_PROHIBITED, IPSTATS_MIB_INNOROUTES);
}

static int ip6_pkt_prohibit_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	skb->dev = skb_dst_dev(skb);
	return ip6_pkt_drop(skb, ICMPV6_ADM_PROHIBITED, IPSTATS_MIB_OUTNOROUTES);
}

/*
 *	Allocate a dst for local (unicast / anycast) address.
 */

struct fib6_info *addrconf_f6i_alloc(struct net *net,
				     struct inet6_dev *idev,
				     const struct in6_addr *addr,
				     bool anycast, gfp_t gfp_flags,
				     struct netlink_ext_ack *extack)
{
	struct fib6_config cfg = {
		.fc_table = l3mdev_fib_table(idev->dev) ? : RT6_TABLE_LOCAL,
		.fc_ifindex = idev->dev->ifindex,
		.fc_flags = RTF_UP | RTF_NONEXTHOP,
		.fc_dst = *addr,
		.fc_dst_len = 128,
		.fc_protocol = RTPROT_KERNEL,
		.fc_nlinfo.nl_net = net,
		.fc_ignore_dev_down = true,
	};
	struct fib6_info *f6i;
	int err;

	if (anycast) {
		cfg.fc_type = RTN_ANYCAST;
		cfg.fc_flags |= RTF_ANYCAST;
	} else {
		cfg.fc_type = RTN_LOCAL;
		cfg.fc_flags |= RTF_LOCAL;
	}

	f6i = ip6_route_info_create(&cfg, gfp_flags, extack);
	if (IS_ERR(f6i))
		return f6i;

	err = ip6_route_info_create_nh(f6i, &cfg, gfp_flags, extack);
	if (err)
		return ERR_PTR(err);

	f6i->dst_nocount = true;

	if (!anycast &&
	    (READ_ONCE(net->ipv6.devconf_all->disable_policy) ||
	     READ_ONCE(idev->cnf.disable_policy)))
		f6i->dst_nopolicy = true;

	return f6i;
}

/* remove deleted ip from prefsrc entries */
struct arg_dev_net_ip {
	struct net *net;
	struct in6_addr *addr;
};

static int fib6_remove_prefsrc(struct fib6_info *rt, void *arg)
{
	struct net *net = ((struct arg_dev_net_ip *)arg)->net;
	struct in6_addr *addr = ((struct arg_dev_net_ip *)arg)->addr;

	if (!rt->nh &&
	    rt != net->ipv6.fib6_null_entry &&
	    ipv6_addr_equal(addr, &rt->fib6_prefsrc.addr) &&
	    !ipv6_chk_addr(net, addr, rt->fib6_nh->fib_nh_dev, 0)) {
		spin_lock_bh(&rt6_exception_lock);
		/* remove prefsrc entry */
		rt->fib6_prefsrc.plen = 0;
		spin_unlock_bh(&rt6_exception_lock);
	}
	return 0;
}

void rt6_remove_prefsrc(struct inet6_ifaddr *ifp)
{
	struct net *net = dev_net(ifp->idev->dev);
	struct arg_dev_net_ip adni = {
		.net = net,
		.addr = &ifp->addr,
	};
	fib6_clean_all(net, fib6_remove_prefsrc, &adni);
}

#define RTF_RA_ROUTER		(RTF_ADDRCONF | RTF_DEFAULT)

/* Remove routers and update dst entries when gateway turn into host. */
static int fib6_clean_tohost(struct fib6_info *rt, void *arg)
{
	struct in6_addr *gateway = (struct in6_addr *)arg;
	struct fib6_nh *nh;

	/* RA routes do not use nexthops */
	if (rt->nh)
		return 0;

	nh = rt->fib6_nh;
	if (((rt->fib6_flags & RTF_RA_ROUTER) == RTF_RA_ROUTER) &&
	    nh->fib_nh_gw_family && ipv6_addr_equal(gateway, &nh->fib_nh_gw6))
		return -1;

	/* Further clean up cached routes in exception table.
	 * This is needed because cached route may have a different
	 * gateway than its 'parent' in the case of an ip redirect.
	 */
	fib6_nh_exceptions_clean_tohost(nh, gateway);

	return 0;
}

void rt6_clean_tohost(struct net *net, struct in6_addr *gateway)
{
	fib6_clean_all(net, fib6_clean_tohost, gateway);
}

struct arg_netdev_event {
	const struct net_device *dev;
	union {
		unsigned char nh_flags;
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

/* only called for fib entries with builtin fib6_nh */
static bool rt6_is_dead(const struct fib6_info *rt)
{
	if (rt->fib6_nh->fib_nh_flags & RTNH_F_DEAD ||
	    (rt->fib6_nh->fib_nh_flags & RTNH_F_LINKDOWN &&
	     ip6_ignore_linkdown(rt->fib6_nh->fib_nh_dev)))
		return true;

	return false;
}

static int rt6_multipath_total_weight(const struct fib6_info *rt)
{
	struct fib6_info *iter;
	int total = 0;

	if (!rt6_is_dead(rt))
		total += rt->fib6_nh->fib_nh_weight;

	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings) {
		if (!rt6_is_dead(iter))
			total += iter->fib6_nh->fib_nh_weight;
	}

	return total;
}

static void rt6_upper_bound_set(struct fib6_info *rt, int *weight, int total)
{
	int upper_bound = -1;

	if (!rt6_is_dead(rt)) {
		*weight += rt->fib6_nh->fib_nh_weight;
		upper_bound = DIV_ROUND_CLOSEST_ULL((u64) (*weight) << 31,
						    total) - 1;
	}
	atomic_set(&rt->fib6_nh->fib_nh_upper_bound, upper_bound);
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

	if (rt != net->ipv6.fib6_null_entry && !rt->nh &&
	    rt->fib6_nh->fib_nh_dev == arg->dev) {
		rt->fib6_nh->fib_nh_flags &= ~arg->nh_flags;
		fib6_update_sernum_upto_root(net, rt);
		rt6_multipath_rebalance(rt);
	}

	return 0;
}

void rt6_sync_up(struct net_device *dev, unsigned char nh_flags)
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

/* only called for fib entries with inline fib6_nh */
static bool rt6_multipath_uses_dev(const struct fib6_info *rt,
				   const struct net_device *dev)
{
	struct fib6_info *iter;

	if (rt->fib6_nh->fib_nh_dev == dev)
		return true;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		if (iter->fib6_nh->fib_nh_dev == dev)
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

	if (rt->fib6_nh->fib_nh_dev == down_dev ||
	    rt->fib6_nh->fib_nh_flags & RTNH_F_DEAD)
		dead++;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		if (iter->fib6_nh->fib_nh_dev == down_dev ||
		    iter->fib6_nh->fib_nh_flags & RTNH_F_DEAD)
			dead++;

	return dead;
}

static void rt6_multipath_nh_flags_set(struct fib6_info *rt,
				       const struct net_device *dev,
				       unsigned char nh_flags)
{
	struct fib6_info *iter;

	if (rt->fib6_nh->fib_nh_dev == dev)
		rt->fib6_nh->fib_nh_flags |= nh_flags;
	list_for_each_entry(iter, &rt->fib6_siblings, fib6_siblings)
		if (iter->fib6_nh->fib_nh_dev == dev)
			iter->fib6_nh->fib_nh_flags |= nh_flags;
}

/* called with write lock held for table with rt */
static int fib6_ifdown(struct fib6_info *rt, void *p_arg)
{
	const struct arg_netdev_event *arg = p_arg;
	const struct net_device *dev = arg->dev;
	struct net *net = dev_net(dev);

	if (rt == net->ipv6.fib6_null_entry || rt->nh)
		return 0;

	switch (arg->event) {
	case NETDEV_UNREGISTER:
		return rt->fib6_nh->fib_nh_dev == dev ? -1 : 0;
	case NETDEV_DOWN:
		if (rt->should_flush)
			return -1;
		if (!rt->fib6_nsiblings)
			return rt->fib6_nh->fib_nh_dev == dev ? -1 : 0;
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
		if (rt->fib6_nh->fib_nh_dev != dev ||
		    rt->fib6_flags & (RTF_LOCAL | RTF_ANYCAST))
			break;
		rt->fib6_nh->fib_nh_flags |= RTNH_F_LINKDOWN;
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
	struct net *net = dev_net(dev);

	if (net->ipv6.sysctl.skip_notify_on_dev_down)
		fib6_clean_all_skip_notify(net, fib6_ifdown, &arg);
	else
		fib6_clean_all(net, fib6_ifdown, &arg);
}

void rt6_disable_ip(struct net_device *dev, unsigned long event)
{
	rt6_sync_down_dev(dev, event);
	rt6_uncached_list_flush_dev(dev);
	neigh_ifdown(&nd_tbl, dev);
}

struct rt6_mtu_change_arg {
	struct net_device *dev;
	unsigned int mtu;
	struct fib6_info *f6i;
};

static int fib6_nh_mtu_change(struct fib6_nh *nh, void *_arg)
{
	struct rt6_mtu_change_arg *arg = (struct rt6_mtu_change_arg *)_arg;
	struct fib6_info *f6i = arg->f6i;

	/* For administrative MTU increase, there is no way to discover
	 * IPv6 PMTU increase, so PMTU increase should be updated here.
	 * Since RFC 1981 doesn't include administrative MTU increase
	 * update PMTU increase is a MUST. (i.e. jumbo frame)
	 */
	if (nh->fib_nh_dev == arg->dev) {
		struct inet6_dev *idev = __in6_dev_get(arg->dev);
		u32 mtu = f6i->fib6_pmtu;

		if (mtu >= arg->mtu ||
		    (mtu < arg->mtu && mtu == idev->cnf.mtu6))
			fib6_metric_set(f6i, RTAX_MTU, arg->mtu);

		spin_lock_bh(&rt6_exception_lock);
		rt6_exceptions_update_pmtu(idev, nh, arg->mtu);
		spin_unlock_bh(&rt6_exception_lock);
	}

	return 0;
}

static int rt6_mtu_change_route(struct fib6_info *f6i, void *p_arg)
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

	if (fib6_metric_locked(f6i, RTAX_MTU))
		return 0;

	arg->f6i = f6i;
	if (f6i->nh) {
		/* fib6_nh_mtu_change only returns 0, so this is safe */
		return nexthop_for_each_fib6_nh(f6i->nh, fib6_nh_mtu_change,
						arg);
	}

	return fib6_nh_mtu_change(f6i->fib6_nh, arg);
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
	[RTA_UNSPEC]		= { .strict_start_type = RTA_DPORT + 1 },
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
	[RTA_NH_ID]		= { .type = NLA_U32 },
	[RTA_FLOWLABEL]		= { .type = NLA_BE32 },
};

static int rtm_to_fib6_multipath_config(struct fib6_config *cfg,
					struct netlink_ext_ack *extack,
					bool newroute)
{
	struct rtnexthop *rtnh;
	int remaining;

	remaining = cfg->fc_mp_len;
	rtnh = (struct rtnexthop *)cfg->fc_mp;

	if (!rtnh_ok(rtnh, remaining)) {
		NL_SET_ERR_MSG(extack, "Invalid nexthop configuration - no valid nexthops");
		return -EINVAL;
	}

	do {
		bool has_gateway = cfg->fc_flags & RTF_GATEWAY;
		int attrlen = rtnh_attrlen(rtnh);

		if (attrlen > 0) {
			struct nlattr *nla, *attrs;

			attrs = rtnh_attrs(rtnh);
			nla = nla_find(attrs, attrlen, RTA_GATEWAY);
			if (nla) {
				if (nla_len(nla) < sizeof(cfg->fc_gateway)) {
					NL_SET_ERR_MSG(extack,
						       "Invalid IPv6 address in RTA_GATEWAY");
					return -EINVAL;
				}

				has_gateway = true;
			}
		}

		if (newroute && (cfg->fc_nh_id || !has_gateway)) {
			NL_SET_ERR_MSG(extack,
				       "Device only routes can not be added for IPv6 using the multipath API.");
			return -EINVAL;
		}

		rtnh = rtnh_next(rtnh, &remaining);
	} while (rtnh_ok(rtnh, remaining));

	return lwtunnel_valid_encap_type_attr(cfg->fc_mp, cfg->fc_mp_len, extack);
}

static int rtm_to_fib6_config(struct sk_buff *skb, struct nlmsghdr *nlh,
			      struct fib6_config *cfg,
			      struct netlink_ext_ack *extack)
{
	bool newroute = nlh->nlmsg_type == RTM_NEWROUTE;
	struct nlattr *tb[RTA_MAX+1];
	struct rtmsg *rtm;
	unsigned int pref;
	int err;

	err = nlmsg_parse_deprecated(nlh, sizeof(*rtm), tb, RTA_MAX,
				     rtm_ipv6_policy, extack);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	rtm = nlmsg_data(nlh);

	if (rtm->rtm_tos) {
		NL_SET_ERR_MSG(extack,
			       "Invalid dsfield (tos): option not available for IPv6");
		goto errout;
	}

	if (tb[RTA_FLOWLABEL]) {
		NL_SET_ERR_MSG_ATTR(extack, tb[RTA_FLOWLABEL],
				    "Flow label cannot be specified for this operation");
		goto errout;
	}

	*cfg = (struct fib6_config){
		.fc_table = rtm->rtm_table,
		.fc_dst_len = rtm->rtm_dst_len,
		.fc_src_len = rtm->rtm_src_len,
		.fc_flags = RTF_UP,
		.fc_protocol = rtm->rtm_protocol,
		.fc_type = rtm->rtm_type,

		.fc_nlinfo.portid = NETLINK_CB(skb).portid,
		.fc_nlinfo.nlh = nlh,
		.fc_nlinfo.nl_net = sock_net(skb->sk),
	};

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

	if (tb[RTA_NH_ID]) {
		if (tb[RTA_GATEWAY]   || tb[RTA_OIF] ||
		    tb[RTA_MULTIPATH] || tb[RTA_ENCAP]) {
			NL_SET_ERR_MSG(extack,
				       "Nexthop specification and nexthop id are mutually exclusive");
			goto errout;
		}
		cfg->fc_nh_id = nla_get_u32(tb[RTA_NH_ID]);
	}

	if (tb[RTA_GATEWAY]) {
		cfg->fc_gateway = nla_get_in6_addr(tb[RTA_GATEWAY]);
		cfg->fc_flags |= RTF_GATEWAY;
	}
	if (tb[RTA_VIA]) {
		NL_SET_ERR_MSG(extack, "IPv6 does not support RTA_VIA attribute");
		goto errout;
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

		err = rtm_to_fib6_multipath_config(cfg, extack, newroute);
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
	struct list_head list;
};

static int ip6_route_info_append(struct list_head *rt6_nh_list,
				 struct fib6_info *rt,
				 struct fib6_config *r_cfg)
{
	struct rt6_nh *nh;

	list_for_each_entry(nh, rt6_nh_list, list) {
		/* check if fib6_info already exists */
		if (rt6_duplicate_nexthop(nh->fib6_info, rt))
			return -EEXIST;
	}

	nh = kzalloc(sizeof(*nh), GFP_KERNEL);
	if (!nh)
		return -ENOMEM;

	nh->fib6_info = rt;
	memcpy(&nh->r_cfg, r_cfg, sizeof(*r_cfg));
	list_add_tail(&nh->list, rt6_nh_list);

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
	rcu_read_lock();

	if ((nlflags & NLM_F_APPEND) && rt_last &&
	    READ_ONCE(rt_last->fib6_nsiblings)) {
		rt = list_first_or_null_rcu(&rt_last->fib6_siblings,
					    struct fib6_info,
					    fib6_siblings);
	}

	if (rt)
		inet6_rt_notify(RTM_NEWROUTE, rt, info, nlflags);

	rcu_read_unlock();
}

static bool ip6_route_mpath_should_notify(const struct fib6_info *rt)
{
	bool rt_can_ecmp = rt6_qualify_for_ecmp(rt);
	bool should_notify = false;
	struct fib6_info *leaf;
	struct fib6_node *fn;

	rcu_read_lock();
	fn = rcu_dereference(rt->fib6_node);
	if (!fn)
		goto out;

	leaf = rcu_dereference(fn->leaf);
	if (!leaf)
		goto out;

	if (rt == leaf ||
	    (rt_can_ecmp && rt->fib6_metric == leaf->fib6_metric &&
	     rt6_qualify_for_ecmp(leaf)))
		should_notify = true;
out:
	rcu_read_unlock();

	return should_notify;
}

static int ip6_route_multipath_add(struct fib6_config *cfg,
				   struct netlink_ext_ack *extack)
{
	struct fib6_info *rt_notif = NULL, *rt_last = NULL;
	struct nl_info *info = &cfg->fc_nlinfo;
	struct rt6_nh *nh, *nh_safe;
	struct fib6_config r_cfg;
	struct rtnexthop *rtnh;
	LIST_HEAD(rt6_nh_list);
	struct rt6_nh *err_nh;
	struct fib6_info *rt;
	__u16 nlflags;
	int remaining;
	int attrlen;
	int replace;
	int nhn = 0;
	int err;

	err = fib6_config_validate(cfg, extack);
	if (err)
		return err;

	replace = (cfg->fc_nlinfo.nlh &&
		   (cfg->fc_nlinfo.nlh->nlmsg_flags & NLM_F_REPLACE));

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

		err = ip6_route_info_create_nh(rt, &r_cfg, GFP_KERNEL, extack);
		if (err) {
			rt = NULL;
			goto cleanup;
		}

		rt->fib6_nh->fib_nh_weight = rtnh->rtnh_hops + 1;

		err = ip6_route_info_append(&rt6_nh_list, rt, &r_cfg);
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

	/* For add and replace, send one notification with all nexthops. For
	 * append, send one notification with all appended nexthops.
	 */
	info->skip_notify_kernel = 1;

	err_nh = NULL;
	list_for_each_entry(nh, &rt6_nh_list, list) {
		err = __ip6_ins_rt(nh->fib6_info, info, extack);

		if (err) {
			if (replace && nhn)
				NL_SET_ERR_MSG_MOD(extack,
						   "multipath route replace failed (check consistency of installed routes)");
			err_nh = nh;
			goto add_errout;
		}
		/* save reference to last route successfully inserted */
		rt_last = nh->fib6_info;

		/* save reference to first route for notification */
		if (!rt_notif)
			rt_notif = nh->fib6_info;

		/* Because each route is added like a single route we remove
		 * these flags after the first nexthop: if there is a collision,
		 * we have already failed to add the first nexthop:
		 * fib6_add_rt2node() has rejected it; when replacing, old
		 * nexthops have been replaced by first new, the rest should
		 * be added to it.
		 */
		if (cfg->fc_nlinfo.nlh) {
			cfg->fc_nlinfo.nlh->nlmsg_flags &= ~(NLM_F_EXCL |
							     NLM_F_REPLACE);
			cfg->fc_nlinfo.nlh->nlmsg_flags |= NLM_F_CREATE;
		}
		nhn++;
	}

	/* An in-kernel notification should only be sent in case the new
	 * multipath route is added as the first route in the node, or if
	 * it was appended to it. We pass 'rt_notif' since it is the first
	 * sibling and might allow us to skip some checks in the replace case.
	 */
	if (ip6_route_mpath_should_notify(rt_notif)) {
		enum fib_event_type fib_event;

		if (rt_notif->fib6_nsiblings != nhn - 1)
			fib_event = FIB_EVENT_ENTRY_APPEND;
		else
			fib_event = FIB_EVENT_ENTRY_REPLACE;

		err = call_fib6_multipath_entry_notifiers(info->nl_net,
							  fib_event, rt_notif,
							  nhn - 1, extack);
		if (err) {
			/* Delete all the siblings that were just added */
			err_nh = NULL;
			goto add_errout;
		}
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
	list_for_each_entry(nh, &rt6_nh_list, list) {
		if (err_nh == nh)
			break;
		ip6_route_del(&nh->r_cfg, extack);
	}

cleanup:
	list_for_each_entry_safe(nh, nh_safe, &rt6_nh_list, list) {
		fib6_info_release(nh->fib6_info);
		list_del(&nh->list);
		kfree(nh);
	}

	return err;
}

static int ip6_route_multipath_del(struct fib6_config *cfg,
				   struct netlink_ext_ack *extack)
{
	struct fib6_config r_cfg;
	struct rtnexthop *rtnh;
	int last_err = 0;
	int remaining;
	int attrlen;
	int err;

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
				r_cfg.fc_gateway = nla_get_in6_addr(nla);
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

	if (cfg.fc_nh_id) {
		rcu_read_lock();
		err = !nexthop_find_by_id(sock_net(skb->sk), cfg.fc_nh_id);
		rcu_read_unlock();

		if (err) {
			NL_SET_ERR_MSG(extack, "Nexthop id does not exist");
			return -EINVAL;
		}
	}

	if (cfg.fc_mp) {
		return ip6_route_multipath_del(&cfg, extack);
	} else {
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

	if (cfg.fc_metric == 0)
		cfg.fc_metric = IP6_RT_PRIO_USER;

	if (cfg.fc_mp)
		return ip6_route_multipath_add(&cfg, extack);
	else
		return ip6_route_add(&cfg, GFP_KERNEL, extack);
}

/* add the overhead of this fib6_nh to nexthop_len */
static int rt6_nh_nlmsg_size(struct fib6_nh *nh, void *arg)
{
	int *nexthop_len = arg;

	*nexthop_len += nla_total_size(0)	 /* RTA_MULTIPATH */
		     + NLA_ALIGN(sizeof(struct rtnexthop))
		     + nla_total_size(16); /* RTA_GATEWAY */

	if (nh->fib_nh_lws) {
		/* RTA_ENCAP_TYPE */
		*nexthop_len += lwtunnel_get_encap_size(nh->fib_nh_lws);
		/* RTA_ENCAP */
		*nexthop_len += nla_total_size(2);
	}

	return 0;
}

static size_t rt6_nlmsg_size(struct fib6_info *f6i)
{
	struct fib6_info *sibling;
	struct fib6_nh *nh;
	int nexthop_len;

	if (f6i->nh) {
		nexthop_len = nla_total_size(4); /* RTA_NH_ID */
		nexthop_for_each_fib6_nh(f6i->nh, rt6_nh_nlmsg_size,
					 &nexthop_len);
		goto common;
	}

	rcu_read_lock();
retry:
	nh = f6i->fib6_nh;
	nexthop_len = 0;
	if (READ_ONCE(f6i->fib6_nsiblings)) {
		rt6_nh_nlmsg_size(nh, &nexthop_len);

		list_for_each_entry_rcu(sibling, &f6i->fib6_siblings,
					fib6_siblings) {
			rt6_nh_nlmsg_size(sibling->fib6_nh, &nexthop_len);
			if (!READ_ONCE(f6i->fib6_nsiblings))
				goto retry;
		}
	}
	rcu_read_unlock();
	nexthop_len += lwtunnel_get_encap_size(nh->fib_nh_lws);
common:
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
	       + nexthop_len;
}

static int rt6_fill_node_nexthop(struct sk_buff *skb, struct nexthop *nh,
				 unsigned char *flags)
{
	if (nexthop_is_multipath(nh)) {
		struct nlattr *mp;

		mp = nla_nest_start_noflag(skb, RTA_MULTIPATH);
		if (!mp)
			goto nla_put_failure;

		if (nexthop_mpath_fill_node(skb, nh, AF_INET6))
			goto nla_put_failure;

		nla_nest_end(skb, mp);
	} else {
		struct fib6_nh *fib6_nh;

		fib6_nh = nexthop_fib6_nh(nh);
		if (fib_nexthop_info(skb, &fib6_nh->nh_common, AF_INET6,
				     flags, false) < 0)
			goto nla_put_failure;
	}

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
	struct rt6_info *rt6 = dst_rt6_info(dst);
	struct rt6key *rt6_dst, *rt6_src;
	u32 *pmetrics, table, rt6_flags;
	unsigned char nh_flags = 0;
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
	rtm->rtm_table = table < 256 ? table : RT_TABLE_COMPAT;
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
		if (ip6_route_get_saddr(net, rt, dest, 0, 0, &saddr_buf) == 0 &&
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
		struct net_device *dev;

		if (rt6_flags & RTF_GATEWAY &&
		    nla_put_in6_addr(skb, RTA_GATEWAY, &rt6->rt6i_gateway))
			goto nla_put_failure;

		dev = dst_dev(dst);
		if (dev && nla_put_u32(skb, RTA_OIF, dev->ifindex))
			goto nla_put_failure;

		if (lwtunnel_fill_encap(skb, dst->lwtstate, RTA_ENCAP, RTA_ENCAP_TYPE) < 0)
			goto nla_put_failure;
	} else if (READ_ONCE(rt->fib6_nsiblings)) {
		struct fib6_info *sibling;
		struct nlattr *mp;

		mp = nla_nest_start_noflag(skb, RTA_MULTIPATH);
		if (!mp)
			goto nla_put_failure;

		if (fib_add_nexthop(skb, &rt->fib6_nh->nh_common,
				    rt->fib6_nh->fib_nh_weight, AF_INET6,
				    0) < 0)
			goto nla_put_failure;

		rcu_read_lock();

		list_for_each_entry_rcu(sibling, &rt->fib6_siblings,
					fib6_siblings) {
			if (fib_add_nexthop(skb, &sibling->fib6_nh->nh_common,
					    sibling->fib6_nh->fib_nh_weight,
					    AF_INET6, 0) < 0) {
				rcu_read_unlock();

				goto nla_put_failure;
			}
		}

		rcu_read_unlock();

		nla_nest_end(skb, mp);
	} else if (rt->nh) {
		if (nla_put_u32(skb, RTA_NH_ID, rt->nh->id))
			goto nla_put_failure;

		if (nexthop_is_blackhole(rt->nh))
			rtm->rtm_type = RTN_BLACKHOLE;

		if (READ_ONCE(net->ipv4.sysctl_nexthop_compat_mode) &&
		    rt6_fill_node_nexthop(skb, rt->nh, &nh_flags) < 0)
			goto nla_put_failure;

		rtm->rtm_flags |= nh_flags;
	} else {
		if (fib_nexthop_info(skb, &rt->fib6_nh->nh_common, AF_INET6,
				     &nh_flags, false) < 0)
			goto nla_put_failure;

		rtm->rtm_flags |= nh_flags;
	}

	if (rt6_flags & RTF_EXPIRES) {
		expires = dst ? READ_ONCE(dst->expires) : rt->expires;
		expires -= jiffies;
	}

	if (!dst) {
		if (READ_ONCE(rt->offload))
			rtm->rtm_flags |= RTM_F_OFFLOAD;
		if (READ_ONCE(rt->trap))
			rtm->rtm_flags |= RTM_F_TRAP;
		if (READ_ONCE(rt->offload_failed))
			rtm->rtm_flags |= RTM_F_OFFLOAD_FAILED;
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

static int fib6_info_nh_uses_dev(struct fib6_nh *nh, void *arg)
{
	const struct net_device *dev = arg;

	if (nh->fib_nh_dev == dev)
		return 1;

	return 0;
}

static bool fib6_info_uses_dev(const struct fib6_info *f6i,
			       const struct net_device *dev)
{
	if (f6i->nh) {
		struct net_device *_dev = (struct net_device *)dev;

		return !!nexthop_for_each_fib6_nh(f6i->nh,
						  fib6_info_nh_uses_dev,
						  _dev);
	}

	if (f6i->fib6_nh->fib_nh_dev == dev)
		return true;

	if (READ_ONCE(f6i->fib6_nsiblings)) {
		const struct fib6_info *sibling;

		rcu_read_lock();
		list_for_each_entry_rcu(sibling, &f6i->fib6_siblings,
					fib6_siblings) {
			if (sibling->fib6_nh->fib_nh_dev == dev) {
				rcu_read_unlock();
				return true;
			}
			if (!READ_ONCE(f6i->fib6_nsiblings))
				break;
		}
		rcu_read_unlock();
	}
	return false;
}

struct fib6_nh_exception_dump_walker {
	struct rt6_rtnl_dump_arg *dump;
	struct fib6_info *rt;
	unsigned int flags;
	unsigned int skip;
	unsigned int count;
};

static int rt6_nh_dump_exceptions(struct fib6_nh *nh, void *arg)
{
	struct fib6_nh_exception_dump_walker *w = arg;
	struct rt6_rtnl_dump_arg *dump = w->dump;
	struct rt6_exception_bucket *bucket;
	struct rt6_exception *rt6_ex;
	int i, err;

	bucket = fib6_nh_get_excptn_bucket(nh, NULL);
	if (!bucket)
		return 0;

	for (i = 0; i < FIB6_EXCEPTION_BUCKET_SIZE; i++) {
		hlist_for_each_entry(rt6_ex, &bucket->chain, hlist) {
			if (w->skip) {
				w->skip--;
				continue;
			}

			/* Expiration of entries doesn't bump sernum, insertion
			 * does. Removal is triggered by insertion, so we can
			 * rely on the fact that if entries change between two
			 * partial dumps, this node is scanned again completely,
			 * see rt6_insert_exception() and fib6_dump_table().
			 *
			 * Count expired entries we go through as handled
			 * entries that we'll skip next time, in case of partial
			 * node dump. Otherwise, if entries expire meanwhile,
			 * we'll skip the wrong amount.
			 */
			if (rt6_check_expired(rt6_ex->rt6i)) {
				w->count++;
				continue;
			}

			err = rt6_fill_node(dump->net, dump->skb, w->rt,
					    &rt6_ex->rt6i->dst, NULL, NULL, 0,
					    RTM_NEWROUTE,
					    NETLINK_CB(dump->cb->skb).portid,
					    dump->cb->nlh->nlmsg_seq, w->flags);
			if (err)
				return err;

			w->count++;
		}
		bucket++;
	}

	return 0;
}

/* Return -1 if done with node, number of handled routes on partial dump */
int rt6_dump_route(struct fib6_info *rt, void *p_arg, unsigned int skip)
{
	struct rt6_rtnl_dump_arg *arg = (struct rt6_rtnl_dump_arg *) p_arg;
	struct fib_dump_filter *filter = &arg->filter;
	unsigned int flags = NLM_F_MULTI;
	struct net *net = arg->net;
	int count = 0;

	if (rt == net->ipv6.fib6_null_entry)
		return -1;

	if ((filter->flags & RTM_F_PREFIX) &&
	    !(rt->fib6_flags & RTF_PREFIX_RT)) {
		/* success since this is not a prefix route */
		return -1;
	}
	if (filter->filter_set &&
	    ((filter->rt_type  && rt->fib6_type != filter->rt_type) ||
	     (filter->dev      && !fib6_info_uses_dev(rt, filter->dev)) ||
	     (filter->protocol && rt->fib6_protocol != filter->protocol))) {
		return -1;
	}

	if (filter->filter_set ||
	    !filter->dump_routes || !filter->dump_exceptions) {
		flags |= NLM_F_DUMP_FILTERED;
	}

	if (filter->dump_routes) {
		if (skip) {
			skip--;
		} else {
			if (rt6_fill_node(net, arg->skb, rt, NULL, NULL, NULL,
					  0, RTM_NEWROUTE,
					  NETLINK_CB(arg->cb->skb).portid,
					  arg->cb->nlh->nlmsg_seq, flags)) {
				return 0;
			}
			count++;
		}
	}

	if (filter->dump_exceptions) {
		struct fib6_nh_exception_dump_walker w = { .dump = arg,
							   .rt = rt,
							   .flags = flags,
							   .skip = skip,
							   .count = 0 };
		int err;

		rcu_read_lock();
		if (rt->nh) {
			err = nexthop_for_each_fib6_nh(rt->nh,
						       rt6_nh_dump_exceptions,
						       &w);
		} else {
			err = rt6_nh_dump_exceptions(rt->fib6_nh, &w);
		}
		rcu_read_unlock();

		if (err)
			return count + w.count;
	}

	return -1;
}

static int inet6_rtm_valid_getroute_req(struct sk_buff *skb,
					const struct nlmsghdr *nlh,
					struct nlattr **tb,
					struct netlink_ext_ack *extack)
{
	struct rtmsg *rtm;
	int i, err;

	rtm = nlmsg_payload(nlh, sizeof(*rtm));
	if (!rtm) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid header for get route request");
		return -EINVAL;
	}

	if (!netlink_strict_get_check(skb))
		return nlmsg_parse_deprecated(nlh, sizeof(*rtm), tb, RTA_MAX,
					      rtm_ipv6_policy, extack);

	if ((rtm->rtm_src_len && rtm->rtm_src_len != 128) ||
	    (rtm->rtm_dst_len && rtm->rtm_dst_len != 128) ||
	    rtm->rtm_table || rtm->rtm_protocol || rtm->rtm_scope ||
	    rtm->rtm_type) {
		NL_SET_ERR_MSG_MOD(extack, "Invalid values in header for get route request");
		return -EINVAL;
	}
	if (rtm->rtm_flags & ~RTM_F_FIB_MATCH) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Invalid flags for get route request");
		return -EINVAL;
	}

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(*rtm), tb, RTA_MAX,
					    rtm_ipv6_policy, extack);
	if (err)
		return err;

	if ((tb[RTA_SRC] && !rtm->rtm_src_len) ||
	    (tb[RTA_DST] && !rtm->rtm_dst_len)) {
		NL_SET_ERR_MSG_MOD(extack, "rtm_src_len and rtm_dst_len must be 128 for IPv6");
		return -EINVAL;
	}

	if (tb[RTA_FLOWLABEL] &&
	    (nla_get_be32(tb[RTA_FLOWLABEL]) & ~IPV6_FLOWLABEL_MASK)) {
		NL_SET_ERR_MSG_ATTR(extack, tb[RTA_FLOWLABEL],
				    "Invalid flow label");
		return -EINVAL;
	}

	for (i = 0; i <= RTA_MAX; i++) {
		if (!tb[i])
			continue;

		switch (i) {
		case RTA_SRC:
		case RTA_DST:
		case RTA_IIF:
		case RTA_OIF:
		case RTA_MARK:
		case RTA_UID:
		case RTA_SPORT:
		case RTA_DPORT:
		case RTA_IP_PROTO:
		case RTA_FLOWLABEL:
			break;
		default:
			NL_SET_ERR_MSG_MOD(extack, "Unsupported attribute in get route request");
			return -EINVAL;
		}
	}

	return 0;
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
	struct flowi6 fl6 = {};
	__be32 flowlabel;
	bool fibmatch;

	err = inet6_rtm_valid_getroute_req(in_skb, nlh, tb, extack);
	if (err < 0)
		goto errout;

	err = -EINVAL;
	rtm = nlmsg_data(nlh);
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
						  &fl6.flowi6_proto, AF_INET6,
						  extack);
		if (err)
			goto errout;
	}

	flowlabel = nla_get_be32_default(tb[RTA_FLOWLABEL], 0);
	fl6.flowlabel = ip6_make_flowinfo(rtm->rtm_tos, flowlabel);

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


	rt = dst_rt6_info(dst);
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
	if (from) {
		if (fibmatch)
			err = rt6_fill_node(net, skb, from, NULL, NULL, NULL,
					    iif, RTM_NEWROUTE,
					    NETLINK_CB(in_skb).portid,
					    nlh->nlmsg_seq, 0);
		else
			err = rt6_fill_node(net, skb, from, dst, &fl6.daddr,
					    &fl6.saddr, iif, RTM_NEWROUTE,
					    NETLINK_CB(in_skb).portid,
					    nlh->nlmsg_seq, 0);
	} else {
		err = -ENETUNREACH;
	}
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
	struct net *net = info->nl_net;
	struct sk_buff *skb;
	size_t sz;
	u32 seq;
	int err;

	err = -ENOBUFS;
	seq = info->nlh ? info->nlh->nlmsg_seq : 0;

	rcu_read_lock();
	sz = rt6_nlmsg_size(rt);
retry:
	skb = nlmsg_new(sz, GFP_ATOMIC);
	if (!skb)
		goto errout;

	err = rt6_fill_node(net, skb, rt, NULL, NULL, NULL, 0,
			    event, info->portid, seq, nlm_flags);
	if (err < 0) {
		kfree_skb(skb);
		/* -EMSGSIZE implies needed space grew under us. */
		if (err == -EMSGSIZE) {
			sz = max(rt6_nlmsg_size(rt), sz << 1);
			goto retry;
		}
		goto errout;
	}

	rcu_read_unlock();

	rtnl_notify(skb, net, info->portid, RTNLGRP_IPV6_ROUTE,
		    info->nlh, GFP_ATOMIC);
	return;
errout:
	rcu_read_unlock();
	rtnl_set_sk_err(net, RTNLGRP_IPV6_ROUTE, err);
}

void fib6_rt_update(struct net *net, struct fib6_info *rt,
		    struct nl_info *info)
{
	u32 seq = info->nlh ? info->nlh->nlmsg_seq : 0;
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(rt6_nlmsg_size(rt), gfp_any());
	if (!skb)
		goto errout;

	err = rt6_fill_node(net, skb, rt, NULL, NULL, NULL, 0,
			    RTM_NEWROUTE, info->portid, seq, NLM_F_REPLACE);
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
	rtnl_set_sk_err(net, RTNLGRP_IPV6_ROUTE, err);
}

void fib6_info_hw_flags_set(struct net *net, struct fib6_info *f6i,
			    bool offload, bool trap, bool offload_failed)
{
	struct sk_buff *skb;
	int err;

	if (READ_ONCE(f6i->offload) == offload &&
	    READ_ONCE(f6i->trap) == trap &&
	    READ_ONCE(f6i->offload_failed) == offload_failed)
		return;

	WRITE_ONCE(f6i->offload, offload);
	WRITE_ONCE(f6i->trap, trap);

	/* 2 means send notifications only if offload_failed was changed. */
	if (net->ipv6.sysctl.fib_notify_on_flag_change == 2 &&
	    READ_ONCE(f6i->offload_failed) == offload_failed)
		return;

	WRITE_ONCE(f6i->offload_failed, offload_failed);

	if (!rcu_access_pointer(f6i->fib6_node))
		/* The route was removed from the tree, do not send
		 * notification.
		 */
		return;

	if (!net->ipv6.sysctl.fib_notify_on_flag_change)
		return;

	skb = nlmsg_new(rt6_nlmsg_size(f6i), GFP_KERNEL);
	if (!skb) {
		err = -ENOBUFS;
		goto errout;
	}

	err = rt6_fill_node(net, skb, f6i, NULL, NULL, NULL, 0, RTM_NEWROUTE, 0,
			    0, 0);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in rt6_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}

	rtnl_notify(skb, net, 0, RTNLGRP_IPV6_ROUTE, NULL, GFP_KERNEL);
	return;

errout:
	rtnl_set_sk_err(net, RTNLGRP_IPV6_ROUTE, err);
}
EXPORT_SYMBOL(fib6_info_hw_flags_set);

static int ip6_route_dev_notify(struct notifier_block *this,
				unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct net *net = dev_net(dev);

	if (!(dev->flags & IFF_LOOPBACK))
		return NOTIFY_OK;

	if (event == NETDEV_REGISTER) {
		net->ipv6.fib6_null_entry->fib6_nh->fib_nh_dev = dev;
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

static int ipv6_sysctl_rtcache_flush(const struct ctl_table *ctl, int write,
			      void *buffer, size_t *lenp, loff_t *ppos)
{
	struct net *net;
	int delay;
	int ret;
	if (!write)
		return -EINVAL;

	ret = proc_dointvec(ctl, write, buffer, lenp, ppos);
	if (ret)
		return ret;

	net = (struct net *)ctl->extra1;
	delay = net->ipv6.sysctl.flush_delay;
	fib6_run_gc(delay <= 0 ? 0 : (unsigned long)delay, net, delay > 0);
	return 0;
}

static struct ctl_table ipv6_route_table_template[] = {
	{
		.procname	=	"max_size",
		.data		=	&init_net.ipv6.sysctl.ip6_rt_max_size,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec,
	},
	{
		.procname	=	"gc_thresh",
		.data		=	&ip6_dst_ops_template.gc_thresh,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	proc_dointvec,
	},
	{
		.procname	=	"flush",
		.data		=	&init_net.ipv6.sysctl.flush_delay,
		.maxlen		=	sizeof(int),
		.mode		=	0200,
		.proc_handler	=	ipv6_sysctl_rtcache_flush
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
	{
		.procname	=	"skip_notify_on_dev_down",
		.data		=	&init_net.ipv6.sysctl.skip_notify_on_dev_down,
		.maxlen		=	sizeof(u8),
		.mode		=	0644,
		.proc_handler	=	proc_dou8vec_minmax,
		.extra1		=	SYSCTL_ZERO,
		.extra2		=	SYSCTL_ONE,
	},
};

struct ctl_table * __net_init ipv6_route_sysctl_init(struct net *net)
{
	struct ctl_table *table;

	table = kmemdup(ipv6_route_table_template,
			sizeof(ipv6_route_table_template),
			GFP_KERNEL);

	if (table) {
		table[0].data = &net->ipv6.sysctl.ip6_rt_max_size;
		table[1].data = &net->ipv6.ip6_dst_ops.gc_thresh;
		table[2].data = &net->ipv6.sysctl.flush_delay;
		table[2].extra1 = net;
		table[3].data = &net->ipv6.sysctl.ip6_rt_gc_min_interval;
		table[4].data = &net->ipv6.sysctl.ip6_rt_gc_timeout;
		table[5].data = &net->ipv6.sysctl.ip6_rt_gc_interval;
		table[6].data = &net->ipv6.sysctl.ip6_rt_gc_elasticity;
		table[7].data = &net->ipv6.sysctl.ip6_rt_mtu_expires;
		table[8].data = &net->ipv6.sysctl.ip6_rt_min_advmss;
		table[9].data = &net->ipv6.sysctl.ip6_rt_gc_min_interval;
		table[10].data = &net->ipv6.sysctl.skip_notify_on_dev_down;
	}

	return table;
}

size_t ipv6_route_sysctl_table_size(struct net *net)
{
	/* Don't export sysctls to unprivileged users */
	if (net->user_ns != &init_user_ns)
		return 1;

	return ARRAY_SIZE(ipv6_route_table_template);
}
#endif

static int __net_init ip6_route_net_init(struct net *net)
{
	int ret = -ENOMEM;

	memcpy(&net->ipv6.ip6_dst_ops, &ip6_dst_ops_template,
	       sizeof(net->ipv6.ip6_dst_ops));

	if (dst_entries_init(&net->ipv6.ip6_dst_ops) < 0)
		goto out_ip6_dst_ops;

	net->ipv6.fib6_null_entry = fib6_info_alloc(GFP_KERNEL, true);
	if (!net->ipv6.fib6_null_entry)
		goto out_ip6_dst_entries;
	memcpy(net->ipv6.fib6_null_entry, &fib6_null_entry_template,
	       sizeof(*net->ipv6.fib6_null_entry));

	net->ipv6.ip6_null_entry = kmemdup(&ip6_null_entry_template,
					   sizeof(*net->ipv6.ip6_null_entry),
					   GFP_KERNEL);
	if (!net->ipv6.ip6_null_entry)
		goto out_fib6_null_entry;
	net->ipv6.ip6_null_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_null_entry->dst,
			 ip6_template_metrics, true);
	INIT_LIST_HEAD(&net->ipv6.ip6_null_entry->dst.rt_uncached);

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
	INIT_LIST_HEAD(&net->ipv6.ip6_prohibit_entry->dst.rt_uncached);

	net->ipv6.ip6_blk_hole_entry = kmemdup(&ip6_blk_hole_entry_template,
					       sizeof(*net->ipv6.ip6_blk_hole_entry),
					       GFP_KERNEL);
	if (!net->ipv6.ip6_blk_hole_entry)
		goto out_ip6_prohibit_entry;
	net->ipv6.ip6_blk_hole_entry->dst.ops = &net->ipv6.ip6_dst_ops;
	dst_init_metrics(&net->ipv6.ip6_blk_hole_entry->dst,
			 ip6_template_metrics, true);
	INIT_LIST_HEAD(&net->ipv6.ip6_blk_hole_entry->dst.rt_uncached);
#ifdef CONFIG_IPV6_SUBTREES
	net->ipv6.fib6_routes_require_src = 0;
#endif
#endif

	net->ipv6.sysctl.flush_delay = 0;
	net->ipv6.sysctl.ip6_rt_max_size = INT_MAX;
	net->ipv6.sysctl.ip6_rt_gc_min_interval = HZ / 2;
	net->ipv6.sysctl.ip6_rt_gc_timeout = 60*HZ;
	net->ipv6.sysctl.ip6_rt_gc_interval = 30*HZ;
	net->ipv6.sysctl.ip6_rt_gc_elasticity = 9;
	net->ipv6.sysctl.ip6_rt_mtu_expires = 10*60*HZ;
	net->ipv6.sysctl.ip6_rt_min_advmss = IPV6_MIN_MTU - 20 - 40;
	net->ipv6.sysctl.skip_notify_on_dev_down = 0;

	atomic_set(&net->ipv6.ip6_rt_gc_expire, 30*HZ);

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
	if (!proc_create_net("ipv6_route", 0, net->proc_net,
			     &ipv6_route_seq_ops,
			     sizeof(struct ipv6_route_iter)))
		return -ENOMEM;

	if (!proc_create_net_single("rt6_stats", 0444, net->proc_net,
				    rt6_stats_seq_show, NULL)) {
		remove_proc_entry("ipv6_route", net->proc_net);
		return -ENOMEM;
	}
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
	init_net.ipv6.fib6_null_entry->fib6_nh->fib_nh_dev = init_net.loopback_dev;
	init_net.ipv6.ip6_null_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_null_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
  #ifdef CONFIG_IPV6_MULTIPLE_TABLES
	init_net.ipv6.ip6_prohibit_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_prohibit_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
	init_net.ipv6.ip6_blk_hole_entry->dst.dev = init_net.loopback_dev;
	init_net.ipv6.ip6_blk_hole_entry->rt6i_idev = in6_dev_get(init_net.loopback_dev);
  #endif
}

#if IS_BUILTIN(CONFIG_IPV6)
#if defined(CONFIG_BPF_SYSCALL) && defined(CONFIG_PROC_FS)
DEFINE_BPF_ITER_FUNC(ipv6_route, struct bpf_iter_meta *meta, struct fib6_info *rt)

BTF_ID_LIST_SINGLE(btf_fib6_info_id, struct, fib6_info)

static const struct bpf_iter_seq_info ipv6_route_seq_info = {
	.seq_ops		= &ipv6_route_seq_ops,
	.init_seq_private	= bpf_iter_init_seq_net,
	.fini_seq_private	= bpf_iter_fini_seq_net,
	.seq_priv_size		= sizeof(struct ipv6_route_iter),
};

static struct bpf_iter_reg ipv6_route_reg_info = {
	.target			= "ipv6_route",
	.ctx_arg_info_size	= 1,
	.ctx_arg_info		= {
		{ offsetof(struct bpf_iter__ipv6_route, rt),
		  PTR_TO_BTF_ID_OR_NULL },
	},
	.seq_info		= &ipv6_route_seq_info,
};

static int __init bpf_iter_register(void)
{
	ipv6_route_reg_info.ctx_arg_info[0].btf_id = *btf_fib6_info_id;
	return bpf_iter_reg_target(&ipv6_route_reg_info);
}

static void bpf_iter_unregister(void)
{
	bpf_iter_unreg_target(&ipv6_route_reg_info);
}
#endif
#endif

static const struct rtnl_msg_handler ip6_route_rtnl_msg_handlers[] __initconst_or_module = {
	{.owner = THIS_MODULE, .protocol = PF_INET6, .msgtype = RTM_NEWROUTE,
	 .doit = inet6_rtm_newroute, .flags = RTNL_FLAG_DOIT_UNLOCKED},
	{.owner = THIS_MODULE, .protocol = PF_INET6, .msgtype = RTM_DELROUTE,
	 .doit = inet6_rtm_delroute, .flags = RTNL_FLAG_DOIT_UNLOCKED},
	{.owner = THIS_MODULE, .protocol = PF_INET6, .msgtype = RTM_GETROUTE,
	 .doit = inet6_rtm_getroute, .flags = RTNL_FLAG_DOIT_UNLOCKED},
};

int __init ip6_route_init(void)
{
	int ret;
	int cpu;

	ret = -ENOMEM;
	ip6_dst_ops_template.kmem_cachep =
		kmem_cache_create("ip6_dst_cache", sizeof(struct rt6_info), 0,
				  SLAB_HWCACHE_ALIGN | SLAB_ACCOUNT, NULL);
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

	ret = rtnl_register_many(ip6_route_rtnl_msg_handlers);
	if (ret < 0)
		goto out_register_late_subsys;

	ret = register_netdevice_notifier(&ip6_route_dev_notifier);
	if (ret)
		goto out_register_late_subsys;

#if IS_BUILTIN(CONFIG_IPV6)
#if defined(CONFIG_BPF_SYSCALL) && defined(CONFIG_PROC_FS)
	ret = bpf_iter_register();
	if (ret)
		goto out_register_late_subsys;
#endif
#endif

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
#if IS_BUILTIN(CONFIG_IPV6)
#if defined(CONFIG_BPF_SYSCALL) && defined(CONFIG_PROC_FS)
	bpf_iter_unregister();
#endif
#endif
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
