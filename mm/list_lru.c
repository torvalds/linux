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

#ifdef CONFIG_MEMCG_KMEM
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
#endif /* CONFIG_MEMCG_KMEM */

bool list_lru_add(struct list_lru *lru, struct list_head *item, int nid,
		    struct mem_cgroup *memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (list_empty(item)) {
		l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
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
EXPORT_SYMBOL_GPL(list_lru_add);

bool list_lru_add_obj(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct mem_cgroup *memcg = list_lru_memcg_aware(lru) ?
		mem_cgroup_from_slab_obj(item) : NULL;

	return list_lru_add(lru, item, nid, memcg);
}
EXPORT_SYMBOL_GPL(list_lru_add_obj);

bool list_lru_del(struct list_lru *lru, struct list_head *item, int nid,
		    struct mem_cgroup *memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (!list_empty(item)) {
		l = list_lru_from_memcg_idx(lru, nid, memcg_kmem_id(memcg));
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
	int nid = page_to_nid(virt_to_page(item));
	struct mem_cgroup *memcg = list_lru_memcg_aware(lru) ?
		mem_cgroup_from_slab_obj(item) : NULL;

	return list_lru_del(lru, item, nid, memcg);
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

#ifdef CONFIG_MEMCG_KMEM
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

#ifdef CONFIG_MEMCG_KMEM
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

static void memcg_list_lru_free(struct list_lru *lru, int src_idx)
{
	struct list_lru_memcg *mlru = xa_erase_irq(&lru->xa, src_idx);

	/*
	 * The __list_lru_walk_one() can walk the list of this node.
	 * We need kvfree_rcu() here. And the walking of the list
	 * is under lru->node[nid]->lock, which can serve as a RCU
	 * read-side critical section.
	 */
	if (mlru)
		kvfree_rcu(mlru, rcu);
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
					 int src_idx, struct mem_cgroup *dst_memcg)
{
	struct list_lru_node *nlru = &lru->node[nid];
	int dst_idx = dst_memcg->kmemcg_id;
	struct list_lru_one *src, *dst;

	/*
	 * Since list_lru_{add,del} may be called under an IRQ-safe lock,
	 * we have to use IRQ-safe primitives here to avoid deadlock.
	 */
	spin_lock_irq(&nlru->lock);

	src = list_lru_from_memcg_idx(lru, nid, src_idx);
	if (!src)
		goto out;
	dst = list_lru_from_memcg_idx(lru, nid, dst_idx);

	list_splice_init(&src->list, &dst->list);

	if (src->nr_items) {
		dst->nr_items += src->nr_items;
		set_shrinker_bit(dst_memcg, nid, lru_shrinker_id(lru));
		src->nr_items = 0;
	}
out:
	spin_unlock_irq(&nlru->lock);
}

static void memcg_reparent_list_lru(struct list_lru *lru,
				    int src_idx, struct mem_cgroup *dst_memcg)
{
	int i;

	for_each_node(i)
		memcg_reparent_list_lru_node(lru, i, src_idx, dst_memcg);

	memcg_list_lru_free(lru, src_idx);
}

void memcg_reparent_list_lrus(struct mem_cgroup *memcg, struct mem_cgroup *parent)
{
	struct cgroup_subsys_state *css;
	struct list_lru *lru;
	int src_idx = memcg->kmemcg_id;

	/*
	 * Change kmemcg_id of this cgroup and all its descendants to the
	 * parent's id, and then move all entries from this cgroup's list_lrus
	 * to ones of the parent.
	 *
	 * After we have finished, all list_lrus corresponding to this cgroup
	 * are guaranteed to remain empty. So we can safely free this cgroup's
	 * list lrus in memcg_list_lru_free().
	 *
	 * Changing ->kmemcg_id to the parent can prevent memcg_list_lru_alloc()
	 * from allocating list lrus for this cgroup after memcg_list_lru_free()
	 * call.
	 */
	rcu_read_lock();
	css_for_each_descendant_pre(css, &memcg->css) {
		struct mem_cgroup *child;

		child = mem_cgroup_from_css(css);
		WRITE_ONCE(child->kmemcg_id, parent->kmemcg_id);
	}
	rcu_read_unlock();

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &memcg_list_lrus, list)
		memcg_reparent_list_lru(lru, src_idx, parent);
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
	int i;
	unsigned long flags;
	struct list_lru_memcg_table {
		struct list_lru_memcg *mlru;
		struct mem_cgroup *memcg;
	} *table;
	XA_STATE(xas, &lru->xa, 0);

	if (!list_lru_memcg_aware(lru) || memcg_list_lru_allocated(memcg, lru))
		return 0;

	gfp &= GFP_RECLAIM_MASK;
	table = kmalloc_array(memcg->css.cgroup->level, sizeof(*table), gfp);
	if (!table)
		return -ENOMEM;

	/*
	 * Because the list_lru can be reparented to the parent cgroup's
	 * list_lru, we should make sure that this cgroup and all its
	 * ancestors have allocated list_lru_memcg.
	 */
	for (i = 0; memcg; memcg = parent_mem_cgroup(memcg), i++) {
		if (memcg_list_lru_allocated(memcg, lru))
			break;

		table[i].memcg = memcg;
		table[i].mlru = memcg_init_list_lru_one(gfp);
		if (!table[i].mlru) {
			while (i--)
				kfree(table[i].mlru);
			kfree(table);
			return -ENOMEM;
		}
	}

	xas_lock_irqsave(&xas, flags);
	while (i--) {
		int index = READ_ONCE(table[i].memcg->kmemcg_id);
		struct list_lru_memcg *mlru = table[i].mlru;

		xas_set(&xas, index);
retry:
		if (unlikely(index < 0 || xas_error(&xas) || xas_load(&xas))) {
			kfree(mlru);
		} else {
			xas_store(&xas, mlru);
			if (xas_error(&xas) == -ENOMEM) {
				xas_unlock_irqrestore(&xas, flags);
				if (xas_nomem(&xas, gfp))
					xas_set_err(&xas, 0);
				xas_lock_irqsave(&xas, flags);
				/*
				 * The xas lock has been released, this memcg
				 * can be reparented before us. So reload
				 * memcg id. More details see the comments
				 * in memcg_reparent_list_lrus().
				 */
				index = READ_ONCE(table[i].memcg->kmemcg_id);
				if (index < 0)
					xas_set_err(&xas, 0);
				else if (!xas_error(&xas) && index != xas.xa_index)
					xas_set(&xas, index);
				goto retry;
			}
		}
	}
	/* xas_nomem() is used to free memory instead of memory allocation. */
	if (xas.xa_alloc)
		xas_nomem(&xas, gfp);
	xas_unlock_irqrestore(&xas, flags);
	kfree(table);

	return xas_error(&xas);
}
#else
static inline void memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
}

static void memcg_destroy_list_lru(struct list_lru *lru)
{
}
#endif /* CONFIG_MEMCG_KMEM */

int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker)
{
	int i;

#ifdef CONFIG_MEMCG_KMEM
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
		if (key)
			lockdep_set_class(&lru->node[i].lock, key);
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

#ifdef CONFIG_MEMCG_KMEM
	lru->shrinker_id = -1;
#endif
}
EXPORT_SYMBOL_GPL(list_lru_destroy);
