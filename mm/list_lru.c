// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013 Red Hat, Inc. and Parallels Inc. All rights reserved.
 * Authors: David Chinner and Glauber Costa
 *
 * Generic LRU infrastructure
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/list_lru.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/memcontrol.h>
#include "slab.h"
#include "internal.h"

#ifdef CONFIG_MEMCG
static LIST_HEAD(memcg_list_lrus);
static DEFINE_MUTEX(list_lrus_mutex);

static inline bool list_lru_memcg_aware(struct list_lru *lru)
{
	return lru->memcg_aware;
}

static void list_lru_register(struct list_lru *lru)
{
	if (!list_lru_memcg_aware(lru))
		return;

	mutex_lock(&list_lrus_mutex);
	list_add(&lru->list, &memcg_list_lrus);
	mutex_unlock(&list_lrus_mutex);
}

static void list_lru_unregister(struct list_lru *lru)
{
	if (!list_lru_memcg_aware(lru))
		return;

	mutex_lock(&list_lrus_mutex);
	list_del(&lru->list);
	mutex_unlock(&list_lrus_mutex);
}

static int lru_shrinker_id(struct list_lru *lru)
{
	return lru->shrinker_id;
}

static inline struct list_lru_one *
list_lru_from_memcg_idx(struct list_lru *lru, int nid, int idx)
{
	if (list_lru_memcg_aware(lru) && idx >= 0) {
		struct list_lru_memcg *mlru = xa_load(&lru->xa, idx);

		return mlru ? &mlru->node[nid] : NULL;
	}
	return &lru->node[nid].lru;
}

static inline bool lock_list_lru(struct list_lru_one *l, bool irq)
{
	if (irq)
		spin_lock_irq(&l->lock);
	else
		spin_lock(&l->lock);
	if (unlikely(READ_ONCE(l->nr_items) == LONG_MIN)) {
		if (irq)
			spin_unlock_irq(&l->lock);
		else
			spin_unlock(&l->lock);
		return false;
	}
	return true;
}

static inline struct list_lru_one *
lock_list_lru_of_memcg(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		       bool irq, bool skip_empty)
{
	struct list_lru_one *l;

	rcu_read_lock();
again:
	l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
	if (likely(l) && lock_list_lru(l, irq)) {
		rcu_read_unlock();
		return l;
	}
	/*
	 * Caller may simply bail out if raced with reparenting or
	 * may iterate through the list_lru and expect empty slots.
	 */
	if (skip_empty) {
		rcu_read_unlock();
		return NULL;
	}
	VM_WARN_ON(!css_is_dying(&memcg->css));
	memcg = parent_mem_cgroup(memcg);
	goto again;
}

static inline void unlock_list_lru(struct list_lru_one *l, bool irq_off)
{
	if (irq_off)
		spin_unlock_irq(&l->lock);
	else
		spin_unlock(&l->lock);
}
#else
static void list_lru_register(struct list_lru *lru)
{
}

static void list_lru_unregister(struct list_lru *lru)
{
}

static int lru_shrinker_id(struct list_lru *lru)
{
	return -1;
}

static inline bool list_lru_memcg_aware(struct list_lru *lru)
{
	return false;
}

static inline struct list_lru_one *
list_lru_from_memcg_idx(struct list_lru *lru, int nid, int idx)
{
	return &lru->node[nid].lru;
}

static inline struct list_lru_one *
lock_list_lru_of_memcg(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		       bool irq, bool skip_empty)
{
	struct list_lru_one *l = &lru->node[nid].lru;

	if (irq)
		spin_lock_irq(&l->lock);
	else
		spin_lock(&l->lock);

	return l;
}

static inline void unlock_list_lru(struct list_lru_one *l, bool irq_off)
{
	if (irq_off)
		spin_unlock_irq(&l->lock);
	else
		spin_unlock(&l->lock);
}
#endif /* CONFIG_MEMCG */

/* The caller must ensure the memcg lifetime. */
bool list_lru_add(struct list_lru *lru, struct list_head *item, int nid,
		  struct mem_cgroup *memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;

	l = lock_list_lru_of_memcg(lru, nid, memcg, false, false);
	if (!l)
		return false;
	if (list_empty(item)) {
		list_add_tail(item, &l->list);
		/* Set shrinker bit if the first element was added */
		if (!l->nr_items++)
			set_shrinker_bit(memcg, nid, lru_shrinker_id(lru));
		unlock_list_lru(l, false);
		atomic_long_inc(&nlru->nr_items);
		return true;
	}
	unlock_list_lru(l, false);
	return false;
}

bool list_lru_add_obj(struct list_lru *lru, struct list_head *item)
{
	bool ret;
	int nid = page_to_nid(virt_to_page(item));

	if (list_lru_memcg_aware(lru)) {
		rcu_read_lock();
		ret = list_lru_add(lru, item, nid, mem_cgroup_from_slab_obj(item));
		rcu_read_unlock();
	} else {
		ret = list_lru_add(lru, item, nid, NULL);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(list_lru_add_obj);

/* The caller must ensure the memcg lifetime. */
bool list_lru_del(struct list_lru *lru, struct list_head *item, int nid,
		  struct mem_cgroup *memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;
	l = lock_list_lru_of_memcg(lru, nid, memcg, false, false);
	if (!l)
		return false;
	if (!list_empty(item)) {
		list_del_init(item);
		l->nr_items--;
		unlock_list_lru(l, false);
		atomic_long_dec(&nlru->nr_items);
		return true;
	}
	unlock_list_lru(l, false);
	return false;
}

bool list_lru_del_obj(struct list_lru *lru, struct list_head *item)
{
	bool ret;
	int nid = page_to_nid(virt_to_page(item));

	if (list_lru_memcg_aware(lru)) {
		rcu_read_lock();
		ret = list_lru_del(lru, item, nid, mem_cgroup_from_slab_obj(item));
		rcu_read_unlock();
	} else {
		ret = list_lru_del(lru, item, nid, NULL);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(list_lru_del_obj);

void list_lru_isolate(struct list_lru_one *list, struct list_head *item)
{
	list_del_init(item);
	list->nr_items--;
}
EXPORT_SYMBOL_GPL(list_lru_isolate);

void list_lru_isolate_move(struct list_lru_one *list, struct list_head *item,
			   struct list_head *head)
{
	list_move(item, head);
	list->nr_items--;
}
EXPORT_SYMBOL_GPL(list_lru_isolate_move);

unsigned long list_lru_count_one(struct list_lru *lru,
				 int nid, struct mem_cgroup *memcg)
{
	struct list_lru_one *l;
	long count;

	rcu_read_lock();
	l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
	count = l ? READ_ONCE(l->nr_items) : 0;
	rcu_read_unlock();

	if (unlikely(count < 0))
		count = 0;

	return count;
}
EXPORT_SYMBOL_GPL(list_lru_count_one);

unsigned long list_lru_count_node(struct list_lru *lru, int nid)
{
	struct list_lru_node *nlru;

	nlru = &lru->node[nid];
	return atomic_long_read(&nlru->nr_items);
}
EXPORT_SYMBOL_GPL(list_lru_count_node);

static unsigned long
__list_lru_walk_one(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		    list_lru_walk_cb isolate, void *cb_arg,
		    unsigned long *nr_to_walk, bool irq_off)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l = NULL;
	struct list_head *item, *n;
	unsigned long isolated = 0;

restart:
	l = lock_list_lru_of_memcg(lru, nid, memcg, irq_off, true);
	if (!l)
		return isolated;
	list_for_each_safe(item, n, &l->list) {
		enum lru_status ret;

		/*
		 * decrement nr_to_walk first so that we don't livelock if we
		 * get stuck on large numbers of LRU_RETRY items
		 */
		if (!*nr_to_walk)
			break;
		--*nr_to_walk;

		ret = isolate(item, l, cb_arg);
		switch (ret) {
		/*
		 * LRU_RETRY, LRU_REMOVED_RETRY and LRU_STOP will drop the lru
		 * lock. List traversal will have to restart from scratch.
		 */
		case LRU_RETRY:
			goto restart;
		case LRU_REMOVED_RETRY:
			fallthrough;
		case LRU_REMOVED:
			isolated++;
			atomic_long_dec(&nlru->nr_items);
			if (ret == LRU_REMOVED_RETRY)
				goto restart;
			break;
		case LRU_ROTATE:
			list_move_tail(item, &l->list);
			break;
		case LRU_SKIP:
			break;
		case LRU_STOP:
			goto out;
		default:
			BUG();
		}
	}
	unlock_list_lru(l, irq_off);
out:
	return isolated;
}

unsigned long
list_lru_walk_one(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		  list_lru_walk_cb isolate, void *cb_arg,
		  unsigned long *nr_to_walk)
{
	return __list_lru_walk_one(lru, nid, memcg, isolate,
				   cb_arg, nr_to_walk, false);
}
EXPORT_SYMBOL_GPL(list_lru_walk_one);

unsigned long
list_lru_walk_one_irq(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		      list_lru_walk_cb isolate, void *cb_arg,
		      unsigned long *nr_to_walk)
{
	return __list_lru_walk_one(lru, nid, memcg, isolate,
				   cb_arg, nr_to_walk, true);
}

unsigned long list_lru_walk_node(struct list_lru *lru, int nid,
				 list_lru_walk_cb isolate, void *cb_arg,
				 unsigned long *nr_to_walk)
{
	long isolated = 0;

	isolated += list_lru_walk_one(lru, nid, NULL, isolate, cb_arg,
				      nr_to_walk);

#ifdef CONFIG_MEMCG
	if (*nr_to_walk > 0 && list_lru_memcg_aware(lru)) {
		struct list_lru_memcg *mlru;
		struct mem_cgroup *memcg;
		unsigned long index;

		xa_for_each(&lru->xa, index, mlru) {
			rcu_read_lock();
			memcg = mem_cgroup_from_id(index);
			if (!mem_cgroup_tryget(memcg)) {
				rcu_read_unlock();
				continue;
			}
			rcu_read_unlock();
			isolated += __list_lru_walk_one(lru, nid, memcg,
							isolate, cb_arg,
							nr_to_walk, false);
			mem_cgroup_put(memcg);

			if (*nr_to_walk <= 0)
				break;
		}
	}
#endif

	return isolated;
}
EXPORT_SYMBOL_GPL(list_lru_walk_node);

static void init_one_lru(struct list_lru *lru, struct list_lru_one *l)
{
	INIT_LIST_HEAD(&l->list);
	spin_lock_init(&l->lock);
	l->nr_items = 0;
#ifdef CONFIG_LOCKDEP
	if (lru->key)
		lockdep_set_class(&l->lock, lru->key);
#endif
}

#ifdef CONFIG_MEMCG
static struct list_lru_memcg *memcg_init_list_lru_one(struct list_lru *lru, gfp_t gfp)
{
	int nid;
	struct list_lru_memcg *mlru;

	mlru = kmalloc(struct_size(mlru, node, nr_node_ids), gfp);
	if (!mlru)
		return NULL;

	for_each_node(nid)
		init_one_lru(lru, &mlru->node[nid]);

	return mlru;
}

static inline void memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
	if (memcg_aware)
		xa_init_flags(&lru->xa, XA_FLAGS_LOCK_IRQ);
	lru->memcg_aware = memcg_aware;
}

static void memcg_destroy_list_lru(struct list_lru *lru)
{
	XA_STATE(xas, &lru->xa, 0);
	struct list_lru_memcg *mlru;

	if (!list_lru_memcg_aware(lru))
		return;

	xas_lock_irq(&xas);
	xas_for_each(&xas, mlru, ULONG_MAX) {
		kfree(mlru);
		xas_store(&xas, NULL);
	}
	xas_unlock_irq(&xas);
}

static void memcg_reparent_list_lru_one(struct list_lru *lru, int nid,
					struct list_lru_one *src,
					struct mem_cgroup *dst_memcg)
{
	int dst_idx = dst_memcg->kmemcg_id;
	struct list_lru_one *dst;

	spin_lock_irq(&src->lock);
	dst = list_lru_from_memcg_idx(lru, nid, dst_idx);
	spin_lock_nested(&dst->lock, SINGLE_DEPTH_NESTING);

	list_splice_init(&src->list, &dst->list);
	if (src->nr_items) {
		WARN_ON(src->nr_items < 0);
		dst->nr_items += src->nr_items;
		set_shrinker_bit(dst_memcg, nid, lru_shrinker_id(lru));
	}
	/* Mark the list_lru_one dead */
	src->nr_items = LONG_MIN;

	spin_unlock(&dst->lock);
	spin_unlock_irq(&src->lock);
}

void memcg_reparent_list_lrus(struct mem_cgroup *memcg, struct mem_cgroup *parent)
{
	struct list_lru *lru;
	int i;

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &memcg_list_lrus, list) {
		struct list_lru_memcg *mlru;
		XA_STATE(xas, &lru->xa, memcg->kmemcg_id);

		/*
		 * Lock the Xarray to ensure no on going list_lru_memcg
		 * allocation and further allocation will see css_is_dying().
		 */
		xas_lock_irq(&xas);
		mlru = xas_store(&xas, NULL);
		xas_unlock_irq(&xas);
		if (!mlru)
			continue;

		/*
		 * With Xarray value set to NULL, holding the lru lock below
		 * prevents list_lru_{add,del,isolate} from touching the lru,
		 * safe to reparent.
		 */
		for_each_node(i)
			memcg_reparent_list_lru_one(lru, i, &mlru->node[i], parent);

		/*
		 * Here all list_lrus corresponding to the cgroup are guaranteed
		 * to remain empty, we can safely free this lru, any further
		 * memcg_list_lru_alloc() call will simply bail out.
		 */
		kvfree_rcu(mlru, rcu);
	}
	mutex_unlock(&list_lrus_mutex);
}

static inline bool memcg_list_lru_allocated(struct mem_cgroup *memcg,
					    struct list_lru *lru)
{
	int idx = memcg->kmemcg_id;

	return idx < 0 || xa_load(&lru->xa, idx);
}

int memcg_list_lru_alloc(struct mem_cgroup *memcg, struct list_lru *lru,
			 gfp_t gfp)
{
	unsigned long flags;
	struct list_lru_memcg *mlru = NULL;
	struct mem_cgroup *pos, *parent;
	XA_STATE(xas, &lru->xa, 0);

	if (!list_lru_memcg_aware(lru) || memcg_list_lru_allocated(memcg, lru))
		return 0;

	gfp &= GFP_RECLAIM_MASK;
	/*
	 * Because the list_lru can be reparented to the parent cgroup's
	 * list_lru, we should make sure that this cgroup and all its
	 * ancestors have allocated list_lru_memcg.
	 */
	do {
		/*
		 * Keep finding the farest parent that wasn't populated
		 * until found memcg itself.
		 */
		pos = memcg;
		parent = parent_mem_cgroup(pos);
		while (!memcg_list_lru_allocated(parent, lru)) {
			pos = parent;
			parent = parent_mem_cgroup(pos);
		}

		if (!mlru) {
			mlru = memcg_init_list_lru_one(lru, gfp);
			if (!mlru)
				return -ENOMEM;
		}
		xas_set(&xas, pos->kmemcg_id);
		do {
			xas_lock_irqsave(&xas, flags);
			if (!xas_load(&xas) && !css_is_dying(&pos->css)) {
				xas_store(&xas, mlru);
				if (!xas_error(&xas))
					mlru = NULL;
			}
			xas_unlock_irqrestore(&xas, flags);
		} while (xas_nomem(&xas, gfp));
	} while (pos != memcg && !css_is_dying(&pos->css));

	if (unlikely(mlru))
		kfree(mlru);

	return xas_error(&xas);
}
#else
static inline void memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
}

static void memcg_destroy_list_lru(struct list_lru *lru)
{
}
#endif /* CONFIG_MEMCG */

int __list_lru_init(struct list_lru *lru, bool memcg_aware, struct shrinker *shrinker)
{
	int i;

#ifdef CONFIG_MEMCG
	if (shrinker)
		lru->shrinker_id = shrinker->id;
	else
		lru->shrinker_id = -1;

	if (mem_cgroup_kmem_disabled())
		memcg_aware = false;
#endif

	lru->node = kcalloc(nr_node_ids, sizeof(*lru->node), GFP_KERNEL);
	if (!lru->node)
		return -ENOMEM;

	for_each_node(i)
		init_one_lru(lru, &lru->node[i].lru);

	memcg_init_list_lru(lru, memcg_aware);
	list_lru_register(lru);

	return 0;
}
EXPORT_SYMBOL_GPL(__list_lru_init);

void list_lru_destroy(struct list_lru *lru)
{
	/* Already destroyed or not yet initialized? */
	if (!lru->node)
		return;

	list_lru_unregister(lru);

	memcg_destroy_list_lru(lru);
	kfree(lru->node);
	lru->node = NULL;

#ifdef CONFIG_MEMCG
	lru->shrinker_id = -1;
#endif
}
EXPORT_SYMBOL_GPL(list_lru_destroy);
