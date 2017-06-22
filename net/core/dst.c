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

#include <net/dst.h>
#include <net/dst_metadata.h>

/*
 * Theory of operations:
 * 1) We use a list, protected by a spinlock, to add
 *    new entries from both BH and non-BH context.
 * 2) In order to keep spinlock held for a small delay,
 *    we use a second list where are stored long lived
 *    entries, that are handled by the garbage collect thread
 *    fired by a workqueue.
 * 3) This list is guarded by a mutex,
 *    so that the gc_task and dst_dev_event() can be synchronized.
 */

/*
 * We want to keep lock & list close together
 * to dirty as few cache lines as possible in __dst_free().
 * As this is not a very strong hint, we dont force an alignment on SMP.
 */
static struct {
	spinlock_t		lock;
	struct dst_entry	*list;
	unsigned long		timer_inc;
	unsigned long		timer_expires;
} dst_garbage = {
	.lock = __SPIN_LOCK_UNLOCKED(dst_garbage.lock),
	.timer_inc = DST_GC_MAX,
};
static void dst_gc_task(struct work_struct *work);
static void ___dst_free(struct dst_entry *dst);

static DECLARE_DELAYED_WORK(dst_gc_work, dst_gc_task);

static DEFINE_MUTEX(dst_gc_mutex);
/*
 * long lived entries are maintained in this list, guarded by dst_gc_mutex
 */
static struct dst_entry         *dst_busy_list;

static void dst_gc_task(struct work_struct *work)
{
	int    delayed = 0;
	int    work_performed = 0;
	unsigned long expires = ~0L;
	struct dst_entry *dst, *next, head;
	struct dst_entry *last = &head;

	mutex_lock(&dst_gc_mutex);
	next = dst_busy_list;

loop:
	while ((dst = next) != NULL) {
		next = dst->next;
		prefetch(&next->next);
		cond_resched();
		if (likely(atomic_read(&dst->__refcnt))) {
			last->next = dst;
			last = dst;
			delayed++;
			continue;
		}
		work_performed++;

		dst = dst_destroy(dst);
		if (dst) {
			/* NOHASH and still referenced. Unless it is already
			 * on gc list, invalidate it and add to gc list.
			 *
			 * Note: this is temporary. Actually, NOHASH dst's
			 * must be obsoleted when parent is obsoleted.
			 * But we do not have state "obsoleted, but
			 * referenced by parent", so it is right.
			 */
			if (dst->obsolete > 0)
				continue;

			___dst_free(dst);
			dst->next = next;
			next = dst;
		}
	}

	spin_lock_bh(&dst_garbage.lock);
	next = dst_garbage.list;
	if (next) {
		dst_garbage.list = NULL;
		spin_unlock_bh(&dst_garbage.lock);
		goto loop;
	}
	last->next = NULL;
	dst_busy_list = head.next;
	if (!dst_busy_list)
		dst_garbage.timer_inc = DST_GC_MAX;
	else {
		/*
		 * if we freed less than 1/10 of delayed entries,
		 * we can sleep longer.
		 */
		if (work_performed <= delayed/10) {
			dst_garbage.timer_expires += dst_garbage.timer_inc;
			if (dst_garbage.timer_expires > DST_GC_MAX)
				dst_garbage.timer_expires = DST_GC_MAX;
			dst_garbage.timer_inc += DST_GC_INC;
		} else {
			dst_garbage.timer_inc = DST_GC_INC;
			dst_garbage.timer_expires = DST_GC_MIN;
		}
		expires = dst_garbage.timer_expires;
		/*
		 * if the next desired timer is more than 4 seconds in the
		 * future then round the timer to whole seconds
		 */
		if (expires > 4*HZ)
			expires = round_jiffies_relative(expires);
		schedule_delayed_work(&dst_gc_work, expires);
	}

	spin_unlock_bh(&dst_garbage.lock);
	mutex_unlock(&dst_gc_mutex);
}

int dst_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	kfree_skb(skb);
	return 0;
}
EXPORT_SYMBOL(dst_discard_out);

const u32 dst_default_metrics[RTAX_MAX + 1] = {
	/* This initializer is needed to force linker to place this variable
	 * into const section. Otherwise it might end into bss section.
	 * We really want to avoid false sharing on this variable, and catch
	 * any writes on it.
	 */
	[RTAX_MAX] = 0xdeadbeef,
};

void dst_init(struct dst_entry *dst, struct dst_ops *ops,
	      struct net_device *dev, int initial_ref, int initial_obsolete,
	      unsigned short flags)
{
	dst->child = NULL;
	dst->dev = dev;
	if (dev)
		dev_hold(dev);
	dst->ops = ops;
	dst_init_metrics(dst, dst_default_metrics, true);
	dst->expires = 0UL;
	dst->path = dst;
	dst->from = NULL;
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
	atomic_set(&dst->__refcnt, initial_ref);
	dst->__use = 0;
	dst->lastuse = jiffies;
	dst->flags = flags;
	dst->next = NULL;
	if (!(flags & DST_NOCOUNT))
		dst_entries_add(ops, 1);
}
EXPORT_SYMBOL(dst_init);

void *dst_alloc(struct dst_ops *ops, struct net_device *dev,
		int initial_ref, int initial_obsolete, unsigned short flags)
{
	struct dst_entry *dst;

	if (ops->gc && dst_entries_get_fast(ops) > ops->gc_thresh) {
		if (ops->gc(ops))
			return NULL;
	}

	dst = kmem_cache_alloc(ops->kmem_cachep, GFP_ATOMIC);
	if (!dst)
		return NULL;

	dst_init(dst, ops, dev, initial_ref, initial_obsolete, flags);

	return dst;
}
EXPORT_SYMBOL(dst_alloc);

static void ___dst_free(struct dst_entry *dst)
{
	/* The first case (dev==NULL) is required, when
	   protocol module is unloaded.
	 */
	if (dst->dev == NULL || !(dst->dev->flags&IFF_UP)) {
		dst->input = dst_discard;
		dst->output = dst_discard_out;
	}
	dst->obsolete = DST_OBSOLETE_DEAD;
}

void __dst_free(struct dst_entry *dst)
{
	spin_lock_bh(&dst_garbage.lock);
	___dst_free(dst);
	dst->next = dst_garbage.list;
	dst_garbage.list = dst;
	if (dst_garbage.timer_inc > DST_GC_INC) {
		dst_garbage.timer_inc = DST_GC_INC;
		dst_garbage.timer_expires = DST_GC_MIN;
		mod_delayed_work(system_wq, &dst_gc_work,
				 dst_garbage.timer_expires);
	}
	spin_unlock_bh(&dst_garbage.lock);
}
EXPORT_SYMBOL(__dst_free);

struct dst_entry *dst_destroy(struct dst_entry * dst)
{
	struct dst_entry *child;

	smp_rmb();

again:
	child = dst->child;

	if (!(dst->flags & DST_NOCOUNT))
		dst_entries_add(dst->ops, -1);

	if (dst->ops->destroy)
		dst->ops->destroy(dst);
	if (dst->dev)
		dev_put(dst->dev);

	lwtstate_put(dst->lwtstate);

	if (dst->flags & DST_METADATA)
		metadata_dst_free((struct metadata_dst *)dst);
	else
		kmem_cache_free(dst->ops->kmem_cachep, dst);

	dst = child;
	if (dst) {
		int nohash = dst->flags & DST_NOHASH;

		if (atomic_dec_and_test(&dst->__refcnt)) {
			/* We were real parent of this dst, so kill child. */
			if (nohash)
				goto again;
		} else {
			/* Child is still referenced, return it for freeing. */
			if (nohash)
				return dst;
			/* Child is still in his hash table */
		}
	}
	return NULL;
}
EXPORT_SYMBOL(dst_destroy);

static void dst_destroy_rcu(struct rcu_head *head)
{
	struct dst_entry *dst = container_of(head, struct dst_entry, rcu_head);

	dst = dst_destroy(dst);
	if (dst)
		__dst_free(dst);
}

void dst_release(struct dst_entry *dst)
{
	if (dst) {
		int newrefcnt;
		unsigned short nocache = dst->flags & DST_NOCACHE;

		newrefcnt = atomic_dec_return(&dst->__refcnt);
		if (unlikely(newrefcnt < 0))
			net_warn_ratelimited("%s: dst:%p refcnt:%d\n",
					     __func__, dst, newrefcnt);
		if (!newrefcnt && unlikely(nocache))
			call_rcu(&dst->rcu_head, dst_destroy_rcu);
	}
}
EXPORT_SYMBOL(dst_release);

u32 *dst_cow_metrics_generic(struct dst_entry *dst, unsigned long old)
{
	u32 *p = kmalloc(sizeof(u32) * RTAX_MAX, GFP_ATOMIC);

	if (p) {
		u32 *old_p = __DST_METRICS_PTR(old);
		unsigned long prev, new;

		memcpy(p, old_p, sizeof(u32) * RTAX_MAX);

		new = (unsigned long) p;
		prev = cmpxchg(&dst->_metrics, old, new);

		if (prev != old) {
			kfree(p);
			p = __DST_METRICS_PTR(prev);
			if (prev & DST_METRICS_READ_ONLY)
				p = NULL;
		}
	}
	return p;
}
EXPORT_SYMBOL(dst_cow_metrics_generic);

/* Caller asserts that dst_metrics_read_only(dst) is false.  */
void __dst_destroy_metrics_generic(struct dst_entry *dst, unsigned long old)
{
	unsigned long prev, new;

	new = ((unsigned long) dst_default_metrics) | DST_METRICS_READ_ONLY;
	prev = cmpxchg(&dst->_metrics, old, new);
	if (prev == old)
		kfree(__DST_METRICS_PTR(old));
}
EXPORT_SYMBOL(__dst_destroy_metrics_generic);

static struct dst_ops md_dst_ops = {
	.family =		AF_UNSPEC,
};

static int dst_md_discard_out(struct net *net, struct sock *sk, struct sk_buff *skb)
{
	WARN_ONCE(1, "Attempting to call output on metadata dst\n");
	kfree_skb(skb);
	return 0;
}

static int dst_md_discard(struct sk_buff *skb)
{
	WARN_ONCE(1, "Attempting to call input on metadata dst\n");
	kfree_skb(skb);
	return 0;
}

static void __metadata_dst_init(struct metadata_dst *md_dst, u8 optslen)
{
	struct dst_entry *dst;

	dst = &md_dst->dst;
	dst_init(dst, &md_dst_ops, NULL, 1, DST_OBSOLETE_NONE,
		 DST_METADATA | DST_NOCACHE | DST_NOCOUNT);

	dst->input = dst_md_discard;
	dst->output = dst_md_discard_out;

	memset(dst + 1, 0, sizeof(*md_dst) + optslen - sizeof(*dst));
}

struct metadata_dst *metadata_dst_alloc(u8 optslen, gfp_t flags)
{
	struct metadata_dst *md_dst;

	md_dst = kmalloc(sizeof(*md_dst) + optslen, flags);
	if (!md_dst)
		return NULL;

	__metadata_dst_init(md_dst, optslen);

	return md_dst;
}
EXPORT_SYMBOL_GPL(metadata_dst_alloc);

void metadata_dst_free(struct metadata_dst *md_dst)
{
#ifdef CONFIG_DST_CACHE
	dst_cache_destroy(&md_dst->u.tun_info.dst_cache);
#endif
	kfree(md_dst);
}

struct metadata_dst __percpu *metadata_dst_alloc_percpu(u8 optslen, gfp_t flags)
{
	int cpu;
	struct metadata_dst __percpu *md_dst;

	md_dst = __alloc_percpu_gfp(sizeof(struct metadata_dst) + optslen,
				    __alignof__(struct metadata_dst), flags);
	if (!md_dst)
		return NULL;

	for_each_possible_cpu(cpu)
		__metadata_dst_init(per_cpu_ptr(md_dst, cpu), optslen);

	return md_dst;
}
EXPORT_SYMBOL_GPL(metadata_dst_alloc_percpu);

/* Dirty hack. We did it in 2.2 (in __dst_free),
 * we have _very_ good reasons not to repeat
 * this mistake in 2.3, but we have no choice
 * now. _It_ _is_ _explicit_ _deliberate_
 * _race_ _condition_.
 *
 * Commented and originally written by Alexey.
 */
static void dst_ifdown(struct dst_entry *dst, struct net_device *dev,
		       int unregister)
{
	if (dst->ops->ifdown)
		dst->ops->ifdown(dst, dev, unregister);

	if (dev != dst->dev)
		return;

	if (!unregister) {
		dst->input = dst_discard;
		dst->output = dst_discard_out;
	} else {
		dst->dev = dev_net(dst->dev)->loopback_dev;
		dev_hold(dst->dev);
		dev_put(dev);
	}
}

static int dst_dev_event(struct notifier_block *this, unsigned long event,
			 void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct dst_entry *dst, *last = NULL;

	switch (event) {
	case NETDEV_UNREGISTER_FINAL:
	case NETDEV_DOWN:
		mutex_lock(&dst_gc_mutex);
		for (dst = dst_busy_list; dst; dst = dst->next) {
			last = dst;
			dst_ifdown(dst, dev, event != NETDEV_DOWN);
		}

		spin_lock_bh(&dst_garbage.lock);
		dst = dst_garbage.list;
		dst_garbage.list = NULL;
		spin_unlock_bh(&dst_garbage.lock);

		if (last)
			last->next = dst;
		else
			dst_busy_list = dst;
		for (; dst; dst = dst->next)
			dst_ifdown(dst, dev, event != NETDEV_DOWN);
		mutex_unlock(&dst_gc_mutex);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block dst_dev_notifier = {
	.notifier_call	= dst_dev_event,
	.priority = -10, /* must be called after other network notifiers */
};

void __init dst_subsys_init(void)
{
	register_netdevice_notifier(&dst_dev_notifier);
}
