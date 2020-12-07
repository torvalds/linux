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

#ifdef CONFIG_MEMCG_KMEM
static LIST_HEAD(list_lrus);
static DEFINE_MUTEX(list_lrus_mutex);

static void list_lru_register(struct list_lru *lru)
{
	mutex_lock(&list_lrus_mutex);
	list_add(&lru->list, &list_lrus);
	mutex_unlock(&list_lrus_mutex);
}

static void list_lru_unregister(struct list_lru *lru)
{
	mutex_lock(&list_lrus_mutex);
	list_del(&lru->list);
	mutex_unlock(&list_lrus_mutex);
}

static int lru_shrinker_id(struct list_lru *lru)
{
	return lru->shrinker_id;
}

static inline bool list_lru_memcg_aware(struct list_lru *lru)
{
	return lru->memcg_aware;
}

static inline struct list_lru_one *
list_lru_from_memcg_idx(struct list_lru_node *nlru, int idx)
{
	struct list_lru_memcg *memcg_lrus;
	/*
	 * Either lock or RCU protects the array of per cgroup lists
	 * from relocation (see memcg_update_list_lru_node).
	 */
	memcg_lrus = rcu_dereference_check(nlru->memcg_lrus,
					   lockdep_is_held(&nlru->lock));
	if (memcg_lrus && idx >= 0)
		return memcg_lrus->lru[idx];
	return &nlru->lru;
}

static inline struct list_lru_one *
list_lru_from_kmem(struct list_lru_node *nlru, void *ptr,
		   struct mem_cgroup **memcg_ptr)
{
	struct list_lru_one *l = &nlru->lru;
	struct mem_cgroup *memcg = NULL;

	if (!nlru->memcg_lrus)
		goto out;

	memcg = mem_cgroup_from_obj(ptr);
	if (!memcg)
		goto out;

	l = list_lru_from_memcg_idx(nlru, memcg_cache_id(memcg));
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
list_lru_from_memcg_idx(struct list_lru_node *nlru, int idx)
{
	return &nlru->lru;
}

static inline struct list_lru_one *
list_lru_from_kmem(struct list_lru_node *nlru, void *ptr,
		   struct mem_cgroup **memcg_ptr)
{
	if (memcg_ptr)
		*memcg_ptr = NULL;
	return &nlru->lru;
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
		l = list_lru_from_kmem(nlru, item, &memcg);
		list_add_tail(item, &l->list);
		/* Set shrinker bit if the first element was added */
		if (!l->nr_items++)
			memcg_set_shrinker_bit(memcg, nid,
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
		l = list_lru_from_kmem(nlru, item, NULL);
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
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;
	unsigned long count;

	rcu_read_lock();
	l = list_lru_from_memcg_idx(nlru, memcg_cache_id(memcg));
	count = READ_ONCE(l->nr_items);
	rcu_read_unlock();

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
__list_lru_walk_one(struct list_lru_node *nlru, int memcg_idx,
		    list_lru_walk_cb isolate, void *cb_arg,
		    unsigned long *nr_to_walk)
{

	struct list_lru_one *l;
	struct list_head *item, *n;
	unsigned long isolated = 0;

	l = list_lru_from_memcg_idx(nlru, memcg_idx);
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
	ret = __list_lru_walk_one(nlru, memcg_cache_id(memcg), isolate, cb_arg,
				  nr_to_walk);
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
	ret = __list_lru_walk_one(nlru, memcg_cache_id(memcg), isolate, cb_arg,
				  nr_to_walk);
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
			isolated += __list_lru_walk_one(nlru, memcg_idx,
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
static void __memcg_destroy_list_lru_node(struct list_lru_memcg *memcg_lrus,
					  int begin, int end)
{
	int i;

	for (i = begin; i < end; i++)
		kfree(memcg_lrus->lru[i]);
}

static int __memcg_init_list_lru_node(struct list_lru_memcg *memcg_lrus,
				      int begin, int end)
{
	int i;

	for (i = begin; i < end; i++) {
		struct list_lru_one *l;

		l = kmalloc(sizeof(struct list_lru_one), GFP_KERNEL);
		if (!l)
			goto fail;

		init_one_lru(l);
		memcg_lrus->lru[i] = l;
	}
	return 0;
fail:
	__memcg_destroy_list_lru_node(memcg_lrus, begin, i);
	return -ENOMEM;
}

static int memcg_init_list_lru_node(struct list_lru_node *nlru)
{
	struct list_lru_memcg *memcg_lrus;
	int size = memcg_nr_cache_ids;

	memcg_lrus = kvmalloc(sizeof(*memcg_lrus) +
			      size * sizeof(void *), GFP_KERNEL);
	if (!memcg_lrus)
		return -ENOMEM;

	if (__memcg_init_list_lru_node(memcg_lrus, 0, size)) {
		kvfree(memcg_lrus);
		return -ENOMEM;
	}
	RCU_INIT_POINTER(nlru->memcg_lrus, memcg_lrus);

	return 0;
}

static void memcg_destroy_list_lru_node(struct list_lru_node *nlru)
{
	struct list_lru_memcg *memcg_lrus;
	/*
	 * This is called when shrinker has already been unregistered,
	 * and nobody can use it. So, there is no need to use kvfree_rcu_local().
	 */
	memcg_lrus = rcu_dereference_protected(nlru->memcg_lrus, true);
	__memcg_destroy_list_lru_node(memcg_lrus, 0, memcg_nr_cache_ids);
	kvfree(memcg_lrus);
}

static void kvfree_rcu_local(struct rcu_head *head)
{
	struct list_lru_memcg *mlru;

	mlru = container_of(head, struct list_lru_memcg, rcu);
	kvfree(mlru);
}

static int memcg_update_list_lru_node(struct list_lru_node *nlru,
				      int old_size, int new_size)
{
	struct list_lru_memcg *old, *new;

	BUG_ON(old_size > new_size);

	old = rcu_dereference_protected(nlru->memcg_lrus,
					lockdep_is_held(&list_lrus_mutex));
	new = kvmalloc(sizeof(*new) + new_size * sizeof(void *), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	if (__memcg_init_list_lru_node(new, old_size, new_size)) {
		kvfree(new);
		return -ENOMEM;
	}

	memcpy(&new->lru, &old->lru, old_size * sizeof(void *));

	/*
	 * The locking below allows readers that hold nlru->lock avoid taking
	 * rcu_read_lock (see list_lru_from_memcg_idx).
	 *
	 * Since list_lru_{add,del} may be called under an IRQ-safe lock,
	 * we have to use IRQ-safe primitives here to avoid deadlock.
	 */
	spin_lock_irq(&nlru->lock);
	rcu_assign_pointer(nlru->memcg_lrus, new);
	spin_unlock_irq(&nlru->lock);

	call_rcu(&old->rcu, kvfree_rcu_local);
	return 0;
}

static void memcg_cancel_update_list_lru_node(struct list_lru_node *nlru,
					      int old_size, int new_size)
{
	struct list_lru_memcg *memcg_lrus;

	memcg_lrus = rcu_dereference_protected(nlru->memcg_lrus,
					       lockdep_is_held(&list_lrus_mutex));
	/* do not bother shrinking the array back to the old size, because we
	 * cannot handle allocation failures here */
	__memcg_destroy_list_lru_node(memcg_lrus, old_size, new_size);
}

static int memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
	int i;

	lru->memcg_aware = memcg_aware;

	if (!memcg_aware)
		return 0;

	for_each_node(i) {
		if (memcg_init_list_lru_node(&lru->node[i]))
			goto fail;
	}
	return 0;
fail:
	for (i = i - 1; i >= 0; i--) {
		if (!lru->node[i].memcg_lrus)
			continue;
		memcg_destroy_list_lru_node(&lru->node[i]);
	}
	return -ENOMEM;
}

static void memcg_destroy_list_lru(struct list_lru *lru)
{
	int i;

	if (!list_lru_memcg_aware(lru))
		return;

	for_each_node(i)
		memcg_destroy_list_lru_node(&lru->node[i]);
}

static int memcg_update_list_lru(struct list_lru *lru,
				 int old_size, int new_size)
{
	int i;

	if (!list_lru_memcg_aware(lru))
		return 0;

	for_each_node(i) {
		if (memcg_update_list_lru_node(&lru->node[i],
					       old_size, new_size))
			goto fail;
	}
	return 0;
fail:
	for (i = i - 1; i >= 0; i--) {
		if (!lru->node[i].memcg_lrus)
			continue;

		memcg_cancel_update_list_lru_node(&lru->node[i],
						  old_size, new_size);
	}
	return -ENOMEM;
}

static void memcg_cancel_update_list_lru(struct list_lru *lru,
					 int old_size, int new_size)
{
	int i;

	if (!list_lru_memcg_aware(lru))
		return;

	for_each_node(i)
		memcg_cancel_update_list_lru_node(&lru->node[i],
						  old_size, new_size);
}

int memcg_update_all_list_lrus(int new_size)
{
	int ret = 0;
	struct list_lru *lru;
	int old_size = memcg_nr_cache_ids;

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &list_lrus, list) {
		ret = memcg_update_list_lru(lru, old_size, new_size);
		if (ret)
			goto fail;
	}
out:
	mutex_unlock(&list_lrus_mutex);
	return ret;
fail:
	list_for_each_entry_continue_reverse(lru, &list_lrus, list)
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

	src = list_lru_from_memcg_idx(nlru, src_idx);
	dst = list_lru_from_memcg_idx(nlru, dst_idx);

	list_splice_init(&src->list, &dst->list);

	if (src->nr_items) {
		dst->nr_items += src->nr_items;
		memcg_set_shrinker_bit(dst_memcg, nid, lru_shrinker_id(lru));
		src->nr_items = 0;
	}

	spin_unlock_irq(&nlru->lock);
}

static void memcg_drain_list_lru(struct list_lru *lru,
				 int src_idx, struct mem_cgroup *dst_memcg)
{
	int i;

	if (!list_lru_memcg_aware(lru))
		return;

	for_each_node(i)
		memcg_drain_list_lru_node(lru, i, src_idx, dst_memcg);
}

void memcg_drain_all_list_lrus(int src_idx, struct mem_cgroup *dst_memcg)
{
	struct list_lru *lru;

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &list_lrus, list)
		memcg_drain_list_lru(lru, src_idx, dst_memcg);
	mutex_unlock(&list_lrus_mutex);
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
