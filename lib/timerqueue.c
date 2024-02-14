// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Generic Timer-queue
 *
 *  Manages a simple queue of timers, ordered by expiration time.
 *  Uses rbtrees for quick list adds and expiration.
 *
 *  NOTE: All of the following functions need to be serialized
 *  to avoid races. No locking is done by this library code.
 */

#include <linux/bug.h>
#include <linux/timerqueue.h>
#include <linux/rbtree.h>
#include <linux/export.h>

#define __node_2_tq(_n) \
	rb_entry((_n), struct timerqueue_node, node)

static inline bool __timerqueue_less(struct rb_node *a, const struct rb_node *b)
{
	return __node_2_tq(a)->expires < __node_2_tq(b)->expires;
}

/**
 * timerqueue_add - Adds timer to timerqueue.
 *
 * @head: head of timerqueue
 * @node: timer node to be added
 *
 * Adds the timer node to the timerqueue, sorted by the node's expires
 * value. Returns true if the newly added timer is the first expiring timer in
 * the queue.
 */
bool timerqueue_add(struct timerqueue_head *head, struct timerqueue_node *node)
{
	/* Make sure we don't add nodes that are already added */
	WARN_ON_ONCE(!RB_EMPTY_NODE(&node->node));

	return rb_add_cached(&node->node, &head->rb_root, __timerqueue_less);
}
EXPORT_SYMBOL_GPL(timerqueue_add);

/**
 * timerqueue_del - Removes a timer from the timerqueue.
 *
 * @head: head of timerqueue
 * @node: timer node to be removed
 *
 * Removes the timer node from the timerqueue. Returns true if the queue is
 * not empty after the remove.
 */
bool timerqueue_del(struct timerqueue_head *head, struct timerqueue_node *node)
{
	WARN_ON_ONCE(RB_EMPTY_NODE(&node->node));

	rb_erase_cached(&node->node, &head->rb_root);
	RB_CLEAR_NODE(&node->node);

	return !RB_EMPTY_ROOT(&head->rb_root.rb_root);
}
EXPORT_SYMBOL_GPL(timerqueue_del);

/**
 * timerqueue_iterate_next - Returns the timer after the provided timer
 *
 * @node: Pointer to a timer.
 *
 * Provides the timer that is after the given node. This is used, when
 * necessary, to iterate through the list of timers in a timer list
 * without modifying the list.
 */
struct timerqueue_node *timerqueue_iterate_next(struct timerqueue_node *node)
{
	struct rb_node *next;

	if (!node)
		return NULL;
	next = rb_next(&node->node);
	if (!next)
		return NULL;
	return container_of(next, struct timerqueue_node, node);
}
EXPORT_SYMBOL_GPL(timerqueue_iterate_next);
