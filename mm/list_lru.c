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
	struct list_lru_memcg *mlrus;
	struct list_lru_node *nlru = &lru->node[nid];

	/*
	 * Either lock or RCU protects the array of per cgroup lists
	 * from relocation (see memcg_update_list_lru).
	 */
	mlrus = rcu_dereference_check(lru->mlrus, lockdep_is_held(&nlru->lock));
	if (mlrus && idx >= 0)
		return &mlrus->mlru[idx]->node[nid];
	return &nlru->lru;
}

static inline struct list_lru_one *
list_lru_from_kmem(struct list_lru *lru, int nid, void *ptr,
		   struct mem_cgroup **memcg_ptr)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l = &nlru->lru;
	struct mem_cgroup *memcg = NULL;

	if (!lru->mlrus)
		goto out;

	memcg = mem_cgroup_from_obj(ptr);
	if (!memcg)
		goto out;

	l = list_lru_from_memcg_idx(lru, nid, memcg_cache_id(memcg));
out:
	if (memcg_ptr)
		*memcg_ptr = memcg;
	return l;
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
list_lru_from_kmem(struct list_lru *lru, int nid, void *ptr,
		   struct mem_cgroup **memcg_ptr)
{
	if (memcg_ptr)
		*memcg_ptr = NULL;
	return &lru->node[nid].lru;
}
#endif /* CONFIG_MEMCG_KMEM */

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];
	struct mem_cgroup *memcg;
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (list_empty(item)) {
		l = list_lru_from_kmem(lru, nid, item, &memcg);
		list_add_tail(item, &l->list);
		/* Set shrinker bit if the first element was added */
		if (!l->nr_items++)
			set_shrinker_bit(memcg, nid,
					 lru_shrinker_id(lru));
		nlru->nr_items++;
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
	return false;
}
EXPORT_SYMBOL_GPL(list_lru_add);

bool list_lru_del(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (!list_empty(item)) {
		l = list_lru_from_kmem(lru, nid, item, NULL);
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
	l = list_lru_from_memcg_idx(lru, nid, memcg_cache_id(memcg));
	count = READ_ONCE(l->nr_items);
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

	l = list_lru_from_memcg_idx(lru, nid, memcg_idx);
restart:
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
		default:
			BUG();
		}
	}
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
	ret = __list_lru_walk_one(lru, nid, memcg_cache_id(memcg), isolate,
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
	ret = __list_lru_walk_one(lru, nid, memcg_cache_id(memcg), isolate,
				  cb_arg, nr_to_walk);
	spin_unlock_irq(&nlru->lock);
	return ret;
}

unsigned long list_lru_walk_node(struct list_lru *lru, int nid,
				 list_lru_walk_cb isolate, void *cb_arg,
				 unsigned long *nr_to_walk)
{
	long isolated = 0;
	int memcg_idx;

	isolated += list_lru_walk_one(lru, nid, NULL, isolate, cb_arg,
				      nr_to_walk);
	if (*nr_to_walk > 0 && list_lru_memcg_aware(lru)) {
		for_each_memcg_cache_index(memcg_idx) {
			struct list_lru_node *nlru = &lru->node[nid];

			spin_lock(&nlru->lock);
			isolated += __list_lru_walk_one(lru, nid, memcg_idx,
							isolate, cb_arg,
							nr_to_walk);
			spin_unlock(&nlru->lock);

			if (*nr_to_walk <= 0)
				break;
		}
	}
	return isolated;
}
EXPORT_SYMBOL_GPL(list_lru_walk_node);

static void init_one_lru(struct list_lru_one *l)
{
	INIT_LIST_HEAD(&l->list);
	l->nr_items = 0;
}

#ifdef CONFIG_MEMCG_KMEM
static void memcg_destroy_list_lru_range(struct list_lru_memcg *mlrus,
					 int begin, int end)
{
	int i;

	for (i = begin; i < end; i++)
		kfree(mlrus->mlru[i]);
}

static struct list_lru_per_memcg *memcg_init_list_lru_one(gfp_t gfp)
{
	int nid;
	struct list_lru_per_memcg *mlru;

	mlru = kmalloc(struct_size(mlru, node, nr_node_ids), gfp);
	if (!mlru)
		return NULL;

	for_each_node(nid)
		init_one_lru(&mlru->node[nid]);

	return mlru;
}

static int memcg_init_list_lru_range(struct list_lru_memcg *mlrus,
				     int begin, int end)
{
	int i;

	for (i = begin; i < end; i++) {
		mlrus->mlru[i] = memcg_init_list_lru_one(GFP_KERNEL);
		if (!mlrus->mlru[i])
			goto fail;
	}
	return 0;
fail:
	memcg_destroy_list_lru_range(mlrus, begin, i);
	return -ENOMEM;
}

static int memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
	struct list_lru_memcg *mlrus;
	int size = memcg_nr_cache_ids;

	lru->memcg_aware = memcg_aware;
	if (!memcg_aware)
		return 0;

	spin_lock_init(&lru->lock);

	mlrus = kvmalloc(struct_size(mlrus, mlru, size), GFP_KERNEL);
	if (!mlrus)
		return -ENOMEM;

	if (memcg_init_list_lru_range(mlrus, 0, size)) {
		kvfree(mlrus);
		return -ENOMEM;
	}
	RCU_INIT_POINTER(lru->mlrus, mlrus);

	return 0;
}

static void memcg_destroy_list_lru(struct list_lru *lru)
{
	struct list_lru_memcg *mlrus;

	if (!list_lru_memcg_aware(lru))
		return;

	/*
	 * This is called when shrinker has already been unregistered,
	 * and nobody can use it. So, there is no need to use kvfree_rcu().
	 */
	mlrus = rcu_dereference_protected(lru->mlrus, true);
	memcg_destroy_list_lru_range(mlrus, 0, memcg_nr_cache_ids);
	kvfree(mlrus);
}

static int memcg_update_list_lru(struct list_lru *lru, int old_size, int new_size)
{
	struct list_lru_memcg *old, *new;

	BUG_ON(old_size > new_size);

	old = rcu_dereference_protected(lru->mlrus,
					lockdep_is_held(&list_lrus_mutex));
	new = kvmalloc(struct_size(new, mlru, new_size), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	if (memcg_init_list_lru_range(new, old_size, new_size)) {
		kvfree(new);
		return -ENOMEM;
	}

	spin_lock_irq(&lru->lock);
	memcpy(&new->mlru, &old->mlru, flex_array_size(new, mlru, old_size));
	rcu_assign_pointer(lru->mlrus, new);
	spin_unlock_irq(&lru->lock);

	kvfree_rcu(old, rcu);
	return 0;
}

static void memcg_cancel_update_list_lru(struct list_lru *lru,
					 int old_size, int new_size)
{
	struct list_lru_memcg *mlrus;

	mlrus = rcu_dereference_protected(lru->mlrus,
					  lockdep_is_held(&list_lrus_mutex));
	/*
	 * Do not bother shrinking the array back to the old size, because we
	 * cannot handle allocation failures here.
	 */
	memcg_destroy_list_lru_range(mlrus, old_size, new_size);
}

int memcg_update_all_list_lrus(int new_size)
{
	int ret = 0;
	struct list_lru *lru;
	int old_size = memcg_nr_cache_ids;

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &memcg_list_lrus, list) {
		ret = memcg_update_list_lru(lru, old_size, new_size);
		if (ret)
			goto fail;
	}
out:
	mutex_unlock(&list_lrus_mutex);
	return ret;
fail:
	list_for_each_entry_continue_reverse(lru, &memcg_list_lrus, list)
		memcg_cancel_update_list_lru(lru, old_size, new_size);
	goto out;
}

static void memcg_drain_list_lru_node(struct list_lru *lru, int nid,
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
	dst = list_lru_from_memcg_idx(lru, nid, dst_idx);

	list_splice_init(&src->list, &dst->list);

	if (src->nr_items) {
		dst->nr_items += src->nr_items;
		set_shrinker_bit(dst_memcg, nid, lru_shrinker_id(lru));
		src->nr_items = 0;
	}

	spin_unlock_irq(&nlru->lock);
}

static void memcg_drain_list_lru(struct list_lru *lru,
				 int src_idx, struct mem_cgroup *dst_memcg)
{
	int i;

	for_each_node(i)
		memcg_drain_list_lru_node(lru, i, src_idx, dst_memcg);
}

void memcg_drain_all_list_lrus(int src_idx, struct mem_cgroup *dst_memcg)
{
	struct list_lru *lru;

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &memcg_list_lrus, list)
		memcg_drain_list_lru(lru, src_idx, dst_memcg);
	mutex_unlock(&list_lrus_mutex);
}

static bool memcg_list_lru_allocated(struct mem_cgroup *memcg,
				     struct list_lru *lru)
{
	bool allocated;
	int idx;

	idx = memcg->kmemcg_id;
	if (unlikely(idx < 0))
		return true;

	rcu_read_lock();
	allocated = !!rcu_dereference(lru->mlrus)->mlru[idx];
	rcu_read_unlock();

	return allocated;
}

int memcg_list_lru_alloc(struct mem_cgroup *memcg, struct list_lru *lru,
			 gfp_t gfp)
{
	int i;
	unsigned long flags;
	struct list_lru_memcg *mlrus;
	struct list_lru_memcg_table {
		struct list_lru_per_memcg *mlru;
		struct mem_cgroup *memcg;
	} *table;

	if (!list_lru_memcg_aware(lru) || memcg_list_lru_allocated(memcg, lru))
		return 0;

	gfp &= GFP_RECLAIM_MASK;
	table = kmalloc_array(memcg->css.cgroup->level, sizeof(*table), gfp);
	if (!table)
		return -ENOMEM;

	/*
	 * Because the list_lru can be reparented to the parent cgroup's
	 * list_lru, we should make sure that this cgroup and all its
	 * ancestors have allocated list_lru_per_memcg.
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

	spin_lock_irqsave(&lru->lock, flags);
	mlrus = rcu_dereference_protected(lru->mlrus, true);
	while (i--) {
		int index = table[i].memcg->kmemcg_id;

		if (mlrus->mlru[index])
			kfree(table[i].mlru);
		else
			mlrus->mlru[index] = table[i].mlru;
	}
	spin_unlock_irqrestore(&lru->lock, flags);

	kfree(table);

	return 0;
}
#else
static int memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
	return 0;
}

static void memcg_destroy_list_lru(struct list_lru *lru)
{
}
#endif /* CONFIG_MEMCG_KMEM */

int __list_lru_init(struct list_lru *lru, bool memcg_aware,
		    struct lock_class_key *key, struct shrinker *shrinker)
{
	int i;
	int err = -ENOMEM;

#ifdef CONFIG_MEMCG_KMEM
	if (shrinker)
		lru->shrinker_id = shrinker->id;
	else
		lru->shrinker_id = -1;
#endif
	memcg_get_cache_ids();

	lru->node = kcalloc(nr_node_ids, sizeof(*lru->node), GFP_KERNEL);
	if (!lru->node)
		goto out;

	for_each_node(i) {
		spin_lock_init(&lru->node[i].lock);
		if (key)
			lockdep_set_class(&lru->node[i].lock, key);
		init_one_lru(&lru->node[i].lru);
	}

	err = memcg_init_list_lru(lru, memcg_aware);
	if (err) {
		kfree(lru->node);
		/* Do this so a list_lru_destroy() doesn't crash: */
		lru->node = NULL;
		goto out;
	}

	list_lru_register(lru);
out:
	memcg_put_cache_ids();
	return err;
}
EXPORT_SYMBOL_GPL(__list_lru_init);

void list_lru_destroy(struct list_lru *lru)
{
	/* Already destroyed or not yet initialized? */
	if (!lru->node)
		return;

	memcg_get_cache_ids();

	list_lru_unregister(lru);

	memcg_destroy_list_lru(lru);
	kfree(lru->node);
	lru->node = NULL;

#ifdef CONFIG_MEMCG_KMEM
	lru->shrinker_id = -1;
#endif
	memcg_put_cache_ids();
}
EXPORT_SYMBOL_GPL(list_lru_destroy);
