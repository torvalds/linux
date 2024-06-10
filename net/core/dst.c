// SPDX-License-Identifier: GPL-2.0-only
/*
 * net/core/dst.c	Protocol independent destination cache.
 *
 * Authors:		Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/net_namespace.h>
#include <linux/sched.h>
#include <linux/prefetch.h>
#include <net/lwtunnel.h>
#include <net/xfrm.h>

#include <net/dst.h>
#include <net/dst_metadata.h>

int dst_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL(dst_discard_out);

const struct dst_metrics dst_default_metrics = {
	/* This initializer is needed to force linker to place this variable
	 * into const section. Otherwise it might end into bss section.
	 * We really want to avoid false sharing on this variable, and catch
	 * any writes on it.
	 */
	.refcnt = REFCOUNT_INIT(1),
};
EXPORT_SYMBOL(dst_default_metrics);

void dst_init(struct dst_entry *dst, struct dst_ops *ops,
	      struct net_device *dev, int initial_obsolete,
	      unsigned short flags)
{
	dst->dev = dev;
	netdev_hold(dev, &dst->dev_tracker, GFP_ATOMIC);
	dst->ops = ops;
	dst_init_metrics(dst, dst_default_metrics.metrics, true);
	dst->expires = 0UL;
#ifdef CONFIG_XFRM
	dst->xfrm = NULL;
#endif
	dst->input = dst_discard;
	dst->output = dst_discard_out;
	dst->error = 0;
	dst->obsolete = initial_obsolete;
	dst->header_len = 0;
	dst->trailer_len = 0;
#ifdef CONFIG_IP_ROUTE_CLASSID
	dst->tclassid = 0;
#endif
	dst->lwtstate = NULL;
	rcuref_init(&dst->__rcuref, 1);
	INIT_LIST_HEAD(&dst->rt_uncached);
	dst->__use = 0;
	dst->lastuse = jiffies;
	dst->flags = flags;
	if (!(flags & DST_NOCOUNT))
		dst_entries_add(ops, 1);
}
EXPORT_SYMBOL(dst_init);

void *dst_alloc(struct dst_ops *ops, struct net_device *dev,
		int initial_obsolete, unsigned short flags)
{
	struct dst_entry *dst;

	if (ops->gc &&
	    !(flags & DST_NOCOUNT) &&
	    dst_entries_get_fast(ops) > ops->gc_thresh)
		ops->gc(ops);

	dst = kmem_cache_alloc(ops->kmem_cachep, GFP_ATOMIC);
	if (!dst)
		return NULL;

	dst_init(dst, ops, dev, initial_obsolete, flags);

	return dst;
}
EXPORT_SYMBOL(dst_alloc);

static void dst_destroy(struct dst_entry *dst)
{
	struct dst_entry *child = NULL;

	smp_rmb();

#ifdef CONFIG_XFRM
	if (dst->xfrm) {
		struct xfrm_dst *xdst = (struct xfrm_dst *) dst;

		child = xdst->child;
	}
#endif
	if (!(dst->flags & DST_NOCOUNT))
		dst_entries_add(dst->ops, -1);

	if (dst->ops->destroy)
		dst->ops->destroy(dst);
	netdev_put(dst->dev, &dst->dev_tracker);

	lwtstate_put(dst->lwtstate);

	if (dst->flags & DST_METADATA)
		metadata_dst_free((struct metadata_dst *)dst);
	else
		kmem_cache_free(dst->ops->kmem_cachep, dst);

	dst = child;
	if (dst)
		dst_release_immediate(dst);
}

static void dst_destroy_rcu(struct rcu_head *head)
{
	struct dst_entry *dst = container_of(head, struct dst_entry, rcu_head);

	dst_destroy(dst);
}

/* Operations to mark dst as DEAD and clean up the net device referenced
 * by dst:
 * 1. put the dst under blackhole interface and discard all tx/rx packets
 *    on this route.
 * 2. release the net_device
 * This function should be called when removing routes from the fib tree
 * in preparation for a NETDEV_DOWN/NETDEV_UNREGISTER event and also to
 * make the next dst_ops->check() fail.
 */
void dst_dev_put(struct dst_entry *dst)
{
	struct net_device *dev = dst->dev;

	dst->obsolete = DST_OBSOLETE_DEAD;
	if (dst->ops->ifdown)
		dst->ops->ifdown(dst, dev);
	dst->input = dst_discard;
	dst->output = dst_discard_out;
	dst->dev = blackhole_netdev;
	netdev_ref_replace(dev, blackhole_netdev, &dst->dev_tracker,
			   GFP_ATOMIC);
}
EXPORT_SYMBOL(dst_dev_put);

void dst_release(struct dst_entry *dst)
{
	if (dst && rcuref_put(&dst->__rcuref))
		call_rcu_hurry(&dst->rcu_head, dst_destroy_rcu);
}
EXPORT_SYMBOL(dst_release);

void dst_release_immediate(struct dst_entry *dst)
{
	if (dst && rcuref_put(&dst->__rcuref))
		dst_destroy(dst);
}
EXPORT_SYMBOL(dst_release_immediate);

u32 *dst_cow_metrics_generic(struct dst_entry *dst, unsigned long old)
{
	struct dst_metrics *p = kmalloc(sizeof(*p), GFP_ATOMIC);

	if (p) {
		struct dst_metrics *old_p = (struct dst_metrics *)__DST_METRICS_PTR(old);
		unsigned long prev, new;

		refcount_set(&p->refcnt, 1);
		memcpy(p->metrics, old_p->metrics, sizeof(p->metrics));

		new = (unsigned long) p;
		prev = cmpxchg(&dst->_metrics, old, new);

		if (prev != old) {
			kfree(p);
			p = (struct dst_metrics *)__DST_METRICS_PTR(prev);
			if (prev & DST_METRICS_READ_ONLY)
				p = NULL;
		} else if (prev & DST_METRICS_REFCOUNTED) {
			if (refcount_dec_and_test(&old_p->refcnt))
				kfree(old_p);
		}
	}
	BUILD_BUG_ON(offsetof(struct dst_metrics, metrics) != 0);
	return (u32 *)p;
}
EXPORT_SYMBOL(dst_cow_metrics_generic);

/* Caller asserts that dst_metrics_read_only(dst) is false.  */
void __dst_destroy_metrics_generic(struct dst_entry *dst, unsigned long old)
{
	unsigned long prev, new;

	new = ((unsigned long) &dst_default_metrics) | DST_METRICS_READ_ONLY;
	prev = cmpxchg(&dst->_metrics, old, new);
	if (prev == old)
		kfree(__DST_METRICS_PTR(old));
}
EXPORT_SYMBOL(__dst_destroy_metrics_generic);

struct dst_entry *dst_blackhole_check(struct dst_entry *dst, u32 cookie)
{
	return NULL;
}

u32 *dst_blackhole_cow_metrics(struct dst_entry *dst, unsigned long old)
{
	return NULL;
}

struct neighbour *dst_blackhole_neigh_lookup(const struct dst_entry *dst,
					     struct sk_buff *skb,
					     const void *daddr)
{
	return NULL;
}

void dst_blackhole_update_pmtu(struct dst_entry *dst, struct sock *sk,
			       struct sk_buff *skb, u32 mtu,
			       bool confirm_neigh)
{
}
EXPORT_SYMBOL_GPL(dst_blackhole_update_pmtu);

void dst_blackhole_redirect(struct dst_entry *dst, struct sock *sk,
			    struct sk_buff *skb)
{
}
EXPORT_SYMBOL_GPL(dst_blackhole_redirect);

unsigned int dst_blackhole_mtu(const struct dst_entry *dst)
{
	unsigned int mtu = dst_metric_raw(dst, RTAX_MTU);

	return mtu ? : dst->dev->mtu;
}
EXPORT_SYMBOL_GPL(dst_blackhole_mtu);

static struct dst_ops dst_blackhole_ops = {
	.family		= AF_UNSPEC,
	.neigh_lookup	= dst_blackhole_neigh_lookup,
	.check		= dst_blackhole_check,
	.cow_metrics	= dst_blackhole_cow_metrics,
	.update_pmtu	= dst_blackhole_update_pmtu,
	.redirect	= dst_blackhole_redirect,
	.mtu		= dst_blackhole_mtu,
};

static void __metadata_dst_init(struct metadata_dst *md_dst,
				enum metadata_type type, u8 optslen)
{
	struct dst_entry *dst;

	dst = &md_dst->dst;
	dst_init(dst, &dst_blackhole_ops, NULL, DST_OBSOLETE_NONE,
		 DST_METADATA | DST_NOCOUNT);
	memset(dst + 1, 0, sizeof(*md_dst) + optslen - sizeof(*dst));
	md_dst->type = type;
}

struct metadata_dst *metadata_dst_alloc(u8 optslen, enum metadata_type type,
					gfp_t flags)
{
	struct metadata_dst *md_dst;

	md_dst = kmalloc(sizeof(*md_dst) + optslen, flags);
	if (!md_dst)
		return NULL;

	__metadata_dst_init(md_dst, type, optslen);

	return md_dst;
}
EXPORT_SYMBOL_GPL(metadata_dst_alloc);

void metadata_dst_free(struct metadata_dst *md_dst)
{
#ifdef CONFIG_DST_CACHE
	if (md_dst->type == METADATA_IP_TUNNEL)
		dst_cache_destroy(&md_dst->u.tun_info.dst_cache);
#endif
	if (md_dst->type == METADATA_XFRM)
		dst_release(md_dst->u.xfrm_info.dst_orig);
	kfree(md_dst);
}
EXPORT_SYMBOL_GPL(metadata_dst_free);

struct metadata_dst __percpu *
metadata_dst_alloc_percpu(u8 optslen, enum metadata_type type, gfp_t flags)
{
	int cpu;
	struct metadata_dst __percpu *md_dst;

	md_dst = __alloc_percpu_gfp(sizeof(struct metadata_dst) + optslen,
				    __alignof__(struct metadata_dst), flags);
	if (!md_dst)
		return NULL;

	for_each_possible_cpu(cpu)
		__metadata_dst_init(per_cpu_ptr(md_dst, cpu), type, optslen);

	return md_dst;
}
EXPORT_SYMBOL_GPL(metadata_dst_alloc_percpu);

void metadata_dst_free_percpu(struct metadata_dst __percpu *md_dst)
{
	int cpu;

	for_each_possible_cpu(cpu) {
		struct metadata_dst *one_md_dst = per_cpu_ptr(md_dst, cpu);

#ifdef CONFIG_DST_CACHE
		if (one_md_dst->type == METADATA_IP_TUNNEL)
			dst_cache_destroy(&one_md_dst->u.tun_info.dst_cache);
#endif
		if (one_md_dst->type == METADATA_XFRM)
			dst_release(one_md_dst->u.xfrm_info.dst_orig);
	}
	free_percpu(md_dst);
}
EXPORT_SYMBOL_GPL(metadata_dst_free_percpu);
