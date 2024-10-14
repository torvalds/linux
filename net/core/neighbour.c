// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *	Generic address resolution entity
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>
 *	Alexey Kuznetsov	<kuznet@ms2.inr.ac.ru>
 *
 *	Fixes:
 *	Vitaly E. Lavrov	releasing NULL neighbor in neigh_add.
 *	Harald Welte		Add neighbour cache statistics like rtstat
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/kmemleak.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/socket.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <linux/times.h>
#include <net/net_namespace.h>
#include <net/neighbour.h>
#include <net/arp.h>
#include <net/dst.h>
#include <net/sock.h>
#include <net/netevent.h>
#include <net/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/log2.h>
#include <linux/inetdevice.h>
#include <net/addrconf.h>

#include <trace/events/neigh.h>

#define NEIGH_DEBUG 1
#define neigh_dbg(level, fmt, ...)		\
do {						\
	if (level <= NEIGH_DEBUG)		\
		pr_debug(fmt, ##__VA_ARGS__);	\
} while (0)

#define PNEIGH_HASHMASK		0xF

static void neigh_timer_handler(struct timer_list *t);
static void __neigh_notify(struct neighbour *n, int type, int flags,
			   u32 pid);
static void neigh_update_notify(struct neighbour *neigh, u32 nlmsg_pid);
static int pneigh_ifdown_and_unlock(struct neigh_table *tbl,
				    struct net_device *dev);

#ifdef CONFIG_PROC_FS
static const struct seq_operations neigh_stat_seq_ops;
#endif

/*
   Neighbour hash table buckets are protected with rwlock tbl->lock.

   - All the scans/updates to hash buckets MUST be made under this lock.
   - NOTHING clever should be made under this lock: no callbacks
     to protocol backends, no attempts to send something to network.
     It will result in deadlocks, if backend/driver wants to use neighbour
     cache.
   - If the entry requires some non-trivial actions, increase
     its reference count and release table lock.

   Neighbour entries are protected:
   - with reference count.
   - with rwlock neigh->lock

   Reference count prevents destruction.

   neigh->lock mainly serializes ll address data and its validity state.
   However, the same lock is used to protect another entry fields:
    - timer
    - resolution queue

   Again, nothing clever shall be made under neigh->lock,
   the most complicated procedure, which we allow is dev->hard_header.
   It is supposed, that dev->hard_header is simplistic and does
   not make callbacks to neighbour tables.
 */

static int neigh_blackhole(struct neighbour *neigh, struct sk_buff *skb)
{
	kfree_skb(skb);
	return -ENETDOWN;
}

static void neigh_cleanup_and_release(struct neighbour *neigh)
{
	trace_neigh_cleanup_and_release(neigh, 0);
	__neigh_notify(neigh, RTM_DELNEIGH, 0, 0);
	call_netevent_notifiers(NETEVENT_NEIGH_UPDATE, neigh);
	neigh_release(neigh);
}

/*
 * It is random distribution in the interval (1/2)*base...(3/2)*base.
 * It corresponds to default IPv6 settings and is not overridable,
 * because it is really reasonable choice.
 */

unsigned long neigh_rand_reach_time(unsigned long base)
{
	return base ? get_random_u32_below(base) + (base >> 1) : 0;
}
EXPORT_SYMBOL(neigh_rand_reach_time);

static void neigh_mark_dead(struct neighbour *n)
{
	n->dead = 1;
	if (!list_empty(&n->gc_list)) {
		list_del_init(&n->gc_list);
		atomic_dec(&n->tbl->gc_entries);
	}
	if (!list_empty(&n->managed_list))
		list_del_init(&n->managed_list);
}

static void neigh_update_gc_list(struct neighbour *n)
{
	bool on_gc_list, exempt_from_gc;

	write_lock_bh(&n->tbl->lock);
	write_lock(&n->lock);
	if (n->dead)
		goto out;

	/* remove from the gc list if new state is permanent or if neighbor
	 * is externally learned; otherwise entry should be on the gc list
	 */
	exempt_from_gc = n->nud_state & NUD_PERMANENT ||
			 n->flags & NTF_EXT_LEARNED;
	on_gc_list = !list_empty(&n->gc_list);

	if (exempt_from_gc && on_gc_list) {
		list_del_init(&n->gc_list);
		atomic_dec(&n->tbl->gc_entries);
	} else if (!exempt_from_gc && !on_gc_list) {
		/* add entries to the tail; cleaning removes from the front */
		list_add_tail(&n->gc_list, &n->tbl->gc_list);
		atomic_inc(&n->tbl->gc_entries);
	}
out:
	write_unlock(&n->lock);
	write_unlock_bh(&n->tbl->lock);
}

static void neigh_update_managed_list(struct neighbour *n)
{
	bool on_managed_list, add_to_managed;

	write_lock_bh(&n->tbl->lock);
	write_lock(&n->lock);
	if (n->dead)
		goto out;

	add_to_managed = n->flags & NTF_MANAGED;
	on_managed_list = !list_empty(&n->managed_list);

	if (!add_to_managed && on_managed_list)
		list_del_init(&n->managed_list);
	else if (add_to_managed && !on_managed_list)
		list_add_tail(&n->managed_list, &n->tbl->managed_list);
out:
	write_unlock(&n->lock);
	write_unlock_bh(&n->tbl->lock);
}

static void neigh_update_flags(struct neighbour *neigh, u32 flags, int *notify,
			       bool *gc_update, bool *managed_update)
{
	u32 ndm_flags, old_flags = neigh->flags;

	if (!(flags & NEIGH_UPDATE_F_ADMIN))
		return;

	ndm_flags  = (flags & NEIGH_UPDATE_F_EXT_LEARNED) ? NTF_EXT_LEARNED : 0;
	ndm_flags |= (flags & NEIGH_UPDATE_F_MANAGED) ? NTF_MANAGED : 0;

	if ((old_flags ^ ndm_flags) & NTF_EXT_LEARNED) {
		if (ndm_flags & NTF_EXT_LEARNED)
			neigh->flags |= NTF_EXT_LEARNED;
		else
			neigh->flags &= ~NTF_EXT_LEARNED;
		*notify = 1;
		*gc_update = true;
	}
	if ((old_flags ^ ndm_flags) & NTF_MANAGED) {
		if (ndm_flags & NTF_MANAGED)
			neigh->flags |= NTF_MANAGED;
		else
			neigh->flags &= ~NTF_MANAGED;
		*notify = 1;
		*managed_update = true;
	}
}

static bool neigh_del(struct neighbour *n, struct neighbour __rcu **np,
		      struct neigh_table *tbl)
{
	bool retval = false;

	write_lock(&n->lock);
	if (refcount_read(&n->refcnt) == 1) {
		struct neighbour *neigh;

		neigh = rcu_dereference_protected(n->next,
						  lockdep_is_held(&tbl->lock));
		rcu_assign_pointer(*np, neigh);
		neigh_mark_dead(n);
		retval = true;
	}
	write_unlock(&n->lock);
	if (retval)
		neigh_cleanup_and_release(n);
	return retval;
}

bool neigh_remove_one(struct neighbour *ndel, struct neigh_table *tbl)
{
	struct neigh_hash_table *nht;
	void *pkey = ndel->primary_key;
	u32 hash_val;
	struct neighbour *n;
	struct neighbour __rcu **np;

	nht = rcu_dereference_protected(tbl->nht,
					lockdep_is_held(&tbl->lock));
	hash_val = tbl->hash(pkey, ndel->dev, nht->hash_rnd);
	hash_val = hash_val >> (32 - nht->hash_shift);

	np = &nht->hash_buckets[hash_val];
	while ((n = rcu_dereference_protected(*np,
					      lockdep_is_held(&tbl->lock)))) {
		if (n == ndel)
			return neigh_del(n, np, tbl);
		np = &n->next;
	}
	return false;
}

static int neigh_forced_gc(struct neigh_table *tbl)
{
	int max_clean = atomic_read(&tbl->gc_entries) -
			READ_ONCE(tbl->gc_thresh2);
	u64 tmax = ktime_get_ns() + NSEC_PER_MSEC;
	unsigned long tref = jiffies - 5 * HZ;
	struct neighbour *n, *tmp;
	int shrunk = 0;
	int loop = 0;

	NEIGH_CACHE_STAT_INC(tbl, forced_gc_runs);

	write_lock_bh(&tbl->lock);

	list_for_each_entry_safe(n, tmp, &tbl->gc_list, gc_list) {
		if (refcount_read(&n->refcnt) == 1) {
			bool remove = false;

			write_lock(&n->lock);
			if ((n->nud_state == NUD_FAILED) ||
			    (n->nud_state == NUD_NOARP) ||
			    (tbl->is_multicast &&
			     tbl->is_multicast(n->primary_key)) ||
			    !time_in_range(n->updated, tref, jiffies))
				remove = true;
			write_unlock(&n->lock);

			if (remove && neigh_remove_one(n, tbl))
				shrunk++;
			if (shrunk >= max_clean)
				break;
			if (++loop == 16) {
				if (ktime_get_ns() > tmax)
					goto unlock;
				loop = 0;
			}
		}
	}

	WRITE_ONCE(tbl->last_flush, jiffies);
unlock:
	write_unlock_bh(&tbl->lock);

	return shrunk;
}

static void neigh_add_timer(struct neighbour *n, unsigned long when)
{
	/* Use safe distance from the jiffies - LONG_MAX point while timer
	 * is running in DELAY/PROBE state but still show to user space
	 * large times in the past.
	 */
	unsigned long mint = jiffies - (LONG_MAX - 86400 * HZ);

	neigh_hold(n);
	if (!time_in_range(n->confirmed, mint, jiffies))
		n->confirmed = mint;
	if (time_before(n->used, n->confirmed))
		n->used = n->confirmed;
	if (unlikely(mod_timer(&n->timer, when))) {
		printk("NEIGH: BUG, double timer add, state is %x\n",
		       n->nud_state);
		dump_stack();
	}
}

static int neigh_del_timer(struct neighbour *n)
{
	if ((n->nud_state & NUD_IN_TIMER) &&
	    del_timer(&n->timer)) {
		neigh_release(n);
		return 1;
	}
	return 0;
}

static struct neigh_parms *neigh_get_dev_parms_rcu(struct net_device *dev,
						   int family)
{
	switch (family) {
	case AF_INET:
		return __in_dev_arp_parms_get_rcu(dev);
	case AF_INET6:
		return __in6_dev_nd_parms_get_rcu(dev);
	}
	return NULL;
}

static void neigh_parms_qlen_dec(struct net_device *dev, int family)
{
	struct neigh_parms *p;

	rcu_read_lock();
	p = neigh_get_dev_parms_rcu(dev, family);
	if (p)
		p->qlen--;
	rcu_read_unlock();
}

static void pneigh_queue_purge(struct sk_buff_head *list, struct net *net,
			       int family)
{
	struct sk_buff_head tmp;
	unsigned long flags;
	struct sk_buff *skb;

	skb_queue_head_init(&tmp);
	spin_lock_irqsave(&list->lock, flags);
	skb = skb_peek(list);
	while (skb != NULL) {
		struct sk_buff *skb_next = skb_peek_next(skb, list);
		struct net_device *dev = skb->dev;

		if (net == NULL || net_eq(dev_net(dev), net)) {
			neigh_parms_qlen_dec(dev, family);
			__skb_unlink(skb, list);
			__skb_queue_tail(&tmp, skb);
		}
		skb = skb_next;
	}
	spin_unlock_irqrestore(&list->lock, flags);

	while ((skb = __skb_dequeue(&tmp))) {
		dev_put(skb->dev);
		kfree_skb(skb);
	}
}

static void neigh_flush_dev(struct neigh_table *tbl, struct net_device *dev,
			    bool skip_perm)
{
	int i;
	struct neigh_hash_table *nht;

	nht = rcu_dereference_protected(tbl->nht,
					lockdep_is_held(&tbl->lock));

	for (i = 0; i < (1 << nht->hash_shift); i++) {
		struct neighbour *n;
		struct neighbour __rcu **np = &nht->hash_buckets[i];

		while ((n = rcu_dereference_protected(*np,
					lockdep_is_held(&tbl->lock))) != NULL) {
			if (dev && n->dev != dev) {
				np = &n->next;
				continue;
			}
			if (skip_perm && n->nud_state & NUD_PERMANENT) {
				np = &n->next;
				continue;
			}
			rcu_assign_pointer(*np,
				   rcu_dereference_protected(n->next,
						lockdep_is_held(&tbl->lock)));
			write_lock(&n->lock);
			neigh_del_timer(n);
			neigh_mark_dead(n);
			if (refcount_read(&n->refcnt) != 1) {
				/* The most unpleasant situation.
				   We must destroy neighbour entry,
				   but someone still uses it.

				   The destroy will be delayed until
				   the last user releases us, but
				   we must kill timers etc. and move
				   it to safe state.
				 */
				__skb_queue_purge(&n->arp_queue);
				n->arp_queue_len_bytes = 0;
				WRITE_ONCE(n->output, neigh_blackhole);
				if (n->nud_state & NUD_VALID)
					n->nud_state = NUD_NOARP;
				else
					n->nud_state = NUD_NONE;
				neigh_dbg(2, "neigh %p is stray\n", n);
			}
			write_unlock(&n->lock);
			neigh_cleanup_and_release(n);
		}
	}
}

void neigh_changeaddr(struct neigh_table *tbl, struct net_device *dev)
{
	write_lock_bh(&tbl->lock);
	neigh_flush_dev(tbl, dev, false);
	write_unlock_bh(&tbl->lock);
}
EXPORT_SYMBOL(neigh_changeaddr);

static int __neigh_ifdown(struct neigh_table *tbl, struct net_device *dev,
			  bool skip_perm)
{
	write_lock_bh(&tbl->lock);
	neigh_flush_dev(tbl, dev, skip_perm);
	pneigh_ifdown_and_unlock(tbl, dev);
	pneigh_queue_purge(&tbl->proxy_queue, dev ? dev_net(dev) : NULL,
			   tbl->family);
	if (skb_queue_empty_lockless(&tbl->proxy_queue))
		del_timer_sync(&tbl->proxy_timer);
	return 0;
}

int neigh_carrier_down(struct neigh_table *tbl, struct net_device *dev)
{
	__neigh_ifdown(tbl, dev, true);
	return 0;
}
EXPORT_SYMBOL(neigh_carrier_down);

int neigh_ifdown(struct neigh_table *tbl, struct net_device *dev)
{
	__neigh_ifdown(tbl, dev, false);
	return 0;
}
EXPORT_SYMBOL(neigh_ifdown);

static struct neighbour *neigh_alloc(struct neigh_table *tbl,
				     struct net_device *dev,
				     u32 flags, bool exempt_from_gc)
{
	struct neighbour *n = NULL;
	unsigned long now = jiffies;
	int entries, gc_thresh3;

	if (exempt_from_gc)
		goto do_alloc;

	entries = atomic_inc_return(&tbl->gc_entries) - 1;
	gc_thresh3 = READ_ONCE(tbl->gc_thresh3);
	if (entries >= gc_thresh3 ||
	    (entries >= READ_ONCE(tbl->gc_thresh2) &&
	     time_after(now, READ_ONCE(tbl->last_flush) + 5 * HZ))) {
		if (!neigh_forced_gc(tbl) && entries >= gc_thresh3) {
			net_info_ratelimited("%s: neighbor table overflow!\n",
					     tbl->id);
			NEIGH_CACHE_STAT_INC(tbl, table_fulls);
			goto out_entries;
		}
	}

do_alloc:
	n = kzalloc(tbl->entry_size + dev->neigh_priv_len, GFP_ATOMIC);
	if (!n)
		goto out_entries;

	__skb_queue_head_init(&n->arp_queue);
	rwlock_init(&n->lock);
	seqlock_init(&n->ha_lock);
	n->updated	  = n->used = now;
	n->nud_state	  = NUD_NONE;
	n->output	  = neigh_blackhole;
	n->flags	  = flags;
	seqlock_init(&n->hh.hh_lock);
	n->parms	  = neigh_parms_clone(&tbl->parms);
	timer_setup(&n->timer, neigh_timer_handler, 0);

	NEIGH_CACHE_STAT_INC(tbl, allocs);
	n->tbl		  = tbl;
	refcount_set(&n->refcnt, 1);
	n->dead		  = 1;
	INIT_LIST_HEAD(&n->gc_list);
	INIT_LIST_HEAD(&n->managed_list);

	atomic_inc(&tbl->entries);
out:
	return n;

out_entries:
	if (!exempt_from_gc)
		atomic_dec(&tbl->gc_entries);
	goto out;
}

static void neigh_get_hash_rnd(u32 *x)
{
	*x = get_random_u32() | 1;
}

static struct neigh_hash_table *neigh_hash_alloc(unsigned int shift)
{
	size_t size = (1 << shift) * sizeof(struct neighbour *);
	struct neigh_hash_table *ret;
	struct neighbour __rcu **buckets;
	int i;

	ret = kmalloc(sizeof(*ret), GFP_ATOMIC);
	if (!ret)
		return NULL;
	if (size <= PAGE_SIZE) {
		buckets = kzalloc(size, GFP_ATOMIC);
	} else {
		buckets = (struct neighbour __rcu **)
			  __get_free_pages(GFP_ATOMIC | __GFP_ZERO,
					   get_order(size));
		kmemleak_alloc(buckets, size, 1, GFP_ATOMIC);
	}
	if (!buckets) {
		kfree(ret);
		return NULL;
	}
	ret->hash_buckets = buckets;
	ret->hash_shift = shift;
	for (i = 0; i < NEIGH_NUM_HASH_RND; i++)
		neigh_get_hash_rnd(&ret->hash_rnd[i]);
	return ret;
}

static void neigh_hash_free_rcu(struct rcu_head *head)
{
	struct neigh_hash_table *nht = container_of(head,
						    struct neigh_hash_table,
						    rcu);
	size_t size = (1 << nht->hash_shift) * sizeof(struct neighbour *);
	struct neighbour __rcu **buckets = nht->hash_buckets;

	if (size <= PAGE_SIZE) {
		kfree(buckets);
	} else {
		kmemleak_free(buckets);
		free_pages((unsigned long)buckets, get_order(size));
	}
	kfree(nht);
}

static struct neigh_hash_table *neigh_hash_grow(struct neigh_table *tbl,
						unsigned long new_shift)
{
	unsigned int i, hash;
	struct neigh_hash_table *new_nht, *old_nht;

	NEIGH_CACHE_STAT_INC(tbl, hash_grows);

	old_nht = rcu_dereference_protected(tbl->nht,
					    lockdep_is_held(&tbl->lock));
	new_nht = neigh_hash_alloc(new_shift);
	if (!new_nht)
		return old_nht;

	for (i = 0; i < (1 << old_nht->hash_shift); i++) {
		struct neighbour *n, *next;

		for (n = rcu_dereference_protected(old_nht->hash_buckets[i],
						   lockdep_is_held(&tbl->lock));
		     n != NULL;
		     n = next) {
			hash = tbl->hash(n->primary_key, n->dev,
					 new_nht->hash_rnd);

			hash >>= (32 - new_nht->hash_shift);
			next = rcu_dereference_protected(n->next,
						lockdep_is_held(&tbl->lock));

			rcu_assign_pointer(n->next,
					   rcu_dereference_protected(
						new_nht->hash_buckets[hash],
						lockdep_is_held(&tbl->lock)));
			rcu_assign_pointer(new_nht->hash_buckets[hash], n);
		}
	}

	rcu_assign_pointer(tbl->nht, new_nht);
	call_rcu(&old_nht->rcu, neigh_hash_free_rcu);
	return new_nht;
}

struct neighbour *neigh_lookup(struct neigh_table *tbl, const void *pkey,
			       struct net_device *dev)
{
	struct neighbour *n;

	NEIGH_CACHE_STAT_INC(tbl, lookups);

	rcu_read_lock();
	n = __neigh_lookup_noref(tbl, pkey, dev);
	if (n) {
		if (!refcount_inc_not_zero(&n->refcnt))
			n = NULL;
		NEIGH_CACHE_STAT_INC(tbl, hits);
	}

	rcu_read_unlock();
	return n;
}
EXPORT_SYMBOL(neigh_lookup);

static struct neighbour *
___neigh_create(struct neigh_table *tbl, const void *pkey,
		struct net_device *dev, u32 flags,
		bool exempt_from_gc, bool want_ref)
{
	u32 hash_val, key_len = tbl->key_len;
	struct neighbour *n1, *rc, *n;
	struct neigh_hash_table *nht;
	int error;

	n = neigh_alloc(tbl, dev, flags, exempt_from_gc);
	trace_neigh_create(tbl, dev, pkey, n, exempt_from_gc);
	if (!n) {
		rc = ERR_PTR(-ENOBUFS);
		goto out;
	}

	memcpy(n->primary_key, pkey, key_len);
	n->dev = dev;
	netdev_hold(dev, &n->dev_tracker, GFP_ATOMIC);

	/* Protocol specific setup. */
	if (tbl->constructor &&	(error = tbl->constructor(n)) < 0) {
		rc = ERR_PTR(error);
		goto out_neigh_release;
	}

	if (dev->netdev_ops->ndo_neigh_construct) {
		error = dev->netdev_ops->ndo_neigh_construct(dev, n);
		if (error < 0) {
			rc = ERR_PTR(error);
			goto out_neigh_release;
		}
	}

	/* Device specific setup. */
	if (n->parms->neigh_setup &&
	    (error = n->parms->neigh_setup(n)) < 0) {
		rc = ERR_PTR(error);
		goto out_neigh_release;
	}

	n->confirmed = jiffies - (NEIGH_VAR(n->parms, BASE_REACHABLE_TIME) << 1);

	write_lock_bh(&tbl->lock);
	nht = rcu_dereference_protected(tbl->nht,
					lockdep_is_held(&tbl->lock));

	if (atomic_read(&tbl->entries) > (1 << nht->hash_shift))
		nht = neigh_hash_grow(tbl, nht->hash_shift + 1);

	hash_val = tbl->hash(n->primary_key, dev, nht->hash_rnd) >> (32 - nht->hash_shift);

	if (n->parms->dead) {
		rc = ERR_PTR(-EINVAL);
		goto out_tbl_unlock;
	}

	for (n1 = rcu_dereference_protected(nht->hash_buckets[hash_val],
					    lockdep_is_held(&tbl->lock));
	     n1 != NULL;
	     n1 = rcu_dereference_protected(n1->next,
			lockdep_is_held(&tbl->lock))) {
		if (dev == n1->dev && !memcmp(n1->primary_key, n->primary_key, key_len)) {
			if (want_ref)
				neigh_hold(n1);
			rc = n1;
			goto out_tbl_unlock;
		}
	}

	n->dead = 0;
	if (!exempt_from_gc)
		list_add_tail(&n->gc_list, &n->tbl->gc_list);
	if (n->flags & NTF_MANAGED)
		list_add_tail(&n->managed_list, &n->tbl->managed_list);
	if (want_ref)
		neigh_hold(n);
	rcu_assign_pointer(n->next,
			   rcu_dereference_protected(nht->hash_buckets[hash_val],
						     lockdep_is_held(&tbl->lock)));
	rcu_assign_pointer(nht->hash_buckets[hash_val], n);
	write_unlock_bh(&tbl->lock);
	neigh_dbg(2, "neigh %p is created\n", n);
	rc = n;
out:
	return rc;
out_tbl_unlock:
	write_unlock_bh(&tbl->lock);
out_neigh_release:
	if (!exempt_from_gc)
		atomic_dec(&tbl->gc_entries);
	neigh_release(n);
	goto out;
}

struct neighbour *__neigh_create(struct neigh_table *tbl, const void *pkey,
				 struct net_device *dev, bool want_ref)
{
	bool exempt_from_gc = !!(dev->flags & IFF_LOOPBACK);

	return ___neigh_create(tbl, pkey, dev, 0, exempt_from_gc, want_ref);
}
EXPORT_SYMBOL(__neigh_create);

static u32 pneigh_hash(const void *pkey, unsigned int key_len)
{
	u32 hash_val = *(u32 *)(pkey + key_len - 4);
	hash_val ^= (hash_val >> 16);
	hash_val ^= hash_val >> 8;
	hash_val ^= hash_val >> 4;
	hash_val &= PNEIGH_HASHMASK;
	return hash_val;
}

static struct pneigh_entry *__pneigh_lookup_1(struct pneigh_entry *n,
					      struct net *net,
					      const void *pkey,
					      unsigned int key_len,
					      struct net_device *dev)
{
	while (n) {
		if (!memcmp(n->key, pkey, key_len) &&
		    net_eq(pneigh_net(n), net) &&
		    (n->dev == dev || !n->dev))
			return n;
		n = n->next;
	}
	return NULL;
}

struct pneigh_entry *__pneigh_lookup(struct neigh_table *tbl,
		struct net *net, const void *pkey, struct net_device *dev)
{
	unsigned int key_len = tbl->key_len;
	u32 hash_val = pneigh_hash(pkey, key_len);

	return __pneigh_lookup_1(tbl->phash_buckets[hash_val],
				 net, pkey, key_len, dev);
}
EXPORT_SYMBOL_GPL(__pneigh_lookup);

struct pneigh_entry * pneigh_lookup(struct neigh_table *tbl,
				    struct net *net, const void *pkey,
				    struct net_device *dev, int creat)
{
	struct pneigh_entry *n;
	unsigned int key_len = tbl->key_len;
	u32 hash_val = pneigh_hash(pkey, key_len);

	read_lock_bh(&tbl->lock);
	n = __pneigh_lookup_1(tbl->phash_buckets[hash_val],
			      net, pkey, key_len, dev);
	read_unlock_bh(&tbl->lock);

	if (n || !creat)
		goto out;

	ASSERT_RTNL();

	n = kzalloc(sizeof(*n) + key_len, GFP_KERNEL);
	if (!n)
		goto out;

	write_pnet(&n->net, net);
	memcpy(n->key, pkey, key_len);
	n->dev = dev;
	netdev_hold(dev, &n->dev_tracker, GFP_KERNEL);

	if (tbl->pconstructor && tbl->pconstructor(n)) {
		netdev_put(dev, &n->dev_tracker);
		kfree(n);
		n = NULL;
		goto out;
	}

	write_lock_bh(&tbl->lock);
	n->next = tbl->phash_buckets[hash_val];
	tbl->phash_buckets[hash_val] = n;
	write_unlock_bh(&tbl->lock);
out:
	return n;
}
EXPORT_SYMBOL(pneigh_lookup);


int pneigh_delete(struct neigh_table *tbl, struct net *net, const void *pkey,
		  struct net_device *dev)
{
	struct pneigh_entry *n, **np;
	unsigned int key_len = tbl->key_len;
	u32 hash_val = pneigh_hash(pkey, key_len);

	write_lock_bh(&tbl->lock);
	for (np = &tbl->phash_buckets[hash_val]; (n = *np) != NULL;
	     np = &n->next) {
		if (!memcmp(n->key, pkey, key_len) && n->dev == dev &&
		    net_eq(pneigh_net(n), net)) {
			*np = n->next;
			write_unlock_bh(&tbl->lock);
			if (tbl->pdestructor)
				tbl->pdestructor(n);
			netdev_put(n->dev, &n->dev_tracker);
			kfree(n);
			return 0;
		}
	}
	write_unlock_bh(&tbl->lock);
	return -ENOENT;
}

static int pneigh_ifdown_and_unlock(struct neigh_table *tbl,
				    struct net_device *dev)
{
	struct pneigh_entry *n, **np, *freelist = NULL;
	u32 h;

	for (h = 0; h <= PNEIGH_HASHMASK; h++) {
		np = &tbl->phash_buckets[h];
		while ((n = *np) != NULL) {
			if (!dev || n->dev == dev) {
				*np = n->next;
				n->next = freelist;
				freelist = n;
				continue;
			}
			np = &n->next;
		}
	}
	write_unlock_bh(&tbl->lock);
	while ((n = freelist)) {
		freelist = n->next;
		n->next = NULL;
		if (tbl->pdestructor)
			tbl->pdestructor(n);
		netdev_put(n->dev, &n->dev_tracker);
		kfree(n);
	}
	return -ENOENT;
}

static void neigh_parms_destroy(struct neigh_parms *parms);

static inline void neigh_parms_put(struct neigh_parms *parms)
{
	if (refcount_dec_and_test(&parms->refcnt))
		neigh_parms_destroy(parms);
}

/*
 *	neighbour must already be out of the table;
 *
 */
void neigh_destroy(struct neighbour *neigh)
{
	struct net_device *dev = neigh->dev;

	NEIGH_CACHE_STAT_INC(neigh->tbl, destroys);

	if (!neigh->dead) {
		pr_warn("Destroying alive neighbour %p\n", neigh);
		dump_stack();
		return;
	}

	if (neigh_del_timer(neigh))
		pr_warn("Impossible event\n");

	write_lock_bh(&neigh->lock);
	__skb_queue_purge(&neigh->arp_queue);
	write_unlock_bh(&neigh->lock);
	neigh->arp_queue_len_bytes = 0;

	if (dev->netdev_ops->ndo_neigh_destroy)
		dev->netdev_ops->ndo_neigh_destroy(dev, neigh);

	netdev_put(dev, &neigh->dev_tracker);
	neigh_parms_put(neigh->parms);

	neigh_dbg(2, "neigh %p is destroyed\n", neigh);

	atomic_dec(&neigh->tbl->entries);
	kfree_rcu(neigh, rcu);
}
EXPORT_SYMBOL(neigh_destroy);

/* Neighbour state is suspicious;
   disable fast path.

   Called with write_locked neigh.
 */
static void neigh_suspect(struct neighbour *neigh)
{
	neigh_dbg(2, "neigh %p is suspected\n", neigh);

	WRITE_ONCE(neigh->output, neigh->ops->output);
}

/* Neighbour state is OK;
   enable fast path.

   Called with write_locked neigh.
 */
static void neigh_connect(struct neighbour *neigh)
{
	neigh_dbg(2, "neigh %p is connected\n", neigh);

	WRITE_ONCE(neigh->output, neigh->ops->connected_output);
}

static void neigh_periodic_work(struct work_struct *work)
{
	struct neigh_table *tbl = container_of(work, struct neigh_table, gc_work.work);
	struct neighbour *n;
	struct neighbour __rcu **np;
	unsigned int i;
	struct neigh_hash_table *nht;

	NEIGH_CACHE_STAT_INC(tbl, periodic_gc_runs);

	write_lock_bh(&tbl->lock);
	nht = rcu_dereference_protected(tbl->nht,
					lockdep_is_held(&tbl->lock));

	/*
	 *	periodically recompute ReachableTime from random function
	 */

	if (time_after(jiffies, tbl->last_rand + 300 * HZ)) {
		struct neigh_parms *p;

		WRITE_ONCE(tbl->last_rand, jiffies);
		list_for_each_entry(p, &tbl->parms_list, list)
			p->reachable_time =
				neigh_rand_reach_time(NEIGH_VAR(p, BASE_REACHABLE_TIME));
	}

	if (atomic_read(&tbl->entries) < READ_ONCE(tbl->gc_thresh1))
		goto out;

	for (i = 0 ; i < (1 << nht->hash_shift); i++) {
		np = &nht->hash_buckets[i];

		while ((n = rcu_dereference_protected(*np,
				lockdep_is_held(&tbl->lock))) != NULL) {
			unsigned int state;

			write_lock(&n->lock);

			state = n->nud_state;
			if ((state & (NUD_PERMANENT | NUD_IN_TIMER)) ||
			    (n->flags & NTF_EXT_LEARNED)) {
				write_unlock(&n->lock);
				goto next_elt;
			}

			if (time_before(n->used, n->confirmed) &&
			    time_is_before_eq_jiffies(n->confirmed))
				n->used = n->confirmed;

			if (refcount_read(&n->refcnt) == 1 &&
			    (state == NUD_FAILED ||
			     !time_in_range_open(jiffies, n->used,
						 n->used + NEIGH_VAR(n->parms, GC_STALETIME)))) {
				rcu_assign_pointer(*np,
					rcu_dereference_protected(n->next,
						lockdep_is_held(&tbl->lock)));
				neigh_mark_dead(n);
				write_unlock(&n->lock);
				neigh_cleanup_and_release(n);
				continue;
			}
			write_unlock(&n->lock);

next_elt:
			np = &n->next;
		}
		/*
		 * It's fine to release lock here, even if hash table
		 * grows while we are preempted.
		 */
		write_unlock_bh(&tbl->lock);
		cond_resched();
		write_lock_bh(&tbl->lock);
		nht = rcu_dereference_protected(tbl->nht,
						lockdep_is_held(&tbl->lock));
	}
out:
	/* Cycle through all hash buckets every BASE_REACHABLE_TIME/2 ticks.
	 * ARP entry timeouts range from 1/2 BASE_REACHABLE_TIME to 3/2
	 * BASE_REACHABLE_TIME.
	 */
	queue_delayed_work(system_power_efficient_wq, &tbl->gc_work,
			      NEIGH_VAR(&tbl->parms, BASE_REACHABLE_TIME) >> 1);
	write_unlock_bh(&tbl->lock);
}

static __inline__ int neigh_max_probes(struct neighbour *n)
{
	struct neigh_parms *p = n->parms;
	return NEIGH_VAR(p, UCAST_PROBES) + NEIGH_VAR(p, APP_PROBES) +
	       (n->nud_state & NUD_PROBE ? NEIGH_VAR(p, MCAST_REPROBES) :
	        NEIGH_VAR(p, MCAST_PROBES));
}

static void neigh_invalidate(struct neighbour *neigh)
	__releases(neigh->lock)
	__acquires(neigh->lock)
{
	struct sk_buff *skb;

	NEIGH_CACHE_STAT_INC(neigh->tbl, res_failed);
	neigh_dbg(2, "neigh %p is failed\n", neigh);
	neigh->updated = jiffies;

	/* It is very thin place. report_unreachable is very complicated
	   routine. Particularly, it can hit the same neighbour entry!

	   So that, we try to be accurate and avoid dead loop. --ANK
	 */
	while (neigh->nud_state == NUD_FAILED &&
	       (skb = __skb_dequeue(&neigh->arp_queue)) != NULL) {
		write_unlock(&neigh->lock);
		neigh->ops->error_report(neigh, skb);
		write_lock(&neigh->lock);
	}
	__skb_queue_purge(&neigh->arp_queue);
	neigh->arp_queue_len_bytes = 0;
}

static void neigh_probe(struct neighbour *neigh)
	__releases(neigh->lock)
{
	struct sk_buff *skb = skb_peek_tail(&neigh->arp_queue);
	/* keep skb alive even if arp_queue overflows */
	if (skb)
		skb = skb_clone(skb, GFP_ATOMIC);
	write_unlock(&neigh->lock);
	if (neigh->ops->solicit)
		neigh->ops->solicit(neigh, skb);
	atomic_inc(&neigh->probes);
	consume_skb(skb);
}

/* Called when a timer expires for a neighbour entry. */

static void neigh_timer_handler(struct timer_list *t)
{
	unsigned long now, next;
	struct neighbour *neigh = from_timer(neigh, t, timer);
	unsigned int state;
	int notify = 0;

	write_lock(&neigh->lock);

	state = neigh->nud_state;
	now = jiffies;
	next = now + HZ;

	if (!(state & NUD_IN_TIMER))
		goto out;

	if (state & NUD_REACHABLE) {
		if (time_before_eq(now,
				   neigh->confirmed + neigh->parms->reachable_time)) {
			neigh_dbg(2, "neigh %p is still alive\n", neigh);
			next = neigh->confirmed + neigh->parms->reachable_time;
		} else if (time_before_eq(now,
					  neigh->used +
					  NEIGH_VAR(neigh->parms, DELAY_PROBE_TIME))) {
			neigh_dbg(2, "neigh %p is delayed\n", neigh);
			WRITE_ONCE(neigh->nud_state, NUD_DELAY);
			neigh->updated = jiffies;
			neigh_suspect(neigh);
			next = now + NEIGH_VAR(neigh->parms, DELAY_PROBE_TIME);
		} else {
			neigh_dbg(2, "neigh %p is suspected\n", neigh);
			WRITE_ONCE(neigh->nud_state, NUD_STALE);
			neigh->updated = jiffies;
			neigh_suspect(neigh);
			notify = 1;
		}
	} else if (state & NUD_DELAY) {
		if (time_before_eq(now,
				   neigh->confirmed +
				   NEIGH_VAR(neigh->parms, DELAY_PROBE_TIME))) {
			neigh_dbg(2, "neigh %p is now reachable\n", neigh);
			WRITE_ONCE(neigh->nud_state, NUD_REACHABLE);
			neigh->updated = jiffies;
			neigh_connect(neigh);
			notify = 1;
			next = neigh->confirmed + neigh->parms->reachable_time;
		} else {
			neigh_dbg(2, "neigh %p is probed\n", neigh);
			WRITE_ONCE(neigh->nud_state, NUD_PROBE);
			neigh->updated = jiffies;
			atomic_set(&neigh->probes, 0);
			notify = 1;
			next = now + max(NEIGH_VAR(neigh->parms, RETRANS_TIME),
					 HZ/100);
		}
	} else {
		/* NUD_PROBE|NUD_INCOMPLETE */
		next = now + max(NEIGH_VAR(neigh->parms, RETRANS_TIME), HZ/100);
	}

	if ((neigh->nud_state & (NUD_INCOMPLETE | NUD_PROBE)) &&
	    atomic_read(&neigh->probes) >= neigh_max_probes(neigh)) {
		WRITE_ONCE(neigh->nud_state, NUD_FAILED);
		notify = 1;
		neigh_invalidate(neigh);
		goto out;
	}

	if (neigh->nud_state & NUD_IN_TIMER) {
		if (time_before(next, jiffies + HZ/100))
			next = jiffies + HZ/100;
		if (!mod_timer(&neigh->timer, next))
			neigh_hold(neigh);
	}
	if (neigh->nud_state & (NUD_INCOMPLETE | NUD_PROBE)) {
		neigh_probe(neigh);
	} else {
out:
		write_unlock(&neigh->lock);
	}

	if (notify)
		neigh_update_notify(neigh, 0);

	trace_neigh_timer_handler(neigh, 0);

	neigh_release(neigh);
}

int __neigh_event_send(struct neighbour *neigh, struct sk_buff *skb,
		       const bool immediate_ok)
{
	int rc;
	bool immediate_probe = false;

	write_lock_bh(&neigh->lock);

	rc = 0;
	if (neigh->nud_state & (NUD_CONNECTED | NUD_DELAY | NUD_PROBE))
		goto out_unlock_bh;
	if (neigh->dead)
		goto out_dead;

	if (!(neigh->nud_state & (NUD_STALE | NUD_INCOMPLETE))) {
		if (NEIGH_VAR(neigh->parms, MCAST_PROBES) +
		    NEIGH_VAR(neigh->parms, APP_PROBES)) {
			unsigned long next, now = jiffies;

			atomic_set(&neigh->probes,
				   NEIGH_VAR(neigh->parms, UCAST_PROBES));
			neigh_del_timer(neigh);
			WRITE_ONCE(neigh->nud_state, NUD_INCOMPLETE);
			neigh->updated = now;
			if (!immediate_ok) {
				next = now + 1;
			} else {
				immediate_probe = true;
				next = now + max(NEIGH_VAR(neigh->parms,
							   RETRANS_TIME),
						 HZ / 100);
			}
			neigh_add_timer(neigh, next);
		} else {
			WRITE_ONCE(neigh->nud_state, NUD_FAILED);
			neigh->updated = jiffies;
			write_unlock_bh(&neigh->lock);

			kfree_skb_reason(skb, SKB_DROP_REASON_NEIGH_FAILED);
			return 1;
		}
	} else if (neigh->nud_state & NUD_STALE) {
		neigh_dbg(2, "neigh %p is delayed\n", neigh);
		neigh_del_timer(neigh);
		WRITE_ONCE(neigh->nud_state, NUD_DELAY);
		neigh->updated = jiffies;
		neigh_add_timer(neigh, jiffies +
				NEIGH_VAR(neigh->parms, DELAY_PROBE_TIME));
	}

	if (neigh->nud_state == NUD_INCOMPLETE) {
		if (skb) {
			while (neigh->arp_queue_len_bytes + skb->truesize >
			       NEIGH_VAR(neigh->parms, QUEUE_LEN_BYTES)) {
				struct sk_buff *buff;

				buff = __skb_dequeue(&neigh->arp_queue);
				if (!buff)
					break;
				neigh->arp_queue_len_bytes -= buff->truesize;
				kfree_skb_reason(buff, SKB_DROP_REASON_NEIGH_QUEUEFULL);
				NEIGH_CACHE_STAT_INC(neigh->tbl, unres_discards);
			}
			skb_dst_force(skb);
			__skb_queue_tail(&neigh->arp_queue, skb);
			neigh->arp_queue_len_bytes += skb->truesize;
		}
		rc = 1;
	}
out_unlock_bh:
	if (immediate_probe)
		neigh_probe(neigh);
	else
		write_unlock(&neigh->lock);
	local_bh_enable();
	trace_neigh_event_send_done(neigh, rc);
	return rc;

out_dead:
	if (neigh->nud_state & NUD_STALE)
		goto out_unlock_bh;
	write_unlock_bh(&neigh->lock);
	kfree_skb_reason(skb, SKB_DROP_REASON_NEIGH_DEAD);
	trace_neigh_event_send_dead(neigh, 1);
	return 1;
}
EXPORT_SYMBOL(__neigh_event_send);

static void neigh_update_hhs(struct neighbour *neigh)
{
	struct hh_cache *hh;
	void (*update)(struct hh_cache*, const struct net_device*, const unsigned char *)
		= NULL;

	if (neigh->dev->header_ops)
		update = neigh->dev->header_ops->cache_update;

	if (update) {
		hh = &neigh->hh;
		if (READ_ONCE(hh->hh_len)) {
			write_seqlock_bh(&hh->hh_lock);
			update(hh, neigh->dev, neigh->ha);
			write_sequnlock_bh(&hh->hh_lock);
		}
	}
}

/* Generic update routine.
   -- lladdr is new lladdr or NULL, if it is not supplied.
   -- new    is new state.
   -- flags
	NEIGH_UPDATE_F_OVERRIDE allows to override existing lladdr,
				if it is different.
	NEIGH_UPDATE_F_WEAK_OVERRIDE will suspect existing "connected"
				lladdr instead of overriding it
				if it is different.
	NEIGH_UPDATE_F_ADMIN	means that the change is administrative.
	NEIGH_UPDATE_F_USE	means that the entry is user triggered.
	NEIGH_UPDATE_F_MANAGED	means that the entry will be auto-refreshed.
	NEIGH_UPDATE_F_OVERRIDE_ISROUTER allows to override existing
				NTF_ROUTER flag.
	NEIGH_UPDATE_F_ISROUTER	indicates if the neighbour is known as
				a router.

   Caller MUST hold reference count on the entry.
 */
static int __neigh_update(struct neighbour *neigh, const u8 *lladdr,
			  u8 new, u32 flags, u32 nlmsg_pid,
			  struct netlink_ext_ack *extack)
{
	bool gc_update = false, managed_update = false;
	int update_isrouter = 0;
	struct net_device *dev;
	int err, notify = 0;
	u8 old;

	trace_neigh_update(neigh, lladdr, new, flags, nlmsg_pid);

	write_lock_bh(&neigh->lock);

	dev    = neigh->dev;
	old    = neigh->nud_state;
	err    = -EPERM;

	if (neigh->dead) {
		NL_SET_ERR_MSG(extack, "Neighbor entry is now dead");
		new = old;
		goto out;
	}
	if (!(flags & NEIGH_UPDATE_F_ADMIN) &&
	    (old & (NUD_NOARP | NUD_PERMANENT)))
		goto out;

	neigh_update_flags(neigh, flags, &notify, &gc_update, &managed_update);
	if (flags & (NEIGH_UPDATE_F_USE | NEIGH_UPDATE_F_MANAGED)) {
		new = old & ~NUD_PERMANENT;
		WRITE_ONCE(neigh->nud_state, new);
		err = 0;
		goto out;
	}

	if (!(new & NUD_VALID)) {
		neigh_del_timer(neigh);
		if (old & NUD_CONNECTED)
			neigh_suspect(neigh);
		WRITE_ONCE(neigh->nud_state, new);
		err = 0;
		notify = old & NUD_VALID;
		if ((old & (NUD_INCOMPLETE | NUD_PROBE)) &&
		    (new & NUD_FAILED)) {
			neigh_invalidate(neigh);
			notify = 1;
		}
		goto out;
	}

	/* Compare new lladdr with cached one */
	if (!dev->addr_len) {
		/* First case: device needs no address. */
		lladdr = neigh->ha;
	} else if (lladdr) {
		/* The second case: if something is already cached
		   and a new address is proposed:
		   - compare new & old
		   - if they are different, check override flag
		 */
		if ((old & NUD_VALID) &&
		    !memcmp(lladdr, neigh->ha, dev->addr_len))
			lladdr = neigh->ha;
	} else {
		/* No address is supplied; if we know something,
		   use it, otherwise discard the request.
		 */
		err = -EINVAL;
		if (!(old & NUD_VALID)) {
			NL_SET_ERR_MSG(extack, "No link layer address given");
			goto out;
		}
		lladdr = neigh->ha;
	}

	/* Update confirmed timestamp for neighbour entry after we
	 * received ARP packet even if it doesn't change IP to MAC binding.
	 */
	if (new & NUD_CONNECTED)
		neigh->confirmed = jiffies;

	/* If entry was valid and address is not changed,
	   do not change entry state, if new one is STALE.
	 */
	err = 0;
	update_isrouter = flags & NEIGH_UPDATE_F_OVERRIDE_ISROUTER;
	if (old & NUD_VALID) {
		if (lladdr != neigh->ha && !(flags & NEIGH_UPDATE_F_OVERRIDE)) {
			update_isrouter = 0;
			if ((flags & NEIGH_UPDATE_F_WEAK_OVERRIDE) &&
			    (old & NUD_CONNECTED)) {
				lladdr = neigh->ha;
				new = NUD_STALE;
			} else
				goto out;
		} else {
			if (lladdr == neigh->ha && new == NUD_STALE &&
			    !(flags & NEIGH_UPDATE_F_ADMIN))
				new = old;
		}
	}

	/* Update timestamp only once we know we will make a change to the
	 * neighbour entry. Otherwise we risk to move the locktime window with
	 * noop updates and ignore relevant ARP updates.
	 */
	if (new != old || lladdr != neigh->ha)
		neigh->updated = jiffies;

	if (new != old) {
		neigh_del_timer(neigh);
		if (new & NUD_PROBE)
			atomic_set(&neigh->probes, 0);
		if (new & NUD_IN_TIMER)
			neigh_add_timer(neigh, (jiffies +
						((new & NUD_REACHABLE) ?
						 neigh->parms->reachable_time :
						 0)));
		WRITE_ONCE(neigh->nud_state, new);
		notify = 1;
	}

	if (lladdr != neigh->ha) {
		write_seqlock(&neigh->ha_lock);
		memcpy(&neigh->ha, lladdr, dev->addr_len);
		write_sequnlock(&neigh->ha_lock);
		neigh_update_hhs(neigh);
		if (!(new & NUD_CONNECTED))
			neigh->confirmed = jiffies -
				      (NEIGH_VAR(neigh->parms, BASE_REACHABLE_TIME) << 1);
		notify = 1;
	}
	if (new == old)
		goto out;
	if (new & NUD_CONNECTED)
		neigh_connect(neigh);
	else
		neigh_suspect(neigh);
	if (!(old & NUD_VALID)) {
		struct sk_buff *skb;

		/* Again: avoid dead loop if something went wrong */

		while (neigh->nud_state & NUD_VALID &&
		       (skb = __skb_dequeue(&neigh->arp_queue)) != NULL) {
			struct dst_entry *dst = skb_dst(skb);
			struct neighbour *n2, *n1 = neigh;
			write_unlock_bh(&neigh->lock);

			rcu_read_lock();

			/* Why not just use 'neigh' as-is?  The problem is that
			 * things such as shaper, eql, and sch_teql can end up
			 * using alternative, different, neigh objects to output
			 * the packet in the output path.  So what we need to do
			 * here is re-lookup the top-level neigh in the path so
			 * we can reinject the packet there.
			 */
			n2 = NULL;
			if (dst && dst->obsolete != DST_OBSOLETE_DEAD) {
				n2 = dst_neigh_lookup_skb(dst, skb);
				if (n2)
					n1 = n2;
			}
			READ_ONCE(n1->output)(n1, skb);
			if (n2)
				neigh_release(n2);
			rcu_read_unlock();

			write_lock_bh(&neigh->lock);
		}
		__skb_queue_purge(&neigh->arp_queue);
		neigh->arp_queue_len_bytes = 0;
	}
out:
	if (update_isrouter)
		neigh_update_is_router(neigh, flags, &notify);
	write_unlock_bh(&neigh->lock);
	if (((new ^ old) & NUD_PERMANENT) || gc_update)
		neigh_update_gc_list(neigh);
	if (managed_update)
		neigh_update_managed_list(neigh);
	if (notify)
		neigh_update_notify(neigh, nlmsg_pid);
	trace_neigh_update_done(neigh, err);
	return err;
}

int neigh_update(struct neighbour *neigh, const u8 *lladdr, u8 new,
		 u32 flags, u32 nlmsg_pid)
{
	return __neigh_update(neigh, lladdr, new, flags, nlmsg_pid, NULL);
}
EXPORT_SYMBOL(neigh_update);

/* Update the neigh to listen temporarily for probe responses, even if it is
 * in a NUD_FAILED state. The caller has to hold neigh->lock for writing.
 */
void __neigh_set_probe_once(struct neighbour *neigh)
{
	if (neigh->dead)
		return;
	neigh->updated = jiffies;
	if (!(neigh->nud_state & NUD_FAILED))
		return;
	WRITE_ONCE(neigh->nud_state, NUD_INCOMPLETE);
	atomic_set(&neigh->probes, neigh_max_probes(neigh));
	neigh_add_timer(neigh,
			jiffies + max(NEIGH_VAR(neigh->parms, RETRANS_TIME),
				      HZ/100));
}
EXPORT_SYMBOL(__neigh_set_probe_once);

struct neighbour *neigh_event_ns(struct neigh_table *tbl,
				 u8 *lladdr, void *saddr,
				 struct net_device *dev)
{
	struct neighbour *neigh = __neigh_lookup(tbl, saddr, dev,
						 lladdr || !dev->addr_len);
	if (neigh)
		neigh_update(neigh, lladdr, NUD_STALE,
			     NEIGH_UPDATE_F_OVERRIDE, 0);
	return neigh;
}
EXPORT_SYMBOL(neigh_event_ns);

/* called with read_lock_bh(&n->lock); */
static void neigh_hh_init(struct neighbour *n)
{
	struct net_device *dev = n->dev;
	__be16 prot = n->tbl->protocol;
	struct hh_cache	*hh = &n->hh;

	write_lock_bh(&n->lock);

	/* Only one thread can come in here and initialize the
	 * hh_cache entry.
	 */
	if (!hh->hh_len)
		dev->header_ops->cache(n, hh, prot);

	write_unlock_bh(&n->lock);
}

/* Slow and careful. */

int neigh_resolve_output(struct neighbour *neigh, struct sk_buff *skb)
{
	int rc = 0;

	if (!neigh_event_send(neigh, skb)) {
		int err;
		struct net_device *dev = neigh->dev;
		unsigned int seq;

		if (dev->header_ops->cache && !READ_ONCE(neigh->hh.hh_len))
			neigh_hh_init(neigh);

		do {
			__skb_pull(skb, skb_network_offset(skb));
			seq = read_seqbegin(&neigh->ha_lock);
			err = dev_hard_header(skb, dev, ntohs(skb->protocol),
					      neigh->ha, NULL, skb->len);
		} while (read_seqretry(&neigh->ha_lock, seq));

		if (err >= 0)
			rc = dev_queue_xmit(skb);
		else
			goto out_kfree_skb;
	}
out:
	return rc;
out_kfree_skb:
	rc = -EINVAL;
	kfree_skb(skb);
	goto out;
}
EXPORT_SYMBOL(neigh_resolve_output);

/* As fast as possible without hh cache */

int neigh_connected_output(struct neighbour *neigh, struct sk_buff *skb)
{
	struct net_device *dev = neigh->dev;
	unsigned int seq;
	int err;

	do {
		__skb_pull(skb, skb_network_offset(skb));
		seq = read_seqbegin(&neigh->ha_lock);
		err = dev_hard_header(skb, dev, ntohs(skb->protocol),
				      neigh->ha, NULL, skb->len);
	} while (read_seqretry(&neigh->ha_lock, seq));

	if (err >= 0)
		err = dev_queue_xmit(skb);
	else {
		err = -EINVAL;
		kfree_skb(skb);
	}
	return err;
}
EXPORT_SYMBOL(neigh_connected_output);

int neigh_direct_output(struct neighbour *neigh, struct sk_buff *skb)
{
	return dev_queue_xmit(skb);
}
EXPORT_SYMBOL(neigh_direct_output);

static void neigh_managed_work(struct work_struct *work)
{
	struct neigh_table *tbl = container_of(work, struct neigh_table,
					       managed_work.work);
	struct neighbour *neigh;

	write_lock_bh(&tbl->lock);
	list_for_each_entry(neigh, &tbl->managed_list, managed_list)
		neigh_event_send_probe(neigh, NULL, false);
	queue_delayed_work(system_power_efficient_wq, &tbl->managed_work,
			   NEIGH_VAR(&tbl->parms, INTERVAL_PROBE_TIME_MS));
	write_unlock_bh(&tbl->lock);
}

static void neigh_proxy_process(struct timer_list *t)
{
	struct neigh_table *tbl = from_timer(tbl, t, proxy_timer);
	long sched_next = 0;
	unsigned long now = jiffies;
	struct sk_buff *skb, *n;

	spin_lock(&tbl->proxy_queue.lock);

	skb_queue_walk_safe(&tbl->proxy_queue, skb, n) {
		long tdif = NEIGH_CB(skb)->sched_next - now;

		if (tdif <= 0) {
			struct net_device *dev = skb->dev;

			neigh_parms_qlen_dec(dev, tbl->family);
			__skb_unlink(skb, &tbl->proxy_queue);

			if (tbl->proxy_redo && netif_running(dev)) {
				rcu_read_lock();
				tbl->proxy_redo(skb);
				rcu_read_unlock();
			} else {
				kfree_skb(skb);
			}

			dev_put(dev);
		} else if (!sched_next || tdif < sched_next)
			sched_next = tdif;
	}
	del_timer(&tbl->proxy_timer);
	if (sched_next)
		mod_timer(&tbl->proxy_timer, jiffies + sched_next);
	spin_unlock(&tbl->proxy_queue.lock);
}

static unsigned long neigh_proxy_delay(struct neigh_parms *p)
{
	/* If proxy_delay is zero, do not call get_random_u32_below()
	 * as it is undefined behavior.
	 */
	unsigned long proxy_delay = NEIGH_VAR(p, PROXY_DELAY);

	return proxy_delay ?
	       jiffies + get_random_u32_below(proxy_delay) : jiffies;
}

void pneigh_enqueue(struct neigh_table *tbl, struct neigh_parms *p,
		    struct sk_buff *skb)
{
	unsigned long sched_next = neigh_proxy_delay(p);

	if (p->qlen > NEIGH_VAR(p, PROXY_QLEN)) {
		kfree_skb(skb);
		return;
	}

	NEIGH_CB(skb)->sched_next = sched_next;
	NEIGH_CB(skb)->flags |= LOCALLY_ENQUEUED;

	spin_lock(&tbl->proxy_queue.lock);
	if (del_timer(&tbl->proxy_timer)) {
		if (time_before(tbl->proxy_timer.expires, sched_next))
			sched_next = tbl->proxy_timer.expires;
	}
	skb_dst_drop(skb);
	dev_hold(skb->dev);
	__skb_queue_tail(&tbl->proxy_queue, skb);
	p->qlen++;
	mod_timer(&tbl->proxy_timer, sched_next);
	spin_unlock(&tbl->proxy_queue.lock);
}
EXPORT_SYMBOL(pneigh_enqueue);

static inline struct neigh_parms *lookup_neigh_parms(struct neigh_table *tbl,
						      struct net *net, int ifindex)
{
	struct neigh_parms *p;

	list_for_each_entry(p, &tbl->parms_list, list) {
		if ((p->dev && p->dev->ifindex == ifindex && net_eq(neigh_parms_net(p), net)) ||
		    (!p->dev && !ifindex && net_eq(net, &init_net)))
			return p;
	}

	return NULL;
}

struct neigh_parms *neigh_parms_alloc(struct net_device *dev,
				      struct neigh_table *tbl)
{
	struct neigh_parms *p;
	struct net *net = dev_net(dev);
	const struct net_device_ops *ops = dev->netdev_ops;

	p = kmemdup(&tbl->parms, sizeof(*p), GFP_KERNEL);
	if (p) {
		p->tbl		  = tbl;
		refcount_set(&p->refcnt, 1);
		p->reachable_time =
				neigh_rand_reach_time(NEIGH_VAR(p, BASE_REACHABLE_TIME));
		p->qlen = 0;
		netdev_hold(dev, &p->dev_tracker, GFP_KERNEL);
		p->dev = dev;
		write_pnet(&p->net, net);
		p->sysctl_table = NULL;

		if (ops->ndo_neigh_setup && ops->ndo_neigh_setup(dev, p)) {
			netdev_put(dev, &p->dev_tracker);
			kfree(p);
			return NULL;
		}

		write_lock_bh(&tbl->lock);
		list_add(&p->list, &tbl->parms.list);
		write_unlock_bh(&tbl->lock);

		neigh_parms_data_state_cleanall(p);
	}
	return p;
}
EXPORT_SYMBOL(neigh_parms_alloc);

static void neigh_rcu_free_parms(struct rcu_head *head)
{
	struct neigh_parms *parms =
		container_of(head, struct neigh_parms, rcu_head);

	neigh_parms_put(parms);
}

void neigh_parms_release(struct neigh_table *tbl, struct neigh_parms *parms)
{
	if (!parms || parms == &tbl->parms)
		return;
	write_lock_bh(&tbl->lock);
	list_del(&parms->list);
	parms->dead = 1;
	write_unlock_bh(&tbl->lock);
	netdev_put(parms->dev, &parms->dev_tracker);
	call_rcu(&parms->rcu_head, neigh_rcu_free_parms);
}
EXPORT_SYMBOL(neigh_parms_release);

static void neigh_parms_destroy(struct neigh_parms *parms)
{
	kfree(parms);
}

static struct lock_class_key neigh_table_proxy_queue_class;

static struct neigh_table __rcu *neigh_tables[NEIGH_NR_TABLES] __read_mostly;

void neigh_table_init(int index, struct neigh_table *tbl)
{
	unsigned long now = jiffies;
	unsigned long phsize;

	INIT_LIST_HEAD(&tbl->parms_list);
	INIT_LIST_HEAD(&tbl->gc_list);
	INIT_LIST_HEAD(&tbl->managed_list);

	list_add(&tbl->parms.list, &tbl->parms_list);
	write_pnet(&tbl->parms.net, &init_net);
	refcount_set(&tbl->parms.refcnt, 1);
	tbl->parms.reachable_time =
			  neigh_rand_reach_time(NEIGH_VAR(&tbl->parms, BASE_REACHABLE_TIME));
	tbl->parms.qlen = 0;

	tbl->stats = alloc_percpu(struct neigh_statistics);
	if (!tbl->stats)
		panic("cannot create neighbour cache statistics");

#ifdef CONFIG_PROC_FS
	if (!proc_create_seq_data(tbl->id, 0, init_net.proc_net_stat,
			      &neigh_stat_seq_ops, tbl))
		panic("cannot create neighbour proc dir entry");
#endif

	RCU_INIT_POINTER(tbl->nht, neigh_hash_alloc(3));

	phsize = (PNEIGH_HASHMASK + 1) * sizeof(struct pneigh_entry *);
	tbl->phash_buckets = kzalloc(phsize, GFP_KERNEL);

	if (!tbl->nht || !tbl->phash_buckets)
		panic("cannot allocate neighbour cache hashes");

	if (!tbl->entry_size)
		tbl->entry_size = ALIGN(offsetof(struct neighbour, primary_key) +
					tbl->key_len, NEIGH_PRIV_ALIGN);
	else
		WARN_ON(tbl->entry_size % NEIGH_PRIV_ALIGN);

	rwlock_init(&tbl->lock);

	INIT_DEFERRABLE_WORK(&tbl->gc_work, neigh_periodic_work);
	queue_delayed_work(system_power_efficient_wq, &tbl->gc_work,
			tbl->parms.reachable_time);
	INIT_DEFERRABLE_WORK(&tbl->managed_work, neigh_managed_work);
	queue_delayed_work(system_power_efficient_wq, &tbl->managed_work, 0);

	timer_setup(&tbl->proxy_timer, neigh_proxy_process, 0);
	skb_queue_head_init_class(&tbl->proxy_queue,
			&neigh_table_proxy_queue_class);

	tbl->last_flush = now;
	tbl->last_rand	= now + tbl->parms.reachable_time * 20;

	rcu_assign_pointer(neigh_tables[index], tbl);
}
EXPORT_SYMBOL(neigh_table_init);

/*
 * Only called from ndisc_cleanup(), which means this is dead code
 * because we no longer can unload IPv6 module.
 */
int neigh_table_clear(int index, struct neigh_table *tbl)
{
	RCU_INIT_POINTER(neigh_tables[index], NULL);
	synchronize_rcu();

	/* It is not clean... Fix it to unload IPv6 module safely */
	cancel_delayed_work_sync(&tbl->managed_work);
	cancel_delayed_work_sync(&tbl->gc_work);
	del_timer_sync(&tbl->proxy_timer);
	pneigh_queue_purge(&tbl->proxy_queue, NULL, tbl->family);
	neigh_ifdown(tbl, NULL);
	if (atomic_read(&tbl->entries))
		pr_crit("neighbour leakage\n");

	call_rcu(&rcu_dereference_protected(tbl->nht, 1)->rcu,
		 neigh_hash_free_rcu);
	tbl->nht = NULL;

	kfree(tbl->phash_buckets);
	tbl->phash_buckets = NULL;

	remove_proc_entry(tbl->id, init_net.proc_net_stat);

	free_percpu(tbl->stats);
	tbl->stats = NULL;

	return 0;
}
EXPORT_SYMBOL(neigh_table_clear);

static struct neigh_table *neigh_find_table(int family)
{
	struct neigh_table *tbl = NULL;

	switch (family) {
	case AF_INET:
		tbl = rcu_dereference_rtnl(neigh_tables[NEIGH_ARP_TABLE]);
		break;
	case AF_INET6:
		tbl = rcu_dereference_rtnl(neigh_tables[NEIGH_ND_TABLE]);
		break;
	}

	return tbl;
}

const struct nla_policy nda_policy[NDA_MAX+1] = {
	[NDA_UNSPEC]		= { .strict_start_type = NDA_NH_ID },
	[NDA_DST]		= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[NDA_LLADDR]		= { .type = NLA_BINARY, .len = MAX_ADDR_LEN },
	[NDA_CACHEINFO]		= { .len = sizeof(struct nda_cacheinfo) },
	[NDA_PROBES]		= { .type = NLA_U32 },
	[NDA_VLAN]		= { .type = NLA_U16 },
	[NDA_PORT]		= { .type = NLA_U16 },
	[NDA_VNI]		= { .type = NLA_U32 },
	[NDA_IFINDEX]		= { .type = NLA_U32 },
	[NDA_MASTER]		= { .type = NLA_U32 },
	[NDA_PROTOCOL]		= { .type = NLA_U8 },
	[NDA_NH_ID]		= { .type = NLA_U32 },
	[NDA_FLAGS_EXT]		= NLA_POLICY_MASK(NLA_U32, NTF_EXT_MASK),
	[NDA_FDB_EXT_ATTRS]	= { .type = NLA_NESTED },
};

static int neigh_delete(struct sk_buff *skb, struct nlmsghdr *nlh,
			struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct ndmsg *ndm;
	struct nlattr *dst_attr;
	struct neigh_table *tbl;
	struct neighbour *neigh;
	struct net_device *dev = NULL;
	int err = -EINVAL;

	ASSERT_RTNL();
	if (nlmsg_len(nlh) < sizeof(*ndm))
		goto out;

	dst_attr = nlmsg_find_attr(nlh, sizeof(*ndm), NDA_DST);
	if (!dst_attr) {
		NL_SET_ERR_MSG(extack, "Network address not specified");
		goto out;
	}

	ndm = nlmsg_data(nlh);
	if (ndm->ndm_ifindex) {
		dev = __dev_get_by_index(net, ndm->ndm_ifindex);
		if (dev == NULL) {
			err = -ENODEV;
			goto out;
		}
	}

	tbl = neigh_find_table(ndm->ndm_family);
	if (tbl == NULL)
		return -EAFNOSUPPORT;

	if (nla_len(dst_attr) < (int)tbl->key_len) {
		NL_SET_ERR_MSG(extack, "Invalid network address");
		goto out;
	}

	if (ndm->ndm_flags & NTF_PROXY) {
		err = pneigh_delete(tbl, net, nla_data(dst_attr), dev);
		goto out;
	}

	if (dev == NULL)
		goto out;

	neigh = neigh_lookup(tbl, nla_data(dst_attr), dev);
	if (neigh == NULL) {
		err = -ENOENT;
		goto out;
	}

	err = __neigh_update(neigh, NULL, NUD_FAILED,
			     NEIGH_UPDATE_F_OVERRIDE | NEIGH_UPDATE_F_ADMIN,
			     NETLINK_CB(skb).portid, extack);
	write_lock_bh(&tbl->lock);
	neigh_release(neigh);
	neigh_remove_one(neigh, tbl);
	write_unlock_bh(&tbl->lock);

out:
	return err;
}

static int neigh_add(struct sk_buff *skb, struct nlmsghdr *nlh,
		     struct netlink_ext_ack *extack)
{
	int flags = NEIGH_UPDATE_F_ADMIN | NEIGH_UPDATE_F_OVERRIDE |
		    NEIGH_UPDATE_F_OVERRIDE_ISROUTER;
	struct net *net = sock_net(skb->sk);
	struct ndmsg *ndm;
	struct nlattr *tb[NDA_MAX+1];
	struct neigh_table *tbl;
	struct net_device *dev = NULL;
	struct neighbour *neigh;
	void *dst, *lladdr;
	u8 protocol = 0;
	u32 ndm_flags;
	int err;

	ASSERT_RTNL();
	err = nlmsg_parse_deprecated(nlh, sizeof(*ndm), tb, NDA_MAX,
				     nda_policy, extack);
	if (err < 0)
		goto out;

	err = -EINVAL;
	if (!tb[NDA_DST]) {
		NL_SET_ERR_MSG(extack, "Network address not specified");
		goto out;
	}

	ndm = nlmsg_data(nlh);
	ndm_flags = ndm->ndm_flags;
	if (tb[NDA_FLAGS_EXT]) {
		u32 ext = nla_get_u32(tb[NDA_FLAGS_EXT]);

		BUILD_BUG_ON(sizeof(neigh->flags) * BITS_PER_BYTE <
			     (sizeof(ndm->ndm_flags) * BITS_PER_BYTE +
			      hweight32(NTF_EXT_MASK)));
		ndm_flags |= (ext << NTF_EXT_SHIFT);
	}
	if (ndm->ndm_ifindex) {
		dev = __dev_get_by_index(net, ndm->ndm_ifindex);
		if (dev == NULL) {
			err = -ENODEV;
			goto out;
		}

		if (tb[NDA_LLADDR] && nla_len(tb[NDA_LLADDR]) < dev->addr_len) {
			NL_SET_ERR_MSG(extack, "Invalid link address");
			goto out;
		}
	}

	tbl = neigh_find_table(ndm->ndm_family);
	if (tbl == NULL)
		return -EAFNOSUPPORT;

	if (nla_len(tb[NDA_DST]) < (int)tbl->key_len) {
		NL_SET_ERR_MSG(extack, "Invalid network address");
		goto out;
	}

	dst = nla_data(tb[NDA_DST]);
	lladdr = tb[NDA_LLADDR] ? nla_data(tb[NDA_LLADDR]) : NULL;

	if (tb[NDA_PROTOCOL])
		protocol = nla_get_u8(tb[NDA_PROTOCOL]);
	if (ndm_flags & NTF_PROXY) {
		struct pneigh_entry *pn;

		if (ndm_flags & NTF_MANAGED) {
			NL_SET_ERR_MSG(extack, "Invalid NTF_* flag combination");
			goto out;
		}

		err = -ENOBUFS;
		pn = pneigh_lookup(tbl, net, dst, dev, 1);
		if (pn) {
			pn->flags = ndm_flags;
			if (protocol)
				pn->protocol = protocol;
			err = 0;
		}
		goto out;
	}

	if (!dev) {
		NL_SET_ERR_MSG(extack, "Device not specified");
		goto out;
	}

	if (tbl->allow_add && !tbl->allow_add(dev, extack)) {
		err = -EINVAL;
		goto out;
	}

	neigh = neigh_lookup(tbl, dst, dev);
	if (neigh == NULL) {
		bool ndm_permanent  = ndm->ndm_state & NUD_PERMANENT;
		bool exempt_from_gc = ndm_permanent ||
				      ndm_flags & NTF_EXT_LEARNED;

		if (!(nlh->nlmsg_flags & NLM_F_CREATE)) {
			err = -ENOENT;
			goto out;
		}
		if (ndm_permanent && (ndm_flags & NTF_MANAGED)) {
			NL_SET_ERR_MSG(extack, "Invalid NTF_* flag for permanent entry");
			err = -EINVAL;
			goto out;
		}

		neigh = ___neigh_create(tbl, dst, dev,
					ndm_flags &
					(NTF_EXT_LEARNED | NTF_MANAGED),
					exempt_from_gc, true);
		if (IS_ERR(neigh)) {
			err = PTR_ERR(neigh);
			goto out;
		}
	} else {
		if (nlh->nlmsg_flags & NLM_F_EXCL) {
			err = -EEXIST;
			neigh_release(neigh);
			goto out;
		}

		if (!(nlh->nlmsg_flags & NLM_F_REPLACE))
			flags &= ~(NEIGH_UPDATE_F_OVERRIDE |
				   NEIGH_UPDATE_F_OVERRIDE_ISROUTER);
	}

	if (protocol)
		neigh->protocol = protocol;
	if (ndm_flags & NTF_EXT_LEARNED)
		flags |= NEIGH_UPDATE_F_EXT_LEARNED;
	if (ndm_flags & NTF_ROUTER)
		flags |= NEIGH_UPDATE_F_ISROUTER;
	if (ndm_flags & NTF_MANAGED)
		flags |= NEIGH_UPDATE_F_MANAGED;
	if (ndm_flags & NTF_USE)
		flags |= NEIGH_UPDATE_F_USE;

	err = __neigh_update(neigh, lladdr, ndm->ndm_state, flags,
			     NETLINK_CB(skb).portid, extack);
	if (!err && ndm_flags & (NTF_USE | NTF_MANAGED)) {
		neigh_event_send(neigh, NULL);
		err = 0;
	}
	neigh_release(neigh);
out:
	return err;
}

static int neightbl_fill_parms(struct sk_buff *skb, struct neigh_parms *parms)
{
	struct nlattr *nest;

	nest = nla_nest_start_noflag(skb, NDTA_PARMS);
	if (nest == NULL)
		return -ENOBUFS;

	if ((parms->dev &&
	     nla_put_u32(skb, NDTPA_IFINDEX, parms->dev->ifindex)) ||
	    nla_put_u32(skb, NDTPA_REFCNT, refcount_read(&parms->refcnt)) ||
	    nla_put_u32(skb, NDTPA_QUEUE_LENBYTES,
			NEIGH_VAR(parms, QUEUE_LEN_BYTES)) ||
	    /* approximative value for deprecated QUEUE_LEN (in packets) */
	    nla_put_u32(skb, NDTPA_QUEUE_LEN,
			NEIGH_VAR(parms, QUEUE_LEN_BYTES) / SKB_TRUESIZE(ETH_FRAME_LEN)) ||
	    nla_put_u32(skb, NDTPA_PROXY_QLEN, NEIGH_VAR(parms, PROXY_QLEN)) ||
	    nla_put_u32(skb, NDTPA_APP_PROBES, NEIGH_VAR(parms, APP_PROBES)) ||
	    nla_put_u32(skb, NDTPA_UCAST_PROBES,
			NEIGH_VAR(parms, UCAST_PROBES)) ||
	    nla_put_u32(skb, NDTPA_MCAST_PROBES,
			NEIGH_VAR(parms, MCAST_PROBES)) ||
	    nla_put_u32(skb, NDTPA_MCAST_REPROBES,
			NEIGH_VAR(parms, MCAST_REPROBES)) ||
	    nla_put_msecs(skb, NDTPA_REACHABLE_TIME, parms->reachable_time,
			  NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_BASE_REACHABLE_TIME,
			  NEIGH_VAR(parms, BASE_REACHABLE_TIME), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_GC_STALETIME,
			  NEIGH_VAR(parms, GC_STALETIME), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_DELAY_PROBE_TIME,
			  NEIGH_VAR(parms, DELAY_PROBE_TIME), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_RETRANS_TIME,
			  NEIGH_VAR(parms, RETRANS_TIME), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_ANYCAST_DELAY,
			  NEIGH_VAR(parms, ANYCAST_DELAY), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_PROXY_DELAY,
			  NEIGH_VAR(parms, PROXY_DELAY), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_LOCKTIME,
			  NEIGH_VAR(parms, LOCKTIME), NDTPA_PAD) ||
	    nla_put_msecs(skb, NDTPA_INTERVAL_PROBE_TIME_MS,
			  NEIGH_VAR(parms, INTERVAL_PROBE_TIME_MS), NDTPA_PAD))
		goto nla_put_failure;
	return nla_nest_end(skb, nest);

nla_put_failure:
	nla_nest_cancel(skb, nest);
	return -EMSGSIZE;
}

static int neightbl_fill_info(struct sk_buff *skb, struct neigh_table *tbl,
			      u32 pid, u32 seq, int type, int flags)
{
	struct nlmsghdr *nlh;
	struct ndtmsg *ndtmsg;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ndtmsg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ndtmsg = nlmsg_data(nlh);

	read_lock_bh(&tbl->lock);
	ndtmsg->ndtm_family = tbl->family;
	ndtmsg->ndtm_pad1   = 0;
	ndtmsg->ndtm_pad2   = 0;

	if (nla_put_string(skb, NDTA_NAME, tbl->id) ||
	    nla_put_msecs(skb, NDTA_GC_INTERVAL, READ_ONCE(tbl->gc_interval),
			  NDTA_PAD) ||
	    nla_put_u32(skb, NDTA_THRESH1, READ_ONCE(tbl->gc_thresh1)) ||
	    nla_put_u32(skb, NDTA_THRESH2, READ_ONCE(tbl->gc_thresh2)) ||
	    nla_put_u32(skb, NDTA_THRESH3, READ_ONCE(tbl->gc_thresh3)))
		goto nla_put_failure;
	{
		unsigned long now = jiffies;
		long flush_delta = now - READ_ONCE(tbl->last_flush);
		long rand_delta = now - READ_ONCE(tbl->last_rand);
		struct neigh_hash_table *nht;
		struct ndt_config ndc = {
			.ndtc_key_len		= tbl->key_len,
			.ndtc_entry_size	= tbl->entry_size,
			.ndtc_entries		= atomic_read(&tbl->entries),
			.ndtc_last_flush	= jiffies_to_msecs(flush_delta),
			.ndtc_last_rand		= jiffies_to_msecs(rand_delta),
			.ndtc_proxy_qlen	= READ_ONCE(tbl->proxy_queue.qlen),
		};

		rcu_read_lock();
		nht = rcu_dereference(tbl->nht);
		ndc.ndtc_hash_rnd = nht->hash_rnd[0];
		ndc.ndtc_hash_mask = ((1 << nht->hash_shift) - 1);
		rcu_read_unlock();

		if (nla_put(skb, NDTA_CONFIG, sizeof(ndc), &ndc))
			goto nla_put_failure;
	}

	{
		int cpu;
		struct ndt_stats ndst;

		memset(&ndst, 0, sizeof(ndst));

		for_each_possible_cpu(cpu) {
			struct neigh_statistics	*st;

			st = per_cpu_ptr(tbl->stats, cpu);
			ndst.ndts_allocs		+= READ_ONCE(st->allocs);
			ndst.ndts_destroys		+= READ_ONCE(st->destroys);
			ndst.ndts_hash_grows		+= READ_ONCE(st->hash_grows);
			ndst.ndts_res_failed		+= READ_ONCE(st->res_failed);
			ndst.ndts_lookups		+= READ_ONCE(st->lookups);
			ndst.ndts_hits			+= READ_ONCE(st->hits);
			ndst.ndts_rcv_probes_mcast	+= READ_ONCE(st->rcv_probes_mcast);
			ndst.ndts_rcv_probes_ucast	+= READ_ONCE(st->rcv_probes_ucast);
			ndst.ndts_periodic_gc_runs	+= READ_ONCE(st->periodic_gc_runs);
			ndst.ndts_forced_gc_runs	+= READ_ONCE(st->forced_gc_runs);
			ndst.ndts_table_fulls		+= READ_ONCE(st->table_fulls);
		}

		if (nla_put_64bit(skb, NDTA_STATS, sizeof(ndst), &ndst,
				  NDTA_PAD))
			goto nla_put_failure;
	}

	BUG_ON(tbl->parms.dev);
	if (neightbl_fill_parms(skb, &tbl->parms) < 0)
		goto nla_put_failure;

	read_unlock_bh(&tbl->lock);
	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	read_unlock_bh(&tbl->lock);
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int neightbl_fill_param_info(struct sk_buff *skb,
				    struct neigh_table *tbl,
				    struct neigh_parms *parms,
				    u32 pid, u32 seq, int type,
				    unsigned int flags)
{
	struct ndtmsg *ndtmsg;
	struct nlmsghdr *nlh;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ndtmsg), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	ndtmsg = nlmsg_data(nlh);

	read_lock_bh(&tbl->lock);
	ndtmsg->ndtm_family = tbl->family;
	ndtmsg->ndtm_pad1   = 0;
	ndtmsg->ndtm_pad2   = 0;

	if (nla_put_string(skb, NDTA_NAME, tbl->id) < 0 ||
	    neightbl_fill_parms(skb, parms) < 0)
		goto errout;

	read_unlock_bh(&tbl->lock);
	nlmsg_end(skb, nlh);
	return 0;
errout:
	read_unlock_bh(&tbl->lock);
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static const struct nla_policy nl_neightbl_policy[NDTA_MAX+1] = {
	[NDTA_NAME]		= { .type = NLA_STRING },
	[NDTA_THRESH1]		= { .type = NLA_U32 },
	[NDTA_THRESH2]		= { .type = NLA_U32 },
	[NDTA_THRESH3]		= { .type = NLA_U32 },
	[NDTA_GC_INTERVAL]	= { .type = NLA_U64 },
	[NDTA_PARMS]		= { .type = NLA_NESTED },
};

static const struct nla_policy nl_ntbl_parm_policy[NDTPA_MAX+1] = {
	[NDTPA_IFINDEX]			= { .type = NLA_U32 },
	[NDTPA_QUEUE_LEN]		= { .type = NLA_U32 },
	[NDTPA_PROXY_QLEN]		= { .type = NLA_U32 },
	[NDTPA_APP_PROBES]		= { .type = NLA_U32 },
	[NDTPA_UCAST_PROBES]		= { .type = NLA_U32 },
	[NDTPA_MCAST_PROBES]		= { .type = NLA_U32 },
	[NDTPA_MCAST_REPROBES]		= { .type = NLA_U32 },
	[NDTPA_BASE_REACHABLE_TIME]	= { .type = NLA_U64 },
	[NDTPA_GC_STALETIME]		= { .type = NLA_U64 },
	[NDTPA_DELAY_PROBE_TIME]	= { .type = NLA_U64 },
	[NDTPA_RETRANS_TIME]		= { .type = NLA_U64 },
	[NDTPA_ANYCAST_DELAY]		= { .type = NLA_U64 },
	[NDTPA_PROXY_DELAY]		= { .type = NLA_U64 },
	[NDTPA_LOCKTIME]		= { .type = NLA_U64 },
	[NDTPA_INTERVAL_PROBE_TIME_MS]	= { .type = NLA_U64, .min = 1 },
};

static int neightbl_set(struct sk_buff *skb, struct nlmsghdr *nlh,
			struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(skb->sk);
	struct neigh_table *tbl;
	struct ndtmsg *ndtmsg;
	struct nlattr *tb[NDTA_MAX+1];
	bool found = false;
	int err, tidx;

	err = nlmsg_parse_deprecated(nlh, sizeof(*ndtmsg), tb, NDTA_MAX,
				     nl_neightbl_policy, extack);
	if (err < 0)
		goto errout;

	if (tb[NDTA_NAME] == NULL) {
		err = -EINVAL;
		goto errout;
	}

	ndtmsg = nlmsg_data(nlh);

	for (tidx = 0; tidx < NEIGH_NR_TABLES; tidx++) {
		tbl = rcu_dereference_rtnl(neigh_tables[tidx]);
		if (!tbl)
			continue;
		if (ndtmsg->ndtm_family && tbl->family != ndtmsg->ndtm_family)
			continue;
		if (nla_strcmp(tb[NDTA_NAME], tbl->id) == 0) {
			found = true;
			break;
		}
	}

	if (!found)
		return -ENOENT;

	/*
	 * We acquire tbl->lock to be nice to the periodic timers and
	 * make sure they always see a consistent set of values.
	 */
	write_lock_bh(&tbl->lock);

	if (tb[NDTA_PARMS]) {
		struct nlattr *tbp[NDTPA_MAX+1];
		struct neigh_parms *p;
		int i, ifindex = 0;

		err = nla_parse_nested_deprecated(tbp, NDTPA_MAX,
						  tb[NDTA_PARMS],
						  nl_ntbl_parm_policy, extack);
		if (err < 0)
			goto errout_tbl_lock;

		if (tbp[NDTPA_IFINDEX])
			ifindex = nla_get_u32(tbp[NDTPA_IFINDEX]);

		p = lookup_neigh_parms(tbl, net, ifindex);
		if (p == NULL) {
			err = -ENOENT;
			goto errout_tbl_lock;
		}

		for (i = 1; i <= NDTPA_MAX; i++) {
			if (tbp[i] == NULL)
				continue;

			switch (i) {
			case NDTPA_QUEUE_LEN:
				NEIGH_VAR_SET(p, QUEUE_LEN_BYTES,
					      nla_get_u32(tbp[i]) *
					      SKB_TRUESIZE(ETH_FRAME_LEN));
				break;
			case NDTPA_QUEUE_LENBYTES:
				NEIGH_VAR_SET(p, QUEUE_LEN_BYTES,
					      nla_get_u32(tbp[i]));
				break;
			case NDTPA_PROXY_QLEN:
				NEIGH_VAR_SET(p, PROXY_QLEN,
					      nla_get_u32(tbp[i]));
				break;
			case NDTPA_APP_PROBES:
				NEIGH_VAR_SET(p, APP_PROBES,
					      nla_get_u32(tbp[i]));
				break;
			case NDTPA_UCAST_PROBES:
				NEIGH_VAR_SET(p, UCAST_PROBES,
					      nla_get_u32(tbp[i]));
				break;
			case NDTPA_MCAST_PROBES:
				NEIGH_VAR_SET(p, MCAST_PROBES,
					      nla_get_u32(tbp[i]));
				break;
			case NDTPA_MCAST_REPROBES:
				NEIGH_VAR_SET(p, MCAST_REPROBES,
					      nla_get_u32(tbp[i]));
				break;
			case NDTPA_BASE_REACHABLE_TIME:
				NEIGH_VAR_SET(p, BASE_REACHABLE_TIME,
					      nla_get_msecs(tbp[i]));
				/* update reachable_time as well, otherwise, the change will
				 * only be effective after the next time neigh_periodic_work
				 * decides to recompute it (can be multiple minutes)
				 */
				p->reachable_time =
					neigh_rand_reach_time(NEIGH_VAR(p, BASE_REACHABLE_TIME));
				break;
			case NDTPA_GC_STALETIME:
				NEIGH_VAR_SET(p, GC_STALETIME,
					      nla_get_msecs(tbp[i]));
				break;
			case NDTPA_DELAY_PROBE_TIME:
				NEIGH_VAR_SET(p, DELAY_PROBE_TIME,
					      nla_get_msecs(tbp[i]));
				call_netevent_notifiers(NETEVENT_DELAY_PROBE_TIME_UPDATE, p);
				break;
			case NDTPA_INTERVAL_PROBE_TIME_MS:
				NEIGH_VAR_SET(p, INTERVAL_PROBE_TIME_MS,
					      nla_get_msecs(tbp[i]));
				break;
			case NDTPA_RETRANS_TIME:
				NEIGH_VAR_SET(p, RETRANS_TIME,
					      nla_get_msecs(tbp[i]));
				break;
			case NDTPA_ANYCAST_DELAY:
				NEIGH_VAR_SET(p, ANYCAST_DELAY,
					      nla_get_msecs(tbp[i]));
				break;
			case NDTPA_PROXY_DELAY:
				NEIGH_VAR_SET(p, PROXY_DELAY,
					      nla_get_msecs(tbp[i]));
				break;
			case NDTPA_LOCKTIME:
				NEIGH_VAR_SET(p, LOCKTIME,
					      nla_get_msecs(tbp[i]));
				break;
			}
		}
	}

	err = -ENOENT;
	if ((tb[NDTA_THRESH1] || tb[NDTA_THRESH2] ||
	     tb[NDTA_THRESH3] || tb[NDTA_GC_INTERVAL]) &&
	    !net_eq(net, &init_net))
		goto errout_tbl_lock;

	if (tb[NDTA_THRESH1])
		WRITE_ONCE(tbl->gc_thresh1, nla_get_u32(tb[NDTA_THRESH1]));

	if (tb[NDTA_THRESH2])
		WRITE_ONCE(tbl->gc_thresh2, nla_get_u32(tb[NDTA_THRESH2]));

	if (tb[NDTA_THRESH3])
		WRITE_ONCE(tbl->gc_thresh3, nla_get_u32(tb[NDTA_THRESH3]));

	if (tb[NDTA_GC_INTERVAL])
		WRITE_ONCE(tbl->gc_interval, nla_get_msecs(tb[NDTA_GC_INTERVAL]));

	err = 0;

errout_tbl_lock:
	write_unlock_bh(&tbl->lock);
errout:
	return err;
}

static int neightbl_valid_dump_info(const struct nlmsghdr *nlh,
				    struct netlink_ext_ack *extack)
{
	struct ndtmsg *ndtm;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*ndtm))) {
		NL_SET_ERR_MSG(extack, "Invalid header for neighbor table dump request");
		return -EINVAL;
	}

	ndtm = nlmsg_data(nlh);
	if (ndtm->ndtm_pad1  || ndtm->ndtm_pad2) {
		NL_SET_ERR_MSG(extack, "Invalid values in header for neighbor table dump request");
		return -EINVAL;
	}

	if (nlmsg_attrlen(nlh, sizeof(*ndtm))) {
		NL_SET_ERR_MSG(extack, "Invalid data after header in neighbor table dump request");
		return -EINVAL;
	}

	return 0;
}

static int neightbl_dump_info(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct net *net = sock_net(skb->sk);
	int family, tidx, nidx = 0;
	int tbl_skip = cb->args[0];
	int neigh_skip = cb->args[1];
	struct neigh_table *tbl;

	if (cb->strict_check) {
		int err = neightbl_valid_dump_info(nlh, cb->extack);

		if (err < 0)
			return err;
	}

	family = ((struct rtgenmsg *)nlmsg_data(nlh))->rtgen_family;

	for (tidx = 0; tidx < NEIGH_NR_TABLES; tidx++) {
		struct neigh_parms *p;

		tbl = rcu_dereference_rtnl(neigh_tables[tidx]);
		if (!tbl)
			continue;

		if (tidx < tbl_skip || (family && tbl->family != family))
			continue;

		if (neightbl_fill_info(skb, tbl, NETLINK_CB(cb->skb).portid,
				       nlh->nlmsg_seq, RTM_NEWNEIGHTBL,
				       NLM_F_MULTI) < 0)
			break;

		nidx = 0;
		p = list_next_entry(&tbl->parms, list);
		list_for_each_entry_from(p, &tbl->parms_list, list) {
			if (!net_eq(neigh_parms_net(p), net))
				continue;

			if (nidx < neigh_skip)
				goto next;

			if (neightbl_fill_param_info(skb, tbl, p,
						     NETLINK_CB(cb->skb).portid,
						     nlh->nlmsg_seq,
						     RTM_NEWNEIGHTBL,
						     NLM_F_MULTI) < 0)
				goto out;
		next:
			nidx++;
		}

		neigh_skip = 0;
	}
out:
	cb->args[0] = tidx;
	cb->args[1] = nidx;

	return skb->len;
}

static int neigh_fill_info(struct sk_buff *skb, struct neighbour *neigh,
			   u32 pid, u32 seq, int type, unsigned int flags)
{
	u32 neigh_flags, neigh_flags_ext;
	unsigned long now = jiffies;
	struct nda_cacheinfo ci;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ndm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	neigh_flags_ext = neigh->flags >> NTF_EXT_SHIFT;
	neigh_flags     = neigh->flags & NTF_OLD_MASK;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family	 = neigh->ops->family;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags	 = neigh_flags;
	ndm->ndm_type	 = neigh->type;
	ndm->ndm_ifindex = neigh->dev->ifindex;

	if (nla_put(skb, NDA_DST, neigh->tbl->key_len, neigh->primary_key))
		goto nla_put_failure;

	read_lock_bh(&neigh->lock);
	ndm->ndm_state	 = neigh->nud_state;
	if (neigh->nud_state & NUD_VALID) {
		char haddr[MAX_ADDR_LEN];

		neigh_ha_snapshot(haddr, neigh, neigh->dev);
		if (nla_put(skb, NDA_LLADDR, neigh->dev->addr_len, haddr) < 0) {
			read_unlock_bh(&neigh->lock);
			goto nla_put_failure;
		}
	}

	ci.ndm_used	 = jiffies_to_clock_t(now - neigh->used);
	ci.ndm_confirmed = jiffies_to_clock_t(now - neigh->confirmed);
	ci.ndm_updated	 = jiffies_to_clock_t(now - neigh->updated);
	ci.ndm_refcnt	 = refcount_read(&neigh->refcnt) - 1;
	read_unlock_bh(&neigh->lock);

	if (nla_put_u32(skb, NDA_PROBES, atomic_read(&neigh->probes)) ||
	    nla_put(skb, NDA_CACHEINFO, sizeof(ci), &ci))
		goto nla_put_failure;

	if (neigh->protocol && nla_put_u8(skb, NDA_PROTOCOL, neigh->protocol))
		goto nla_put_failure;
	if (neigh_flags_ext && nla_put_u32(skb, NDA_FLAGS_EXT, neigh_flags_ext))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int pneigh_fill_info(struct sk_buff *skb, struct pneigh_entry *pn,
			    u32 pid, u32 seq, int type, unsigned int flags,
			    struct neigh_table *tbl)
{
	u32 neigh_flags, neigh_flags_ext;
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	nlh = nlmsg_put(skb, pid, seq, type, sizeof(*ndm), flags);
	if (nlh == NULL)
		return -EMSGSIZE;

	neigh_flags_ext = pn->flags >> NTF_EXT_SHIFT;
	neigh_flags     = pn->flags & NTF_OLD_MASK;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family	 = tbl->family;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags	 = neigh_flags | NTF_PROXY;
	ndm->ndm_type	 = RTN_UNICAST;
	ndm->ndm_ifindex = pn->dev ? pn->dev->ifindex : 0;
	ndm->ndm_state	 = NUD_NONE;

	if (nla_put(skb, NDA_DST, tbl->key_len, pn->key))
		goto nla_put_failure;

	if (pn->protocol && nla_put_u8(skb, NDA_PROTOCOL, pn->protocol))
		goto nla_put_failure;
	if (neigh_flags_ext && nla_put_u32(skb, NDA_FLAGS_EXT, neigh_flags_ext))
		goto nla_put_failure;

	nlmsg_end(skb, nlh);
	return 0;

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static void neigh_update_notify(struct neighbour *neigh, u32 nlmsg_pid)
{
	call_netevent_notifiers(NETEVENT_NEIGH_UPDATE, neigh);
	__neigh_notify(neigh, RTM_NEWNEIGH, 0, nlmsg_pid);
}

static bool neigh_master_filtered(struct net_device *dev, int master_idx)
{
	struct net_device *master;

	if (!master_idx)
		return false;

	master = dev ? netdev_master_upper_dev_get_rcu(dev) : NULL;

	/* 0 is already used to denote NDA_MASTER wasn't passed, therefore need another
	 * invalid value for ifindex to denote "no master".
	 */
	if (master_idx == -1)
		return !!master;

	if (!master || master->ifindex != master_idx)
		return true;

	return false;
}

static bool neigh_ifindex_filtered(struct net_device *dev, int filter_idx)
{
	if (filter_idx && (!dev || dev->ifindex != filter_idx))
		return true;

	return false;
}

struct neigh_dump_filter {
	int master_idx;
	int dev_idx;
};

static int neigh_dump_table(struct neigh_table *tbl, struct sk_buff *skb,
			    struct netlink_callback *cb,
			    struct neigh_dump_filter *filter)
{
	struct net *net = sock_net(skb->sk);
	struct neighbour *n;
	int err = 0, h, s_h = cb->args[1];
	int idx, s_idx = idx = cb->args[2];
	struct neigh_hash_table *nht;
	unsigned int flags = NLM_F_MULTI;

	if (filter->dev_idx || filter->master_idx)
		flags |= NLM_F_DUMP_FILTERED;

	nht = rcu_dereference(tbl->nht);

	for (h = s_h; h < (1 << nht->hash_shift); h++) {
		if (h > s_h)
			s_idx = 0;
		for (n = rcu_dereference(nht->hash_buckets[h]), idx = 0;
		     n != NULL;
		     n = rcu_dereference(n->next)) {
			if (idx < s_idx || !net_eq(dev_net(n->dev), net))
				goto next;
			if (neigh_ifindex_filtered(n->dev, filter->dev_idx) ||
			    neigh_master_filtered(n->dev, filter->master_idx))
				goto next;
			err = neigh_fill_info(skb, n, NETLINK_CB(cb->skb).portid,
					      cb->nlh->nlmsg_seq,
					      RTM_NEWNEIGH, flags);
			if (err < 0)
				goto out;
next:
			idx++;
		}
	}
out:
	cb->args[1] = h;
	cb->args[2] = idx;
	return err;
}

static int pneigh_dump_table(struct neigh_table *tbl, struct sk_buff *skb,
			     struct netlink_callback *cb,
			     struct neigh_dump_filter *filter)
{
	struct pneigh_entry *n;
	struct net *net = sock_net(skb->sk);
	int err = 0, h, s_h = cb->args[3];
	int idx, s_idx = idx = cb->args[4];
	unsigned int flags = NLM_F_MULTI;

	if (filter->dev_idx || filter->master_idx)
		flags |= NLM_F_DUMP_FILTERED;

	read_lock_bh(&tbl->lock);

	for (h = s_h; h <= PNEIGH_HASHMASK; h++) {
		if (h > s_h)
			s_idx = 0;
		for (n = tbl->phash_buckets[h], idx = 0; n; n = n->next) {
			if (idx < s_idx || pneigh_net(n) != net)
				goto next;
			if (neigh_ifindex_filtered(n->dev, filter->dev_idx) ||
			    neigh_master_filtered(n->dev, filter->master_idx))
				goto next;
			err = pneigh_fill_info(skb, n, NETLINK_CB(cb->skb).portid,
					       cb->nlh->nlmsg_seq,
					       RTM_NEWNEIGH, flags, tbl);
			if (err < 0) {
				read_unlock_bh(&tbl->lock);
				goto out;
			}
		next:
			idx++;
		}
	}

	read_unlock_bh(&tbl->lock);
out:
	cb->args[3] = h;
	cb->args[4] = idx;
	return err;
}

static int neigh_valid_dump_req(const struct nlmsghdr *nlh,
				bool strict_check,
				struct neigh_dump_filter *filter,
				struct netlink_ext_ack *extack)
{
	struct nlattr *tb[NDA_MAX + 1];
	int err, i;

	if (strict_check) {
		struct ndmsg *ndm;

		if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*ndm))) {
			NL_SET_ERR_MSG(extack, "Invalid header for neighbor dump request");
			return -EINVAL;
		}

		ndm = nlmsg_data(nlh);
		if (ndm->ndm_pad1  || ndm->ndm_pad2  || ndm->ndm_ifindex ||
		    ndm->ndm_state || ndm->ndm_type) {
			NL_SET_ERR_MSG(extack, "Invalid values in header for neighbor dump request");
			return -EINVAL;
		}

		if (ndm->ndm_flags & ~NTF_PROXY) {
			NL_SET_ERR_MSG(extack, "Invalid flags in header for neighbor dump request");
			return -EINVAL;
		}

		err = nlmsg_parse_deprecated_strict(nlh, sizeof(struct ndmsg),
						    tb, NDA_MAX, nda_policy,
						    extack);
	} else {
		err = nlmsg_parse_deprecated(nlh, sizeof(struct ndmsg), tb,
					     NDA_MAX, nda_policy, extack);
	}
	if (err < 0)
		return err;

	for (i = 0; i <= NDA_MAX; ++i) {
		if (!tb[i])
			continue;

		/* all new attributes should require strict_check */
		switch (i) {
		case NDA_IFINDEX:
			filter->dev_idx = nla_get_u32(tb[i]);
			break;
		case NDA_MASTER:
			filter->master_idx = nla_get_u32(tb[i]);
			break;
		default:
			if (strict_check) {
				NL_SET_ERR_MSG(extack, "Unsupported attribute in neighbor dump request");
				return -EINVAL;
			}
		}
	}

	return 0;
}

static int neigh_dump_info(struct sk_buff *skb, struct netlink_callback *cb)
{
	const struct nlmsghdr *nlh = cb->nlh;
	struct neigh_dump_filter filter = {};
	struct neigh_table *tbl;
	int t, family, s_t;
	int proxy = 0;
	int err;

	family = ((struct rtgenmsg *)nlmsg_data(nlh))->rtgen_family;

	/* check for full ndmsg structure presence, family member is
	 * the same for both structures
	 */
	if (nlmsg_len(nlh) >= sizeof(struct ndmsg) &&
	    ((struct ndmsg *)nlmsg_data(nlh))->ndm_flags == NTF_PROXY)
		proxy = 1;

	err = neigh_valid_dump_req(nlh, cb->strict_check, &filter, cb->extack);
	if (err < 0 && cb->strict_check)
		return err;

	s_t = cb->args[0];

	rcu_read_lock();
	for (t = 0; t < NEIGH_NR_TABLES; t++) {
		tbl = rcu_dereference(neigh_tables[t]);

		if (!tbl)
			continue;
		if (t < s_t || (family && tbl->family != family))
			continue;
		if (t > s_t)
			memset(&cb->args[1], 0, sizeof(cb->args) -
						sizeof(cb->args[0]));
		if (proxy)
			err = pneigh_dump_table(tbl, skb, cb, &filter);
		else
			err = neigh_dump_table(tbl, skb, cb, &filter);
		if (err < 0)
			break;
	}
	rcu_read_unlock();

	cb->args[0] = t;
	return err;
}

static int neigh_valid_get_req(const struct nlmsghdr *nlh,
			       struct neigh_table **tbl,
			       void **dst, int *dev_idx, u8 *ndm_flags,
			       struct netlink_ext_ack *extack)
{
	struct nlattr *tb[NDA_MAX + 1];
	struct ndmsg *ndm;
	int err, i;

	if (nlh->nlmsg_len < nlmsg_msg_size(sizeof(*ndm))) {
		NL_SET_ERR_MSG(extack, "Invalid header for neighbor get request");
		return -EINVAL;
	}

	ndm = nlmsg_data(nlh);
	if (ndm->ndm_pad1  || ndm->ndm_pad2  || ndm->ndm_state ||
	    ndm->ndm_type) {
		NL_SET_ERR_MSG(extack, "Invalid values in header for neighbor get request");
		return -EINVAL;
	}

	if (ndm->ndm_flags & ~NTF_PROXY) {
		NL_SET_ERR_MSG(extack, "Invalid flags in header for neighbor get request");
		return -EINVAL;
	}

	err = nlmsg_parse_deprecated_strict(nlh, sizeof(struct ndmsg), tb,
					    NDA_MAX, nda_policy, extack);
	if (err < 0)
		return err;

	*ndm_flags = ndm->ndm_flags;
	*dev_idx = ndm->ndm_ifindex;
	*tbl = neigh_find_table(ndm->ndm_family);
	if (*tbl == NULL) {
		NL_SET_ERR_MSG(extack, "Unsupported family in header for neighbor get request");
		return -EAFNOSUPPORT;
	}

	for (i = 0; i <= NDA_MAX; ++i) {
		if (!tb[i])
			continue;

		switch (i) {
		case NDA_DST:
			if (nla_len(tb[i]) != (int)(*tbl)->key_len) {
				NL_SET_ERR_MSG(extack, "Invalid network address in neighbor get request");
				return -EINVAL;
			}
			*dst = nla_data(tb[i]);
			break;
		default:
			NL_SET_ERR_MSG(extack, "Unsupported attribute in neighbor get request");
			return -EINVAL;
		}
	}

	return 0;
}

static inline size_t neigh_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg))
	       + nla_total_size(MAX_ADDR_LEN) /* NDA_DST */
	       + nla_total_size(MAX_ADDR_LEN) /* NDA_LLADDR */
	       + nla_total_size(sizeof(struct nda_cacheinfo))
	       + nla_total_size(4)  /* NDA_PROBES */
	       + nla_total_size(4)  /* NDA_FLAGS_EXT */
	       + nla_total_size(1); /* NDA_PROTOCOL */
}

static int neigh_get_reply(struct net *net, struct neighbour *neigh,
			   u32 pid, u32 seq)
{
	struct sk_buff *skb;
	int err = 0;

	skb = nlmsg_new(neigh_nlmsg_size(), GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	err = neigh_fill_info(skb, neigh, pid, seq, RTM_NEWNEIGH, 0);
	if (err) {
		kfree_skb(skb);
		goto errout;
	}

	err = rtnl_unicast(skb, net, pid);
errout:
	return err;
}

static inline size_t pneigh_nlmsg_size(void)
{
	return NLMSG_ALIGN(sizeof(struct ndmsg))
	       + nla_total_size(MAX_ADDR_LEN) /* NDA_DST */
	       + nla_total_size(4)  /* NDA_FLAGS_EXT */
	       + nla_total_size(1); /* NDA_PROTOCOL */
}

static int pneigh_get_reply(struct net *net, struct pneigh_entry *neigh,
			    u32 pid, u32 seq, struct neigh_table *tbl)
{
	struct sk_buff *skb;
	int err = 0;

	skb = nlmsg_new(pneigh_nlmsg_size(), GFP_KERNEL);
	if (!skb)
		return -ENOBUFS;

	err = pneigh_fill_info(skb, neigh, pid, seq, RTM_NEWNEIGH, 0, tbl);
	if (err) {
		kfree_skb(skb);
		goto errout;
	}

	err = rtnl_unicast(skb, net, pid);
errout:
	return err;
}

static int neigh_get(struct sk_buff *in_skb, struct nlmsghdr *nlh,
		     struct netlink_ext_ack *extack)
{
	struct net *net = sock_net(in_skb->sk);
	struct net_device *dev = NULL;
	struct neigh_table *tbl = NULL;
	struct neighbour *neigh;
	void *dst = NULL;
	u8 ndm_flags = 0;
	int dev_idx = 0;
	int err;

	err = neigh_valid_get_req(nlh, &tbl, &dst, &dev_idx, &ndm_flags,
				  extack);
	if (err < 0)
		return err;

	if (dev_idx) {
		dev = __dev_get_by_index(net, dev_idx);
		if (!dev) {
			NL_SET_ERR_MSG(extack, "Unknown device ifindex");
			return -ENODEV;
		}
	}

	if (!dst) {
		NL_SET_ERR_MSG(extack, "Network address not specified");
		return -EINVAL;
	}

	if (ndm_flags & NTF_PROXY) {
		struct pneigh_entry *pn;

		pn = pneigh_lookup(tbl, net, dst, dev, 0);
		if (!pn) {
			NL_SET_ERR_MSG(extack, "Proxy neighbour entry not found");
			return -ENOENT;
		}
		return pneigh_get_reply(net, pn, NETLINK_CB(in_skb).portid,
					nlh->nlmsg_seq, tbl);
	}

	if (!dev) {
		NL_SET_ERR_MSG(extack, "No device specified");
		return -EINVAL;
	}

	neigh = neigh_lookup(tbl, dst, dev);
	if (!neigh) {
		NL_SET_ERR_MSG(extack, "Neighbour entry not found");
		return -ENOENT;
	}

	err = neigh_get_reply(net, neigh, NETLINK_CB(in_skb).portid,
			      nlh->nlmsg_seq);

	neigh_release(neigh);

	return err;
}

void neigh_for_each(struct neigh_table *tbl, void (*cb)(struct neighbour *, void *), void *cookie)
{
	int chain;
	struct neigh_hash_table *nht;

	rcu_read_lock();
	nht = rcu_dereference(tbl->nht);

	read_lock_bh(&tbl->lock); /* avoid resizes */
	for (chain = 0; chain < (1 << nht->hash_shift); chain++) {
		struct neighbour *n;

		for (n = rcu_dereference(nht->hash_buckets[chain]);
		     n != NULL;
		     n = rcu_dereference(n->next))
			cb(n, cookie);
	}
	read_unlock_bh(&tbl->lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(neigh_for_each);

/* The tbl->lock must be held as a writer and BH disabled. */
void __neigh_for_each_release(struct neigh_table *tbl,
			      int (*cb)(struct neighbour *))
{
	int chain;
	struct neigh_hash_table *nht;

	nht = rcu_dereference_protected(tbl->nht,
					lockdep_is_held(&tbl->lock));
	for (chain = 0; chain < (1 << nht->hash_shift); chain++) {
		struct neighbour *n;
		struct neighbour __rcu **np;

		np = &nht->hash_buckets[chain];
		while ((n = rcu_dereference_protected(*np,
					lockdep_is_held(&tbl->lock))) != NULL) {
			int release;

			write_lock(&n->lock);
			release = cb(n);
			if (release) {
				rcu_assign_pointer(*np,
					rcu_dereference_protected(n->next,
						lockdep_is_held(&tbl->lock)));
				neigh_mark_dead(n);
			} else
				np = &n->next;
			write_unlock(&n->lock);
			if (release)
				neigh_cleanup_and_release(n);
		}
	}
}
EXPORT_SYMBOL(__neigh_for_each_release);

int neigh_xmit(int index, struct net_device *dev,
	       const void *addr, struct sk_buff *skb)
{
	int err = -EAFNOSUPPORT;

	if (likely(index < NEIGH_NR_TABLES)) {
		struct neigh_table *tbl;
		struct neighbour *neigh;

		rcu_read_lock();
		tbl = rcu_dereference(neigh_tables[index]);
		if (!tbl)
			goto out_unlock;
		if (index == NEIGH_ARP_TABLE) {
			u32 key = *((u32 *)addr);

			neigh = __ipv4_neigh_lookup_noref(dev, key);
		} else {
			neigh = __neigh_lookup_noref(tbl, addr, dev);
		}
		if (!neigh)
			neigh = __neigh_create(tbl, addr, dev, false);
		err = PTR_ERR(neigh);
		if (IS_ERR(neigh)) {
			rcu_read_unlock();
			goto out_kfree_skb;
		}
		err = READ_ONCE(neigh->output)(neigh, skb);
out_unlock:
		rcu_read_unlock();
	}
	else if (index == NEIGH_LINK_TABLE) {
		err = dev_hard_header(skb, dev, ntohs(skb->protocol),
				      addr, NULL, skb->len);
		if (err < 0)
			goto out_kfree_skb;
		err = dev_queue_xmit(skb);
	}
out:
	return err;
out_kfree_skb:
	kfree_skb(skb);
	goto out;
}
EXPORT_SYMBOL(neigh_xmit);

#ifdef CONFIG_PROC_FS

static struct neighbour *neigh_get_first(struct seq_file *seq)
{
	struct neigh_seq_state *state = seq->private;
	struct net *net = seq_file_net(seq);
	struct neigh_hash_table *nht = state->nht;
	struct neighbour *n = NULL;
	int bucket;

	state->flags &= ~NEIGH_SEQ_IS_PNEIGH;
	for (bucket = 0; bucket < (1 << nht->hash_shift); bucket++) {
		n = rcu_dereference(nht->hash_buckets[bucket]);

		while (n) {
			if (!net_eq(dev_net(n->dev), net))
				goto next;
			if (state->neigh_sub_iter) {
				loff_t fakep = 0;
				void *v;

				v = state->neigh_sub_iter(state, n, &fakep);
				if (!v)
					goto next;
			}
			if (!(state->flags & NEIGH_SEQ_SKIP_NOARP))
				break;
			if (READ_ONCE(n->nud_state) & ~NUD_NOARP)
				break;
next:
			n = rcu_dereference(n->next);
		}

		if (n)
			break;
	}
	state->bucket = bucket;

	return n;
}

static struct neighbour *neigh_get_next(struct seq_file *seq,
					struct neighbour *n,
					loff_t *pos)
{
	struct neigh_seq_state *state = seq->private;
	struct net *net = seq_file_net(seq);
	struct neigh_hash_table *nht = state->nht;

	if (state->neigh_sub_iter) {
		void *v = state->neigh_sub_iter(state, n, pos);
		if (v)
			return n;
	}
	n = rcu_dereference(n->next);

	while (1) {
		while (n) {
			if (!net_eq(dev_net(n->dev), net))
				goto next;
			if (state->neigh_sub_iter) {
				void *v = state->neigh_sub_iter(state, n, pos);
				if (v)
					return n;
				goto next;
			}
			if (!(state->flags & NEIGH_SEQ_SKIP_NOARP))
				break;

			if (READ_ONCE(n->nud_state) & ~NUD_NOARP)
				break;
next:
			n = rcu_dereference(n->next);
		}

		if (n)
			break;

		if (++state->bucket >= (1 << nht->hash_shift))
			break;

		n = rcu_dereference(nht->hash_buckets[state->bucket]);
	}

	if (n && pos)
		--(*pos);
	return n;
}

static struct neighbour *neigh_get_idx(struct seq_file *seq, loff_t *pos)
{
	struct neighbour *n = neigh_get_first(seq);

	if (n) {
		--(*pos);
		while (*pos) {
			n = neigh_get_next(seq, n, pos);
			if (!n)
				break;
		}
	}
	return *pos ? NULL : n;
}

static struct pneigh_entry *pneigh_get_first(struct seq_file *seq)
{
	struct neigh_seq_state *state = seq->private;
	struct net *net = seq_file_net(seq);
	struct neigh_table *tbl = state->tbl;
	struct pneigh_entry *pn = NULL;
	int bucket;

	state->flags |= NEIGH_SEQ_IS_PNEIGH;
	for (bucket = 0; bucket <= PNEIGH_HASHMASK; bucket++) {
		pn = tbl->phash_buckets[bucket];
		while (pn && !net_eq(pneigh_net(pn), net))
			pn = pn->next;
		if (pn)
			break;
	}
	state->bucket = bucket;

	return pn;
}

static struct pneigh_entry *pneigh_get_next(struct seq_file *seq,
					    struct pneigh_entry *pn,
					    loff_t *pos)
{
	struct neigh_seq_state *state = seq->private;
	struct net *net = seq_file_net(seq);
	struct neigh_table *tbl = state->tbl;

	do {
		pn = pn->next;
	} while (pn && !net_eq(pneigh_net(pn), net));

	while (!pn) {
		if (++state->bucket > PNEIGH_HASHMASK)
			break;
		pn = tbl->phash_buckets[state->bucket];
		while (pn && !net_eq(pneigh_net(pn), net))
			pn = pn->next;
		if (pn)
			break;
	}

	if (pn && pos)
		--(*pos);

	return pn;
}

static struct pneigh_entry *pneigh_get_idx(struct seq_file *seq, loff_t *pos)
{
	struct pneigh_entry *pn = pneigh_get_first(seq);

	if (pn) {
		--(*pos);
		while (*pos) {
			pn = pneigh_get_next(seq, pn, pos);
			if (!pn)
				break;
		}
	}
	return *pos ? NULL : pn;
}

static void *neigh_get_idx_any(struct seq_file *seq, loff_t *pos)
{
	struct neigh_seq_state *state = seq->private;
	void *rc;
	loff_t idxpos = *pos;

	rc = neigh_get_idx(seq, &idxpos);
	if (!rc && !(state->flags & NEIGH_SEQ_NEIGH_ONLY))
		rc = pneigh_get_idx(seq, &idxpos);

	return rc;
}

void *neigh_seq_start(struct seq_file *seq, loff_t *pos, struct neigh_table *tbl, unsigned int neigh_seq_flags)
	__acquires(tbl->lock)
	__acquires(rcu)
{
	struct neigh_seq_state *state = seq->private;

	state->tbl = tbl;
	state->bucket = 0;
	state->flags = (neigh_seq_flags & ~NEIGH_SEQ_IS_PNEIGH);

	rcu_read_lock();
	state->nht = rcu_dereference(tbl->nht);
	read_lock_bh(&tbl->lock);

	return *pos ? neigh_get_idx_any(seq, pos) : SEQ_START_TOKEN;
}
EXPORT_SYMBOL(neigh_seq_start);

void *neigh_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct neigh_seq_state *state;
	void *rc;

	if (v == SEQ_START_TOKEN) {
		rc = neigh_get_first(seq);
		goto out;
	}

	state = seq->private;
	if (!(state->flags & NEIGH_SEQ_IS_PNEIGH)) {
		rc = neigh_get_next(seq, v, NULL);
		if (rc)
			goto out;
		if (!(state->flags & NEIGH_SEQ_NEIGH_ONLY))
			rc = pneigh_get_first(seq);
	} else {
		BUG_ON(state->flags & NEIGH_SEQ_NEIGH_ONLY);
		rc = pneigh_get_next(seq, v, NULL);
	}
out:
	++(*pos);
	return rc;
}
EXPORT_SYMBOL(neigh_seq_next);

void neigh_seq_stop(struct seq_file *seq, void *v)
	__releases(tbl->lock)
	__releases(rcu)
{
	struct neigh_seq_state *state = seq->private;
	struct neigh_table *tbl = state->tbl;

	read_unlock_bh(&tbl->lock);
	rcu_read_unlock();
}
EXPORT_SYMBOL(neigh_seq_stop);

/* statistics via seq_file */

static void *neigh_stat_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct neigh_table *tbl = pde_data(file_inode(seq->file));
	int cpu;

	if (*pos == 0)
		return SEQ_START_TOKEN;

	for (cpu = *pos-1; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu+1;
		return per_cpu_ptr(tbl->stats, cpu);
	}
	return NULL;
}

static void *neigh_stat_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct neigh_table *tbl = pde_data(file_inode(seq->file));
	int cpu;

	for (cpu = *pos; cpu < nr_cpu_ids; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu+1;
		return per_cpu_ptr(tbl->stats, cpu);
	}
	(*pos)++;
	return NULL;
}

static void neigh_stat_seq_stop(struct seq_file *seq, void *v)
{

}

static int neigh_stat_seq_show(struct seq_file *seq, void *v)
{
	struct neigh_table *tbl = pde_data(file_inode(seq->file));
	struct neigh_statistics *st = v;

	if (v == SEQ_START_TOKEN) {
		seq_puts(seq, "entries  allocs   destroys hash_grows lookups  hits     res_failed rcv_probes_mcast rcv_probes_ucast periodic_gc_runs forced_gc_runs unresolved_discards table_fulls\n");
		return 0;
	}

	seq_printf(seq, "%08x %08lx %08lx %08lx   %08lx %08lx %08lx   "
			"%08lx         %08lx         %08lx         "
			"%08lx       %08lx            %08lx\n",
		   atomic_read(&tbl->entries),

		   st->allocs,
		   st->destroys,
		   st->hash_grows,

		   st->lookups,
		   st->hits,

		   st->res_failed,

		   st->rcv_probes_mcast,
		   st->rcv_probes_ucast,

		   st->periodic_gc_runs,
		   st->forced_gc_runs,
		   st->unres_discards,
		   st->table_fulls
		   );

	return 0;
}

static const struct seq_operations neigh_stat_seq_ops = {
	.start	= neigh_stat_seq_start,
	.next	= neigh_stat_seq_next,
	.stop	= neigh_stat_seq_stop,
	.show	= neigh_stat_seq_show,
};
#endif /* CONFIG_PROC_FS */

static void __neigh_notify(struct neighbour *n, int type, int flags,
			   u32 pid)
{
	struct net *net = dev_net(n->dev);
	struct sk_buff *skb;
	int err = -ENOBUFS;

	skb = nlmsg_new(neigh_nlmsg_size(), GFP_ATOMIC);
	if (skb == NULL)
		goto errout;

	err = neigh_fill_info(skb, n, pid, 0, type, flags);
	if (err < 0) {
		/* -EMSGSIZE implies BUG in neigh_nlmsg_size() */
		WARN_ON(err == -EMSGSIZE);
		kfree_skb(skb);
		goto errout;
	}
	rtnl_notify(skb, net, 0, RTNLGRP_NEIGH, NULL, GFP_ATOMIC);
	return;
errout:
	rtnl_set_sk_err(net, RTNLGRP_NEIGH, err);
}

void neigh_app_ns(struct neighbour *n)
{
	__neigh_notify(n, RTM_GETNEIGH, NLM_F_REQUEST, 0);
}
EXPORT_SYMBOL(neigh_app_ns);

#ifdef CONFIG_SYSCTL
static int unres_qlen_max = INT_MAX / SKB_TRUESIZE(ETH_FRAME_LEN);

static int proc_unres_qlen(const struct ctl_table *ctl, int write,
			   void *buffer, size_t *lenp, loff_t *ppos)
{
	int size, ret;
	struct ctl_table tmp = *ctl;

	tmp.extra1 = SYSCTL_ZERO;
	tmp.extra2 = &unres_qlen_max;
	tmp.data = &size;

	size = *(int *)ctl->data / SKB_TRUESIZE(ETH_FRAME_LEN);
	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);

	if (write && !ret)
		*(int *)ctl->data = size * SKB_TRUESIZE(ETH_FRAME_LEN);
	return ret;
}

static void neigh_copy_dflt_parms(struct net *net, struct neigh_parms *p,
				  int index)
{
	struct net_device *dev;
	int family = neigh_parms_family(p);

	rcu_read_lock();
	for_each_netdev_rcu(net, dev) {
		struct neigh_parms *dst_p =
				neigh_get_dev_parms_rcu(dev, family);

		if (dst_p && !test_bit(index, dst_p->data_state))
			dst_p->data[index] = p->data[index];
	}
	rcu_read_unlock();
}

static void neigh_proc_update(const struct ctl_table *ctl, int write)
{
	struct net_device *dev = ctl->extra1;
	struct neigh_parms *p = ctl->extra2;
	struct net *net = neigh_parms_net(p);
	int index = (int *) ctl->data - p->data;

	if (!write)
		return;

	set_bit(index, p->data_state);
	if (index == NEIGH_VAR_DELAY_PROBE_TIME)
		call_netevent_notifiers(NETEVENT_DELAY_PROBE_TIME_UPDATE, p);
	if (!dev) /* NULL dev means this is default value */
		neigh_copy_dflt_parms(net, p, index);
}

static int neigh_proc_dointvec_zero_intmax(const struct ctl_table *ctl, int write,
					   void *buffer, size_t *lenp,
					   loff_t *ppos)
{
	struct ctl_table tmp = *ctl;
	int ret;

	tmp.extra1 = SYSCTL_ZERO;
	tmp.extra2 = SYSCTL_INT_MAX;

	ret = proc_dointvec_minmax(&tmp, write, buffer, lenp, ppos);
	neigh_proc_update(ctl, write);
	return ret;
}

static int neigh_proc_dointvec_ms_jiffies_positive(const struct ctl_table *ctl, int write,
						   void *buffer, size_t *lenp, loff_t *ppos)
{
	struct ctl_table tmp = *ctl;
	int ret;

	int min = msecs_to_jiffies(1);

	tmp.extra1 = &min;
	tmp.extra2 = NULL;

	ret = proc_dointvec_ms_jiffies_minmax(&tmp, write, buffer, lenp, ppos);
	neigh_proc_update(ctl, write);
	return ret;
}

int neigh_proc_dointvec(const struct ctl_table *ctl, int write, void *buffer,
			size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec(ctl, write, buffer, lenp, ppos);

	neigh_proc_update(ctl, write);
	return ret;
}
EXPORT_SYMBOL(neigh_proc_dointvec);

int neigh_proc_dointvec_jiffies(const struct ctl_table *ctl, int write, void *buffer,
				size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec_jiffies(ctl, write, buffer, lenp, ppos);

	neigh_proc_update(ctl, write);
	return ret;
}
EXPORT_SYMBOL(neigh_proc_dointvec_jiffies);

static int neigh_proc_dointvec_userhz_jiffies(const struct ctl_table *ctl, int write,
					      void *buffer, size_t *lenp,
					      loff_t *ppos)
{
	int ret = proc_dointvec_userhz_jiffies(ctl, write, buffer, lenp, ppos);

	neigh_proc_update(ctl, write);
	return ret;
}

int neigh_proc_dointvec_ms_jiffies(const struct ctl_table *ctl, int write,
				   void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret = proc_dointvec_ms_jiffies(ctl, write, buffer, lenp, ppos);

	neigh_proc_update(ctl, write);
	return ret;
}
EXPORT_SYMBOL(neigh_proc_dointvec_ms_jiffies);

static int neigh_proc_dointvec_unres_qlen(const struct ctl_table *ctl, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos)
{
	int ret = proc_unres_qlen(ctl, write, buffer, lenp, ppos);

	neigh_proc_update(ctl, write);
	return ret;
}

static int neigh_proc_base_reachable_time(const struct ctl_table *ctl, int write,
					  void *buffer, size_t *lenp,
					  loff_t *ppos)
{
	struct neigh_parms *p = ctl->extra2;
	int ret;

	if (strcmp(ctl->procname, "base_reachable_time") == 0)
		ret = neigh_proc_dointvec_jiffies(ctl, write, buffer, lenp, ppos);
	else if (strcmp(ctl->procname, "base_reachable_time_ms") == 0)
		ret = neigh_proc_dointvec_ms_jiffies(ctl, write, buffer, lenp, ppos);
	else
		ret = -1;

	if (write && ret == 0) {
		/* update reachable_time as well, otherwise, the change will
		 * only be effective after the next time neigh_periodic_work
		 * decides to recompute it
		 */
		p->reachable_time =
			neigh_rand_reach_time(NEIGH_VAR(p, BASE_REACHABLE_TIME));
	}
	return ret;
}

#define NEIGH_PARMS_DATA_OFFSET(index)	\
	(&((struct neigh_parms *) 0)->data[index])

#define NEIGH_SYSCTL_ENTRY(attr, data_attr, name, mval, proc) \
	[NEIGH_VAR_ ## attr] = { \
		.procname	= name, \
		.data		= NEIGH_PARMS_DATA_OFFSET(NEIGH_VAR_ ## data_attr), \
		.maxlen		= sizeof(int), \
		.mode		= mval, \
		.proc_handler	= proc, \
	}

#define NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(attr, name) \
	NEIGH_SYSCTL_ENTRY(attr, attr, name, 0644, neigh_proc_dointvec_zero_intmax)

#define NEIGH_SYSCTL_JIFFIES_ENTRY(attr, name) \
	NEIGH_SYSCTL_ENTRY(attr, attr, name, 0644, neigh_proc_dointvec_jiffies)

#define NEIGH_SYSCTL_USERHZ_JIFFIES_ENTRY(attr, name) \
	NEIGH_SYSCTL_ENTRY(attr, attr, name, 0644, neigh_proc_dointvec_userhz_jiffies)

#define NEIGH_SYSCTL_MS_JIFFIES_POSITIVE_ENTRY(attr, name) \
	NEIGH_SYSCTL_ENTRY(attr, attr, name, 0644, neigh_proc_dointvec_ms_jiffies_positive)

#define NEIGH_SYSCTL_MS_JIFFIES_REUSED_ENTRY(attr, data_attr, name) \
	NEIGH_SYSCTL_ENTRY(attr, data_attr, name, 0644, neigh_proc_dointvec_ms_jiffies)

#define NEIGH_SYSCTL_UNRES_QLEN_REUSED_ENTRY(attr, data_attr, name) \
	NEIGH_SYSCTL_ENTRY(attr, data_attr, name, 0644, neigh_proc_dointvec_unres_qlen)

static struct neigh_sysctl_table {
	struct ctl_table_header *sysctl_header;
	struct ctl_table neigh_vars[NEIGH_VAR_MAX];
} neigh_sysctl_template __read_mostly = {
	.neigh_vars = {
		NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(MCAST_PROBES, "mcast_solicit"),
		NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(UCAST_PROBES, "ucast_solicit"),
		NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(APP_PROBES, "app_solicit"),
		NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(MCAST_REPROBES, "mcast_resolicit"),
		NEIGH_SYSCTL_USERHZ_JIFFIES_ENTRY(RETRANS_TIME, "retrans_time"),
		NEIGH_SYSCTL_JIFFIES_ENTRY(BASE_REACHABLE_TIME, "base_reachable_time"),
		NEIGH_SYSCTL_JIFFIES_ENTRY(DELAY_PROBE_TIME, "delay_first_probe_time"),
		NEIGH_SYSCTL_MS_JIFFIES_POSITIVE_ENTRY(INTERVAL_PROBE_TIME_MS,
						       "interval_probe_time_ms"),
		NEIGH_SYSCTL_JIFFIES_ENTRY(GC_STALETIME, "gc_stale_time"),
		NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(QUEUE_LEN_BYTES, "unres_qlen_bytes"),
		NEIGH_SYSCTL_ZERO_INTMAX_ENTRY(PROXY_QLEN, "proxy_qlen"),
		NEIGH_SYSCTL_USERHZ_JIFFIES_ENTRY(ANYCAST_DELAY, "anycast_delay"),
		NEIGH_SYSCTL_USERHZ_JIFFIES_ENTRY(PROXY_DELAY, "proxy_delay"),
		NEIGH_SYSCTL_USERHZ_JIFFIES_ENTRY(LOCKTIME, "locktime"),
		NEIGH_SYSCTL_UNRES_QLEN_REUSED_ENTRY(QUEUE_LEN, QUEUE_LEN_BYTES, "unres_qlen"),
		NEIGH_SYSCTL_MS_JIFFIES_REUSED_ENTRY(RETRANS_TIME_MS, RETRANS_TIME, "retrans_time_ms"),
		NEIGH_SYSCTL_MS_JIFFIES_REUSED_ENTRY(BASE_REACHABLE_TIME_MS, BASE_REACHABLE_TIME, "base_reachable_time_ms"),
		[NEIGH_VAR_GC_INTERVAL] = {
			.procname	= "gc_interval",
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.proc_handler	= proc_dointvec_jiffies,
		},
		[NEIGH_VAR_GC_THRESH1] = {
			.procname	= "gc_thresh1",
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.extra1		= SYSCTL_ZERO,
			.extra2		= SYSCTL_INT_MAX,
			.proc_handler	= proc_dointvec_minmax,
		},
		[NEIGH_VAR_GC_THRESH2] = {
			.procname	= "gc_thresh2",
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.extra1		= SYSCTL_ZERO,
			.extra2		= SYSCTL_INT_MAX,
			.proc_handler	= proc_dointvec_minmax,
		},
		[NEIGH_VAR_GC_THRESH3] = {
			.procname	= "gc_thresh3",
			.maxlen		= sizeof(int),
			.mode		= 0644,
			.extra1		= SYSCTL_ZERO,
			.extra2		= SYSCTL_INT_MAX,
			.proc_handler	= proc_dointvec_minmax,
		},
	},
};

int neigh_sysctl_register(struct net_device *dev, struct neigh_parms *p,
			  proc_handler *handler)
{
	int i;
	struct neigh_sysctl_table *t;
	const char *dev_name_source;
	char neigh_path[ sizeof("net//neigh/") + IFNAMSIZ + IFNAMSIZ ];
	char *p_name;
	size_t neigh_vars_size;

	t = kmemdup(&neigh_sysctl_template, sizeof(*t), GFP_KERNEL_ACCOUNT);
	if (!t)
		goto err;

	for (i = 0; i < NEIGH_VAR_GC_INTERVAL; i++) {
		t->neigh_vars[i].data += (long) p;
		t->neigh_vars[i].extra1 = dev;
		t->neigh_vars[i].extra2 = p;
	}

	neigh_vars_size = ARRAY_SIZE(t->neigh_vars);
	if (dev) {
		dev_name_source = dev->name;
		/* Terminate the table early */
		neigh_vars_size = NEIGH_VAR_BASE_REACHABLE_TIME_MS + 1;
	} else {
		struct neigh_table *tbl = p->tbl;
		dev_name_source = "default";
		t->neigh_vars[NEIGH_VAR_GC_INTERVAL].data = &tbl->gc_interval;
		t->neigh_vars[NEIGH_VAR_GC_THRESH1].data = &tbl->gc_thresh1;
		t->neigh_vars[NEIGH_VAR_GC_THRESH2].data = &tbl->gc_thresh2;
		t->neigh_vars[NEIGH_VAR_GC_THRESH3].data = &tbl->gc_thresh3;
	}

	if (handler) {
		/* RetransTime */
		t->neigh_vars[NEIGH_VAR_RETRANS_TIME].proc_handler = handler;
		/* ReachableTime */
		t->neigh_vars[NEIGH_VAR_BASE_REACHABLE_TIME].proc_handler = handler;
		/* RetransTime (in milliseconds)*/
		t->neigh_vars[NEIGH_VAR_RETRANS_TIME_MS].proc_handler = handler;
		/* ReachableTime (in milliseconds) */
		t->neigh_vars[NEIGH_VAR_BASE_REACHABLE_TIME_MS].proc_handler = handler;
	} else {
		/* Those handlers will update p->reachable_time after
		 * base_reachable_time(_ms) is set to ensure the new timer starts being
		 * applied after the next neighbour update instead of waiting for
		 * neigh_periodic_work to update its value (can be multiple minutes)
		 * So any handler that replaces them should do this as well
		 */
		/* ReachableTime */
		t->neigh_vars[NEIGH_VAR_BASE_REACHABLE_TIME].proc_handler =
			neigh_proc_base_reachable_time;
		/* ReachableTime (in milliseconds) */
		t->neigh_vars[NEIGH_VAR_BASE_REACHABLE_TIME_MS].proc_handler =
			neigh_proc_base_reachable_time;
	}

	switch (neigh_parms_family(p)) {
	case AF_INET:
	      p_name = "ipv4";
	      break;
	case AF_INET6:
	      p_name = "ipv6";
	      break;
	default:
	      BUG();
	}

	snprintf(neigh_path, sizeof(neigh_path), "net/%s/neigh/%s",
		p_name, dev_name_source);
	t->sysctl_header = register_net_sysctl_sz(neigh_parms_net(p),
						  neigh_path, t->neigh_vars,
						  neigh_vars_size);
	if (!t->sysctl_header)
		goto free;

	p->sysctl_table = t;
	return 0;

free:
	kfree(t);
err:
	return -ENOBUFS;
}
EXPORT_SYMBOL(neigh_sysctl_register);

void neigh_sysctl_unregister(struct neigh_parms *p)
{
	if (p->sysctl_table) {
		struct neigh_sysctl_table *t = p->sysctl_table;
		p->sysctl_table = NULL;
		unregister_net_sysctl_table(t->sysctl_header);
		kfree(t);
	}
}
EXPORT_SYMBOL(neigh_sysctl_unregister);

#endif	/* CONFIG_SYSCTL */

static int __init neigh_init(void)
{
	rtnl_register(PF_UNSPEC, RTM_NEWNEIGH, neigh_add, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_DELNEIGH, neigh_delete, NULL, 0);
	rtnl_register(PF_UNSPEC, RTM_GETNEIGH, neigh_get, neigh_dump_info,
		      RTNL_FLAG_DUMP_UNLOCKED);

	rtnl_register(PF_UNSPEC, RTM_GETNEIGHTBL, NULL, neightbl_dump_info,
		      0);
	rtnl_register(PF_UNSPEC, RTM_SETNEIGHTBL, neightbl_set, NULL, 0);

	return 0;
}

subsys_initcall(neigh_init);
