/*
 * Copyright (c) 2013 Red Hat, Inc. and Parallels Inc. All rights reserved.
 * Authors: David Chinner and Glauber Costa
 *
 * Generic LRU infrastructure
 */
#ifndef _LRU_LIST_H
#define _LRU_LIST_H

#include <linux/list.h>
#include <linux/nodemask.h>

/* list_lru_walk_cb has to always return one of those */
enum lru_status {
	LRU_REMOVED,		/* item removed from list */
	LRU_ROTATE,		/* item referenced, give another pass */
	LRU_SKIP,		/* item cannot be locked, skip */
	LRU_RETRY,		/* item not freeable. May drop the lock
				   internally, but has to return locked. */
};

struct list_lru_node {
	spinlock_t		lock;
	struct list_head	list;
	/* kept as signed so we can catch imbalance bugs */
	long			nr_items;
} ____cacheline_aligned_in_smp;

struct list_lru {
	/*
	 * Because we use a fixed-size array, this struct can be very big if
	 * MAX_NUMNODES is big. If this becomes a problem this is fixable by
	 * turning this into a pointer and dynamically allocating this to
	 * nr_node_ids. This quantity is firwmare-provided, and still would
	 * provide room for all nodes at the cost of a pointer lookup and an
	 * extra allocation. Because that allocation will most likely come from
	 * a different slab cache than the main structure holding this
	 * structure, we may very well fail.
	 */
	struct list_lru_node	node[MAX_NUMNODES];
	nodemask_t		active_nodes;
};

int list_lru_init(struct list_lru *lru);

/**
 * list_lru_add: add an element to the lru list's tail
 * @list_lru: the lru pointer
 * @item: the item to be added.
 *
 * If the element is already part of a list, this function returns doing
 * nothing. Therefore the caller does not need to keep state about whether or
 * not the element already belongs in the list and is allowed to lazy update
 * it. Note however that this is valid for *a* list, not *this* list. If
 * the caller organize itself in a way that elements can be in more than
 * one type of list, it is up to the caller to fully remove the item from
 * the previous list (with list_lru_del() for instance) before moving it
 * to @list_lru
 *
 * Return value: true if the list was updated, false otherwise
 */
bool list_lru_add(struct list_lru *lru, struct list_head *item);

/**
 * list_lru_del: delete an element to the lru list
 * @list_lru: the lru pointer
 * @item: the item to be deleted.
 *
 * This function works analogously as list_lru_add in terms of list
 * manipulation. The comments about an element already pertaining to
 * a list are also valid for list_lru_del.
 *
 * Return value: true if the list was updated, false otherwise
 */
bool list_lru_del(struct list_lru *lru, struct list_head *item);

/**
 * list_lru_count_node: return the number of objects currently held by @lru
 * @lru: the lru pointer.
 * @nid: the node id to count from.
 *
 * Always return a non-negative number, 0 for empty lists. There is no
 * guarantee that the list is not updated while the count is being computed.
 * Callers that want such a guarantee need to provide an outer lock.
 */
unsigned long list_lru_count_node(struct list_lru *lru, int nid);
static inline unsigned long list_lru_count(struct list_lru *lru)
{
	long count = 0;
	int nid;

	for_each_node_mask(nid, lru->active_nodes)
		count += list_lru_count_node(lru, nid);

	return count;
}

typedef enum lru_status
(*list_lru_walk_cb)(struct list_head *item, spinlock_t *lock, void *cb_arg);
/**
 * list_lru_walk_node: walk a list_lru, isolating and disposing freeable items.
 * @lru: the lru pointer.
 * @nid: the node id to scan from.
 * @isolate: callback function that is resposible for deciding what to do with
 *  the item currently being scanned
 * @cb_arg: opaque type that will be passed to @isolate
 * @nr_to_walk: how many items to scan.
 *
 * This function will scan all elements in a particular list_lru, calling the
 * @isolate callback for each of those items, along with the current list
 * spinlock and a caller-provided opaque. The @isolate callback can choose to
 * drop the lock internally, but *must* return with the lock held. The callback
 * will return an enum lru_status telling the list_lru infrastructure what to
 * do with the object being scanned.
 *
 * Please note that nr_to_walk does not mean how many objects will be freed,
 * just how many objects will be scanned.
 *
 * Return value: the number of objects effectively removed from the LRU.
 */
unsigned long list_lru_walk_node(struct list_lru *lru, int nid,
				 list_lru_walk_cb isolate, void *cb_arg,
				 unsigned long *nr_to_walk);

static inline unsigned long
list_lru_walk(struct list_lru *lru, list_lru_walk_cb isolate,
	      void *cb_arg, unsigned long nr_to_walk)
{
	long isolated = 0;
	int nid;

	for_each_node_mask(nid, lru->active_nodes) {
		isolated += list_lru_walk_node(lru, nid, isolate,
					       cb_arg, &nr_to_walk);
		if (nr_to_walk <= 0)
			break;
	}
	return isolated;
}

typedef void (*list_lru_dispose_cb)(struct list_head *dispose_list);
/**
 * list_lru_dispose_all: forceably flush all elements in an @lru
 * @lru: the lru pointer
 * @dispose: callback function to be called for each lru list.
 *
 * This function will forceably isolate all elements into the dispose list, and
 * call the @dispose callback to flush the list. Please note that the callback
 * should expect items in any state, clean or dirty, and be able to flush all of
 * them.
 *
 * Return value: how many objects were freed. It should be equal to all objects
 * in the list_lru.
 */
unsigned long
list_lru_dispose_all(struct list_lru *lru, list_lru_dispose_cb dispose);
#endif /* _LRU_LIST_H */
