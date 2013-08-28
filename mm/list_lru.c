/*
 * Copyright (c) 2013 Red Hat, Inc. and Parallels Inc. All rights reserved.
 * Authors: David Chinner and Glauber Costa
 *
 * Generic LRU infrastructure
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list_lru.h>

bool list_lru_add(struct list_lru *lru, struct list_head *item)
{
	spin_lock(&lru->lock);
	if (list_empty(item)) {
		list_add_tail(item, &lru->list);
		lru->nr_items++;
		spin_unlock(&lru->lock);
		return true;
	}
	spin_unlock(&lru->lock);
	return false;
}
EXPORT_SYMBOL_GPL(list_lru_add);

bool list_lru_del(struct list_lru *lru, struct list_head *item)
{
	spin_lock(&lru->lock);
	if (!list_empty(item)) {
		list_del_init(item);
		lru->nr_items--;
		spin_unlock(&lru->lock);
		return true;
	}
	spin_unlock(&lru->lock);
	return false;
}
EXPORT_SYMBOL_GPL(list_lru_del);

unsigned long list_lru_walk(struct list_lru *lru, list_lru_walk_cb isolate,
			    void *cb_arg, unsigned long nr_to_walk)
{
	struct list_head *item, *n;
	unsigned long removed = 0;
	/*
	 * If we don't keep state of at which pass we are, we can loop at
	 * LRU_RETRY, since we have no guarantees that the caller will be able
	 * to do something other than retry on the next pass. We handle this by
	 * allowing at most one retry per object. This should not be altered
	 * by any condition other than LRU_RETRY.
	 */
	bool first_pass = true;

	spin_lock(&lru->lock);
restart:
	list_for_each_safe(item, n, &lru->list) {
		enum lru_status ret;
		ret = isolate(item, &lru->lock, cb_arg);
		switch (ret) {
		case LRU_REMOVED:
			lru->nr_items--;
			removed++;
			break;
		case LRU_ROTATE:
			list_move_tail(item, &lru->list);
			break;
		case LRU_SKIP:
			break;
		case LRU_RETRY:
			if (!first_pass) {
				first_pass = true;
				break;
			}
			first_pass = false;
			goto restart;
		default:
			BUG();
		}

		if (nr_to_walk-- == 0)
			break;

	}
	spin_unlock(&lru->lock);
	return removed;
}
EXPORT_SYMBOL_GPL(list_lru_walk);

unsigned long list_lru_dispose_all(struct list_lru *lru,
				   list_lru_dispose_cb dispose)
{
	unsigned long disposed = 0;
	LIST_HEAD(dispose_list);

	spin_lock(&lru->lock);
	while (!list_empty(&lru->list)) {
		list_splice_init(&lru->list, &dispose_list);
		disposed += lru->nr_items;
		lru->nr_items = 0;
		spin_unlock(&lru->lock);

		dispose(&dispose_list);

		spin_lock(&lru->lock);
	}
	spin_unlock(&lru->lock);
	return disposed;
}

int list_lru_init(struct list_lru *lru)
{
	spin_lock_init(&lru->lock);
	INIT_LIST_HEAD(&lru->list);
	lru->nr_items = 0;

	return 0;
}
EXPORT_SYMBOL_GPL(list_lru_init);
