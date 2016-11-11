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
#else
static void list_lru_register(struct list_lru *lru)
{
}

static void list_lru_unregister(struct list_lru *lru)
{
}
#endif /* CONFIG_MEMCG_KMEM */

#ifdef CONFIG_MEMCG_KMEM
static inline bool list_lru_memcg_aware(struct list_lru *lru)
{
	/*
	 * This needs node 0 to be always present, even
	 * in the systems supporting sparse numa ids.
	 */
	return !!lru->node[0].memcg_lrus;
}

static inline struct list_lru_one *
list_lru_from_memcg_idx(struct list_lru_node *nlru, int idx)
{
	/*
	 * The lock protects the array of per cgroup lists from relocation
	 * (see memcg_update_list_lru_node).
	 */
	lockdep_assert_held(&nlru->lock);
	if (nlru->memcg_lrus && idx >= 0)
		return nlru->memcg_lrus->lru[idx];

	return &nlru->lru;
}

static __always_inline struct mem_cgroup *mem_cgroup_from_kmem(void *ptr)
{
	struct page *page;

	if (!memcg_kmem_enabled())
		return NULL;
	page = virt_to_head_page(ptr);
	return page->mem_cgroup;
}

static inline struct list_lru_one *
list_lru_from_kmem(struct list_lru_node *nlru, void *ptr)
{
	struct mem_cgroup *memcg;

	if (!nlru->memcg_lrus)
		return &nlru->lru;

	memcg = mem_cgroup_from_kmem(ptr);
	if (!memcg)
		return &nlru->lru;

	return list_lru_from_memcg_idx(nlru, memcg_cache_id(memcg));
}
#else
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
list_lru_from_kmem(struct list_lru_node *nlru, void *ptr)
{
	return &nlru->lru;
}
#endif /* CONFIG_MEMCG_KMEM */

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;

	spin_lock(&nlru->lock);
	if (list_empty(item)) {
		l = list_lru_from_kmem(nlru, item);
		list_add_tail(item, &l->list);
		l->nr_items++;
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
		l = list_lru_from_kmem(nlru, item);
		list_del_init(item);
		l->nr_items--;
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

static unsigned long __list_lru_count_one(struct list_lru *lru,
					  int nid, int memcg_idx)
{
	struct list_lru_node *nlru = &lru->node[nid];
	struct list_lru_one *l;
	unsigned long count;

	spin_lock(&nlru->lock);
	l = list_lru_from_memcg_idx(nlru, memcg_idx);
	count = l->nr_items;
	spin_unlock(&nlru->lock);

	return count;
}

unsigned long list_lru_count_one(struct list_lru *lru,
				 int nid, struct mem_cgroup *memcg)
{
	return __list_lru_count_one(lru, nid, memcg_cache_id(memcg));
}
EXPORT_SYMBOL_GPL(list_lru_count_one);

unsigned long list_lru_count_node(struct list_lru *lru, int nid)
{
	long count = 0;
	int memcg_idx;

	count += __list_lru_count_one(lru, nid, -1);
	if (list_lru_memcg_aware(lru)) {
		for_each_memcg_cache_index(memcg_idx)
			count += __list_lru_count_one(lru, nid, memcg_idx);
	}
	return count;
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

	spin_lock(&nlru->lock);
	l = list_lru_from_memcg_idx(nlru, memcg_idx);
restart:
	list_for_each_safe(item, n, &l->list) {
		enum lru_status ret;

		/*
		 * decrement nr_to_walk first so that we don't livelock if we
		 * get stuck on large numbesr of LRU_RETRY items
		 */
		if (!*nr_to_walk)
			break;
		--*nr_to_walk;

		ret = isolate(item, l, &nlru->lock, cb_arg);
		switch (ret) {
		case LRU_REMOVED_RETRY:
			assert_spin_locked(&nlru->lock);
		case LRU_REMOVED:
			isolated++;
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

	spin_unlock(&nlru->lock);
	return isolated;
}

unsigned long
list_lru_walk_one(struct list_lru *lru, int nid, struct mem_cgroup *memcg,
		  list_lru_walk_cb isolate, void *cb_arg,
		  unsigned long *nr_to_walk)
{
	return __list_lru_walk_one(lru, nid, memcg_cache_id(memcg),
				   isolate, cb_arg, nr_to_walk);
}
EXPORT_SYMBOL_GPL(list_lru_walk_one);

unsigned long list_lru_walk_node(struct list_lru *lru, int nid,
				 list_lru_walk_cb isolate, void *cb_arg,
				 unsigned long *nr_to_walk)
{
	long isolated = 0;
	int memcg_idx;

	isolated += __list_lru_walk_one(lru, nid, -1, isolate, cb_arg,
					nr_to_walk);
	if (*nr_to_walk > 0 && list_lru_memcg_aware(lru)) {
		for_each_memcg_cache_index(memcg_idx) {
			isolated += __list_lru_walk_one(lru, nid, memcg_idx,
						isolate, cb_arg, nr_to_walk);
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
	__memcg_destroy_list_lru_node(memcg_lrus, begin, i - 1);
	return -ENOMEM;
}

static int memcg_init_list_lru_node(struct list_lru_node *nlru)
{
	int size = memcg_nr_cache_ids;

	nlru->memcg_lrus = kmalloc(size * sizeof(void *), GFP_KERNEL);
	if (!nlru->memcg_lrus)
		return -ENOMEM;

	if (__memcg_init_list_lru_node(nlru->memcg_lrus, 0, size)) {
		kfree(nlru->memcg_lrus);
		return -ENOMEM;
	}

	return 0;
}

static void memcg_destroy_list_lru_node(struct list_lru_node *nlru)
{
	__memcg_destroy_list_lru_node(nlru->memcg_lrus, 0, memcg_nr_cache_ids);
	kfree(nlru->memcg_lrus);
}

static int memcg_update_list_lru_node(struct list_lru_node *nlru,
				      int old_size, int new_size)
{
	struct list_lru_memcg *old, *new;

	BUG_ON(old_size > new_size);

	old = nlru->memcg_lrus;
	new = kmalloc(new_size * sizeof(void *), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	if (__memcg_init_list_lru_node(new, old_size, new_size)) {
		kfree(new);
		return -ENOMEM;
	}

	memcpy(new, old, old_size * sizeof(void *));

	/*
	 * The lock guarantees that we won't race with a reader
	 * (see list_lru_from_memcg_idx).
	 *
	 * Since list_lru_{add,del} may be called under an IRQ-safe lock,
	 * we have to use IRQ-safe primitives here to avoid deadlock.
	 */
	spin_lock_irq(&nlru->lock);
	nlru->memcg_lrus = new;
	spin_unlock_irq(&nlru->lock);

	kfree(old);
	return 0;
}

static void memcg_cancel_update_list_lru_node(struct list_lru_node *nlru,
					      int old_size, int new_size)
{
	/* do not bother shrinking the array back to the old size, because we
	 * cannot handle allocation failures here */
	__memcg_destroy_list_lru_node(nlru->memcg_lrus, old_size, new_size);
}

static int memcg_init_list_lru(struct list_lru *lru, bool memcg_aware)
{
	int i;

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

static void memcg_drain_list_lru_node(struct list_lru_node *nlru,
				      int src_idx, int dst_idx)
{
	struct list_lru_one *src, *dst;

	/*
	 * Since list_lru_{add,del} may be called under an IRQ-safe lock,
	 * we have to use IRQ-safe primitives here to avoid deadlock.
	 */
	spin_lock_irq(&nlru->lock);

	src = list_lru_from_memcg_idx(nlru, src_idx);
	dst = list_lru_from_memcg_idx(nlru, dst_idx);

	list_splice_init(&src->list, &dst->list);
	dst->nr_items += src->nr_items;
	src->nr_items = 0;

	spin_unlock_irq(&nlru->lock);
}

static void memcg_drain_list_lru(struct list_lru *lru,
				 int src_idx, int dst_idx)
{
	int i;

	if (!list_lru_memcg_aware(lru))
		return;

	for_each_node(i)
		memcg_drain_list_lru_node(&lru->node[i], src_idx, dst_idx);
}

void memcg_drain_all_list_lrus(int src_idx, int dst_idx)
{
	struct list_lru *lru;

	mutex_lock(&list_lrus_mutex);
	list_for_each_entry(lru, &list_lrus, list)
		memcg_drain_list_lru(lru, src_idx, dst_idx);
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
		    struct lock_class_key *key)
{
	int i;
	size_t size = sizeof(*lru->node) * nr_node_ids;
	int err = -ENOMEM;

	memcg_get_cache_ids();

	lru->node = kzalloc(size, GFP_KERNEL);
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

	memcg_put_cache_ids();
}
EXPORT_SYMBOL_GPL(list_lru_destroy);
