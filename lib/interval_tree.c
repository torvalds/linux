// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interval_tree.h>
#include <linux/interval_tree_generic.h>
#include <linux/compiler.h>
#include <linux/export.h>

#define START(node) ((node)->start)
#define LAST(node)  ((node)->last)

INTERVAL_TREE_DEFINE(struct interval_tree_node, rb,
		     unsigned long, __subtree_last,
		     START, LAST,, interval_tree)

EXPORT_SYMBOL_GPL(interval_tree_insert);
EXPORT_SYMBOL_GPL(interval_tree_remove);
EXPORT_SYMBOL_GPL(interval_tree_iter_first);
EXPORT_SYMBOL_GPL(interval_tree_iter_next);

#ifdef CONFIG_INTERVAL_TREE_SPAN_ITER
/*
 * Roll nodes[1] into nodes[0] by advancing nodes[1] to the end of a contiguous
 * span of nodes. This makes nodes[0]->last the end of that contiguous used span
 * of indexes that started at the original nodes[1]->start.
 *
 * If there is an interior hole, nodes[1] is now the first node starting the
 * next used span. A hole span is between nodes[0]->last and nodes[1]->start.
 *
 * If there is a tailing hole, nodes[1] is now NULL. A hole span is between
 * nodes[0]->last and last_index.
 *
 * If the contiguous used range span to last_index, nodes[1] is set to NULL.
 */
static void
interval_tree_span_iter_next_gap(struct interval_tree_span_iter *state)
{
	struct interval_tree_node *cur = state->nodes[1];

	state->nodes[0] = cur;
	do {
		if (cur->last > state->nodes[0]->last)
			state->nodes[0] = cur;
		cur = interval_tree_iter_next(cur, state->first_index,
					      state->last_index);
	} while (cur && (state->nodes[0]->last >= cur->start ||
			 state->nodes[0]->last + 1 == cur->start));
	state->nodes[1] = cur;
}

void interval_tree_span_iter_first(struct interval_tree_span_iter *iter,
				   struct rb_root_cached *itree,
				   unsigned long first_index,
				   unsigned long last_index)
{
	iter->first_index = first_index;
	iter->last_index = last_index;
	iter->nodes[0] = NULL;
	iter->nodes[1] =
		interval_tree_iter_first(itree, first_index, last_index);
	if (!iter->nodes[1]) {
		/* No nodes intersect the span, whole span is hole */
		iter->start_hole = first_index;
		iter->last_hole = last_index;
		iter->is_hole = 1;
		return;
	}
	if (iter->nodes[1]->start > first_index) {
		/* Leading hole on first iteration */
		iter->start_hole = first_index;
		iter->last_hole = iter->nodes[1]->start - 1;
		iter->is_hole = 1;
		interval_tree_span_iter_next_gap(iter);
		return;
	}

	/* Starting inside a used */
	iter->start_used = first_index;
	iter->is_hole = 0;
	interval_tree_span_iter_next_gap(iter);
	iter->last_used = iter->nodes[0]->last;
	if (iter->last_used >= last_index) {
		iter->last_used = last_index;
		iter->nodes[0] = NULL;
		iter->nodes[1] = NULL;
	}
}
EXPORT_SYMBOL_GPL(interval_tree_span_iter_first);

void interval_tree_span_iter_next(struct interval_tree_span_iter *iter)
{
	if (!iter->nodes[0] && !iter->nodes[1]) {
		iter->is_hole = -1;
		return;
	}

	if (iter->is_hole) {
		iter->start_used = iter->last_hole + 1;
		iter->last_used = iter->nodes[0]->last;
		if (iter->last_used >= iter->last_index) {
			iter->last_used = iter->last_index;
			iter->nodes[0] = NULL;
			iter->nodes[1] = NULL;
		}
		iter->is_hole = 0;
		return;
	}

	if (!iter->nodes[1]) {
		/* Trailing hole */
		iter->start_hole = iter->nodes[0]->last + 1;
		iter->last_hole = iter->last_index;
		iter->nodes[0] = NULL;
		iter->is_hole = 1;
		return;
	}

	/* must have both nodes[0] and [1], interior hole */
	iter->start_hole = iter->nodes[0]->last + 1;
	iter->last_hole = iter->nodes[1]->start - 1;
	iter->is_hole = 1;
	interval_tree_span_iter_next_gap(iter);
}
EXPORT_SYMBOL_GPL(interval_tree_span_iter_next);

/*
 * Advance the iterator index to a specific position. The returned used/hole is
 * updated to start at new_index. This is faster than calling
 * interval_tree_span_iter_first() as it can avoid full searches in several
 * cases where the iterator is already set.
 */
void interval_tree_span_iter_advance(struct interval_tree_span_iter *iter,
				     struct rb_root_cached *itree,
				     unsigned long new_index)
{
	if (iter->is_hole == -1)
		return;

	iter->first_index = new_index;
	if (new_index > iter->last_index) {
		iter->is_hole = -1;
		return;
	}

	/* Rely on the union aliasing hole/used */
	if (iter->start_hole <= new_index && new_index <= iter->last_hole) {
		iter->start_hole = new_index;
		return;
	}
	if (new_index == iter->last_hole + 1)
		interval_tree_span_iter_next(iter);
	else
		interval_tree_span_iter_first(iter, itree, new_index,
					      iter->last_index);
}
EXPORT_SYMBOL_GPL(interval_tree_span_iter_advance);
#endif
