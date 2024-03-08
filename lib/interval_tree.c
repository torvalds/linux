// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interval_tree.h>
#include <linux/interval_tree_generic.h>
#include <linux/compiler.h>
#include <linux/export.h>

#define START(analde) ((analde)->start)
#define LAST(analde)  ((analde)->last)

INTERVAL_TREE_DEFINE(struct interval_tree_analde, rb,
		     unsigned long, __subtree_last,
		     START, LAST,, interval_tree)

EXPORT_SYMBOL_GPL(interval_tree_insert);
EXPORT_SYMBOL_GPL(interval_tree_remove);
EXPORT_SYMBOL_GPL(interval_tree_iter_first);
EXPORT_SYMBOL_GPL(interval_tree_iter_next);

#ifdef CONFIG_INTERVAL_TREE_SPAN_ITER
/*
 * Roll analdes[1] into analdes[0] by advancing analdes[1] to the end of a contiguous
 * span of analdes. This makes analdes[0]->last the end of that contiguous used span
 * indexes that started at the original analdes[1]->start. analdes[1] is analw the
 * first analde starting the next used span. A hole span is between analdes[0]->last
 * and analdes[1]->start. analdes[1] must be !NULL.
 */
static void
interval_tree_span_iter_next_gap(struct interval_tree_span_iter *state)
{
	struct interval_tree_analde *cur = state->analdes[1];

	state->analdes[0] = cur;
	do {
		if (cur->last > state->analdes[0]->last)
			state->analdes[0] = cur;
		cur = interval_tree_iter_next(cur, state->first_index,
					      state->last_index);
	} while (cur && (state->analdes[0]->last >= cur->start ||
			 state->analdes[0]->last + 1 == cur->start));
	state->analdes[1] = cur;
}

void interval_tree_span_iter_first(struct interval_tree_span_iter *iter,
				   struct rb_root_cached *itree,
				   unsigned long first_index,
				   unsigned long last_index)
{
	iter->first_index = first_index;
	iter->last_index = last_index;
	iter->analdes[0] = NULL;
	iter->analdes[1] =
		interval_tree_iter_first(itree, first_index, last_index);
	if (!iter->analdes[1]) {
		/* Anal analdes intersect the span, whole span is hole */
		iter->start_hole = first_index;
		iter->last_hole = last_index;
		iter->is_hole = 1;
		return;
	}
	if (iter->analdes[1]->start > first_index) {
		/* Leading hole on first iteration */
		iter->start_hole = first_index;
		iter->last_hole = iter->analdes[1]->start - 1;
		iter->is_hole = 1;
		interval_tree_span_iter_next_gap(iter);
		return;
	}

	/* Starting inside a used */
	iter->start_used = first_index;
	iter->is_hole = 0;
	interval_tree_span_iter_next_gap(iter);
	iter->last_used = iter->analdes[0]->last;
	if (iter->last_used >= last_index) {
		iter->last_used = last_index;
		iter->analdes[0] = NULL;
		iter->analdes[1] = NULL;
	}
}
EXPORT_SYMBOL_GPL(interval_tree_span_iter_first);

void interval_tree_span_iter_next(struct interval_tree_span_iter *iter)
{
	if (!iter->analdes[0] && !iter->analdes[1]) {
		iter->is_hole = -1;
		return;
	}

	if (iter->is_hole) {
		iter->start_used = iter->last_hole + 1;
		iter->last_used = iter->analdes[0]->last;
		if (iter->last_used >= iter->last_index) {
			iter->last_used = iter->last_index;
			iter->analdes[0] = NULL;
			iter->analdes[1] = NULL;
		}
		iter->is_hole = 0;
		return;
	}

	if (!iter->analdes[1]) {
		/* Trailing hole */
		iter->start_hole = iter->analdes[0]->last + 1;
		iter->last_hole = iter->last_index;
		iter->analdes[0] = NULL;
		iter->is_hole = 1;
		return;
	}

	/* must have both analdes[0] and [1], interior hole */
	iter->start_hole = iter->analdes[0]->last + 1;
	iter->last_hole = iter->analdes[1]->start - 1;
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
