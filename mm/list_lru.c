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

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	int nid = page_to_nid(virt_to_page(item));
	struct list_lru_node *nlru = &lru->node[nid];

	spin_lock(&nlru->lock);
	WARN_ON_ONCE(nlru->nr_items < 0);
	if (list_empty(item)) {
		list_add_tail(item, &nlru->list);
		if (nlru->nr_items++ == 0)
			node_set(nid, lru->active_nodes);
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
		if (--nlru->nr_items == 0)
			node_clear(nid, lru->active_nodes);
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
		if (--(*nr_to_walk) == 0)
			break;

		ret = isolate(item, &nlru->lock, cb_arg);
		switch (ret) {
		case LRU_REMOVED:
			if (--nlru->nr_items == 0)
				node_clear(nid, lru->active_nodes);
			WARN_ON_ONCE(nlru->nr_items < 0);
			isolated++;
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
			goto restart;
		default:
			BUG();
		}
	}

	spin_unlock(&nlru->lock);
	return isolated;
}
EXPORT_SYMBOL_GPL(list_lru_walk_node);

static unsigned long list_lru_dispose_all_node(struct list_lru *lru, int nid,
					       list_lru_dispose_cb dispose)
{
	struct list_lru_node	*nlru = &lru->node[nid];
	LIST_HEAD(dispose_list);
	unsigned long disposed = 0;

	spin_lock(&nlru->lock);
	while (!list_empty(&nlru->list)) {
		list_splice_init(&nlru->list, &dispose_list);
		disposed += nlru->nr_items;
		nlru->nr_items = 0;
		node_clear(nid, lru->active_nodes);
		spin_unlock(&nlru->lock);

		dispose(&dispose_list);

		spin_lock(&nlru->lock);
	}
	spin_unlock(&nlru->lock);
	return disposed;
}

unsigned long list_lru_dispose_all(struct list_lru *lru,
				   list_lru_dispose_cb dispose)
{
	unsigned long disposed;
	unsigned long total = 0;
	int nid;

	do {
		disposed = 0;
		for_each_node_mask(nid, lru->active_nodes) {
			disposed += list_lru_dispose_all_node(lru, nid,
							      dispose);
		}
		total += disposed;
	} while (disposed != 0);

	return total;
}

int list_lru_init(struct list_lru *lru)
{
	int i;

	nodes_clear(lru->active_nodes);
	for (i = 0; i < MAX_NUMNODES; i++) {
		spin_lock_init(&lru->node[i].lock);
		INIT_LIST_HEAD(&lru->node[i].list);
		lru->node[i].nr_items = 0;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(list_lru_init);
