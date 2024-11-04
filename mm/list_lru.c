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

static inline struct list_lru_one *
list_lru_from_memcg(struct list_lru *lru, int nid, struct mem_cgroup *memcg)
{
	struct list_lru_one *l;
again:
	l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
	if (likely(l))
		return l;

	memcg = parent_mem_cgroup(memcg);
	VM_WARN_ON(!css_is_dying(&memcg->css));
	goto again;
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
list_lru_from_memcg(struct list_lru *lru, int nid, int idx)
{
	return &lru->node[nid].lru;
}
#endif /* CONFIG_MEMCG */

/* The caller must ensure the memcg lifetime. */
bool list_lru_add(struct list_lru *lru, struct list_head *item, int nid,
		    struct mem_cgroup *memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (list_empty(item)) {
		l = list_lru_from_memcg(lru, nid, memcg);
		list_add_tail(item, &l->list);
		/* Set shrinker bit if the first element was added */
		if (!l->nr_items++)
			set_shrinker_bit(memcg, nid, lru_shrinker_id(lru));
		nlru->nr_items++;
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
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

	spin_lock(&nlru->lock);
	if (!list_empty(item)) {
		l = list_lru_from_memcg(lru, nid, memcg);
		list_del_init(item);
		l->nr_items--;
		nlru->nr_items--;
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
	return false;
}
EXPORT_SYMBOL_GPL(list_lru_del);

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
	return nlru->nr_items;
}
EXPORT_SYMBOL_GPL(list_lru_count_node);

static unsigned long
__list_lru_walk_one(struct list_lru *lru, int nid, int memcg_idx,
		    list_lru_walk_cb isolate, void *cb_arg,
		    unsigned long *nr_to_walk)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;
	struct list_head *item, *n;
	unsigned long isolated = 0;

restart:
	l = list_lru_from_memcg_idx(lru, nid, memcg_idx);
	if (!l)
		goto out;

	list_for_each_safe(item, n, &l->list) {
		enum lru_status ret;

		/*
		 * decrement nr_to_walk first so that we don't livelock if we
		 * get stuck on large numbers of LRU_RETRY items
		 */
		if (!*nr_to_walk)
			break;
		--*nr_to_walk;

		ret = isolate(item, l, &nlru->lock, cb_arg);
		switch (ret) {
		case LRU_REMOVED_RETRY:
			assert_spin_locked(&nlru->lock);
			fallthrough;
		case LRU_REMOVED:
			isolated++;
			nlru->nr_items--;
			/*
			 * If the lru lock has been dropped, our list
			 * traversal is now invalid and so we have to
			 * restart from scratch.
			 */
			if (ret == LRU_REMOVED_RETRY)
				goto restart;
			break;
		case LRU_ROTATE:
			list_move_tail(item, &l->list);
			break;
		case LRU_SKIP:
			break;
		case LRU_RETRY:
			/*
			 * The lru lock has been dropped, our list traversal is
			 * now invalid and so we have to restart from scratch.
			 */
			assert_spin_locked(&nlru->lock);
			goto restart;
		case LRU_STOP:
			assert_spin_locked(&nlru->lock);
			goto out;
		default:
			BUG();
		}
	}
out:
	return isolated;
}

unsigned long
list_lru_walk_one(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		  list_lru_walk_cb isolate, void *cb_arg,
		  unsigned long *nr_to_walk)
{
	struct list_lru_node *nlru = &lru->node[nid];
	unsigned long ret;

	spin_lock(&nlru->lock);
	ret = __list_lru_walk_one(lru, nid, memcg_kmem_id(memcg), isolate,
				  cb_arg, nr_to_walk);
	spin_unlock(&nlru->lock);
	return ret;
}
EXPORT_SYMBOL_GPL(list_lru_walk_one);

unsigned long
list_lru_walk_one_irq(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		      list_lru_walk_cb isolate, void *cb_arg,
		      unsigned long *nr_to_walk)
{
	struct list_lru_node *nlru = &lru->node[nid];
	unsigned long ret;

	spin_lock_irq(&nlru->lock);
	ret = __list_lru_walk_one(lru, nid, memcg_kmem_id(memcg), isolate,
				  cb_arg, nr_to_walk);
	spin_unlock_irq(&nlru->lock);
	return ret;
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
		unsigned long index;

		xa_for_each(&lru->xa, index, mlru) {
			struct list_lru_node *nlru = &lru->node[nid];

			spin_lock(&nlru->lock);
			isolated += __list_lru_walk_one(lru, nid, index,
							isolate, cb_arg,
							nr_to_walk);
			spin_unlock(&nlru->lock);

			if (*nr_to_walk <= 0)
				break;
		}
	}
#endif

	return isolated;
}
EXPORT_SYMBOL_GPL(list_lru_walk_node);

static void init_one_lru(struct list_lru_one *l)
{
	INIT_LIST_HEAD(&l->list);
	l->nr_items = 0;
}

#ifdef CONFIG_MEMCG
static struct list_lru_memcg *memcg_init_list_lru_one(gfp_t gfp)
{
	int nid;
	struct list_lru_memcg *mlru;

	mlru = kmalloc(struct_size(mlru, node, nr_node_ids), gfp);
	if (!mlru)
		return NULL;

	for_each_node(nid)
		init_one_lru(&mlru->node[nid]);

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

static void memcg_reparent_list_lru_node(struct list_lru *lru, int nid,
					 struct list_lru_one *src,
					 struct mem_cgroup *dst_memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *dst;

	/*
	 * Since list_lru_{add,del} may be called under an IRQ-safe lock,
	 * we have to use IRQ-safe primitives here to avoid deadlock.
	 */
	spin_lock_irq(&nlru->lock);
	dst = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(dst_memcg));

	list_splice_init(&src->list, &dst->list);

	if (src->nr_items) {
		dst->nr_items += src->nr_items;
		set_shrinker_bit(dst_memcg, nid, lru_shrinker_id(lru));
		src->nr_items = 0;
	}
	spin_unlock_irq(&nlru->lock);
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
			memcg_reparent_list_lru_node(lru, i, &mlru->node[i], parent);

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
	struct list_lru_memcg *mlru;
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

		mlru = memcg_init_list_lru_one(gfp);
		if (!mlru)
			return -ENOMEM;
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
		if (mlru)
			kfree(mlru);
	} while (pos != memcg && !css_is_dying(&pos->css));

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

	for_each_node(i) {
		spin_lock_init(&lru->node[i].lock);
#ifdef CONFIG_LOCKDEP
		if (lru->key)
			lockdep_set_class(&lru->node[i].lock, lru->key);
#endif
		init_one_lru(&lru->node[i].lru);
	}

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
