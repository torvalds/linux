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

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];

	spin_lock(&nlru->lock);
	WARN_ON_ONCE(nlru->nr_items < 0);
	if (list_empty(item)) {
		list_add_tail(item, &nlru->list);
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

	spin_lock(&nlru->lock);
	if (!list_empty(item)) {
		list_del_init(item);
		nlru->nr_items--;
		WARN_ON_ONCE(nlru->nr_items < 0);
		spin_unlock(&nlru->lock);
		return true;
	}
	spin_unlock(&nlru->lock);
	return false;
}
EXPORT_SYMBOL_GPL(list_lru_del);

unsigned long
list_lru_count_node(struct list_lru *lru, int nid)
{
	unsigned long count = 0;
	struct list_lru_node *nlru = &lru->node[nid];

	spin_lock(&nlru->lock);
	WARN_ON_ONCE(nlru->nr_items < 0);
	count += nlru->nr_items;
	spin_unlock(&nlru->lock);

	return count;
}
EXPORT_SYMBOL_GPL(list_lru_count_node);

unsigned long
list_lru_walk_node(struct list_lru *lru, int nid, list_lru_walk_cb isolate,
		   void *cb_arg, unsigned long *nr_to_walk)
{

	struct list_lru_node	*nlru = &lru->node[nid];
	struct list_head *item, *n;
	unsigned long isolated = 0;

	spin_lock(&nlru->lock);
restart:
	list_for_each_safe(item, n, &nlru->list) {
		enum lru_status ret;

		/*
		 * decrement nr_to_walk first so that we don't livelock if we
		 * get stuck on large numbesr of LRU_RETRY items
		 */
		if (!*nr_to_walk)
			break;
		--*nr_to_walk;

		ret = isolate(item, &nlru->lock, cb_arg);
		switch (ret) {
		case LRU_REMOVED_RETRY:
			assert_spin_locked(&nlru->lock);
		case LRU_REMOVED:
			nlru->nr_items--;
			WARN_ON_ONCE(nlru->nr_items < 0);
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
			list_move_tail(item, &nlru->list);
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
EXPORT_SYMBOL_GPL(list_lru_walk_node);

int list_lru_init_key(struct list_lru *lru, struct lock_class_key *key)
{
	int i;
	size_t size = sizeof(*lru->node) * nr_node_ids;

	lru->node = kzalloc(size, GFP_KERNEL);
	if (!lru->node)
		return -ENOMEM;

	for (i = 0; i < nr_node_ids; i++) {
		spin_lock_init(&lru->node[i].lock);
		if (key)
			lockdep_set_class(&lru->node[i].lock, key);
		INIT_LIST_HEAD(&lru->node[i].list);
		lru->node[i].nr_items = 0;
	}
	list_lru_register(lru);
	return 0;
}
EXPORT_SYMBOL_GPL(list_lru_init_key);

void list_lru_destroy(struct list_lru *lru)
{
	/* Already destroyed or not yet initialized? */
	if (!lru->node)
		return;
	list_lru_unregister(lru);
	kfree(lru->node);
	lru->node = NULL;
}
EXPORT_SYMBOL_GPL(list_lru_destroy);
