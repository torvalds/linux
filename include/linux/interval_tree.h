/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_INTERVAL_TREE_H
#define _LINUX_INTERVAL_TREE_H

#include <linux/rbtree.h>

struct interval_tree_node {
	struct rb_node rb;
	unsigned long start;	/* Start of interval */
	unsigned long last;	/* Last location _in_ interval */
	unsigned long __subtree_last;
};

extern void
interval_tree_insert(struct interval_tree_node *node,
		     struct rb_root_cached *root);

extern void
interval_tree_remove(struct interval_tree_node *node,
		     struct rb_root_cached *root);

extern struct interval_tree_node *
interval_tree_iter_first(struct rb_root_cached *root,
			 unsigned long start, unsigned long last);

extern struct interval_tree_node *
interval_tree_iter_next(struct interval_tree_node *node,
			unsigned long start, unsigned long last);

/**
 * struct interval_tree_span_iter - Find used and unused spans.
 * @start_hole: Start of an interval for a hole when is_hole == 1
 * @last_hole: Inclusive end of an interval for a hole when is_hole == 1
 * @start_used: Start of a used interval when is_hole == 0
 * @last_used: Inclusive end of a used interval when is_hole == 0
 * @is_hole: 0 == used, 1 == is_hole, -1 == done iteration
 *
 * This iterator travels over spans in an interval tree. It does not return
 * nodes but classifies each span as either a hole, where no nodes intersect, or
 * a used, which is fully covered by nodes. Each iteration step toggles between
 * hole and used until the entire range is covered. The returned spans always
 * fully cover the requested range.
 *
 * The iterator is greedy, it always returns the largest hole or used possible,
 * consolidating all consecutive nodes.
 *
 * Use interval_tree_span_iter_done() to detect end of iteration.
 */
struct interval_tree_span_iter {
	/* private: not for use by the caller */
	struct interval_tree_node *nodes[2];
	unsigned long first_index;
	unsigned long last_index;

	/* public: */
	union {
		unsigned long start_hole;
		unsigned long start_used;
	};
	union {
		unsigned long last_hole;
		unsigned long last_used;
	};
	int is_hole;
};

void interval_tree_span_iter_first(struct interval_tree_span_iter *state,
				   struct rb_root_cached *itree,
				   unsigned long first_index,
				   unsigned long last_index);
void interval_tree_span_iter_advance(struct interval_tree_span_iter *iter,
				     struct rb_root_cached *itree,
				     unsigned long new_index);
void interval_tree_span_iter_next(struct interval_tree_span_iter *state);

static inline bool
interval_tree_span_iter_done(struct interval_tree_span_iter *state)
{
	return state->is_hole == -1;
}

#define interval_tree_for_each_span(span, itree, first_index, last_index)      \
	for (interval_tree_span_iter_first(span, itree,                        \
					   first_index, last_index);           \
	     !interval_tree_span_iter_done(span);                              \
	     interval_tree_span_iter_next(span))

#endif	/* _LINUX_INTERVAL_TREE_H */
