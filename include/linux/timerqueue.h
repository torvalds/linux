/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIMERQUEUE_H
#define _LINUX_TIMERQUEUE_H

#include <linux/rbtree.h>
#include <linux/timerqueue_types.h>

bool timerqueue_add(struct timerqueue_head *head, struct timerqueue_node *node);
bool timerqueue_del(struct timerqueue_head *head, struct timerqueue_node *node);
struct timerqueue_node *timerqueue_iterate_next(struct timerqueue_node *node);

bool timerqueue_linked_add(struct timerqueue_linked_head *head, struct timerqueue_linked_node *node);

/**
 * timerqueue_getnext - Returns the timer with the earliest expiration time
 *
 * @head: head of timerqueue
 *
 * Returns a pointer to the timer node that has the earliest expiration time.
 */
static inline struct timerqueue_node *timerqueue_getnext(struct timerqueue_head *head)
{
	struct rb_node *leftmost = rb_first_cached(&head->rb_root);

	return rb_entry_safe(leftmost, struct timerqueue_node, node);
}

static inline void timerqueue_init(struct timerqueue_node *node)
{
	RB_CLEAR_NODE(&node->node);
}

static inline bool timerqueue_node_queued(struct timerqueue_node *node)
{
	return !RB_EMPTY_NODE(&node->node);
}

static inline void timerqueue_init_head(struct timerqueue_head *head)
{
	head->rb_root = RB_ROOT_CACHED;
}

/* Timer queues with linked nodes */

static __always_inline
struct timerqueue_linked_node *timerqueue_linked_first(struct timerqueue_linked_head *head)
{
	return rb_entry_safe(head->rb_root.rb_leftmost, struct timerqueue_linked_node, node);
}

static __always_inline
struct timerqueue_linked_node *timerqueue_linked_next(struct timerqueue_linked_node *node)
{
	return rb_entry_safe(node->node.next, struct timerqueue_linked_node, node);
}

static __always_inline
struct timerqueue_linked_node *timerqueue_linked_prev(struct timerqueue_linked_node *node)
{
	return rb_entry_safe(node->node.prev, struct timerqueue_linked_node, node);
}

static __always_inline
bool timerqueue_linked_del(struct timerqueue_linked_head *head, struct timerqueue_linked_node *node)
{
	return rb_erase_linked(&node->node, &head->rb_root);
}

static __always_inline void timerqueue_linked_init(struct timerqueue_linked_node *node)
{
	RB_CLEAR_LINKED_NODE(&node->node);
}

static __always_inline bool timerqueue_linked_node_queued(struct timerqueue_linked_node *node)
{
	return !RB_EMPTY_LINKED_NODE(&node->node);
}

static __always_inline void timerqueue_linked_init_head(struct timerqueue_linked_head *head)
{
	head->rb_root = RB_ROOT_LINKED;
}

#endif /* _LINUX_TIMERQUEUE_H */
