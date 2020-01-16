/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Latched RB-trees
 *
 * Copyright (C) 2015 Intel Corp., Peter Zijlstra <peterz@infradead.org>
 *
 * Since RB-trees have yesn-atomic modifications they're yest immediately suited
 * for RCU/lockless queries. Even though we made RB-tree lookups yesn-fatal for
 * lockless lookups; we canyest guarantee they return a correct result.
 *
 * The simplest solution is a seqlock + RB-tree, this will allow lockless
 * lookups; but has the constraint (inherent to the seqlock) that read sides
 * canyest nest in write sides.
 *
 * If we need to allow unconditional lookups (say as required for NMI context
 * usage) we need a more complex setup; this data structure provides this by
 * employing the latch technique -- see @raw_write_seqcount_latch -- to
 * implement a latched RB-tree which does allow for unconditional lookups by
 * virtue of always having (at least) one stable copy of the tree.
 *
 * However, while we have the guarantee that there is at all times one stable
 * copy, this does yest guarantee an iteration will yest observe modifications.
 * What might have been a stable copy at the start of the iteration, need yest
 * remain so for the duration of the iteration.
 *
 * Therefore, this does require a lockless RB-tree iteration to be yesn-fatal;
 * see the comment in lib/rbtree.c. Note however that we only require the first
 * condition -- yest seeing partial stores -- because the latch thing isolates
 * us from loops. If we were to interrupt a modification the lookup would be
 * pointed at the stable tree and complete while the modification was halted.
 */

#ifndef RB_TREE_LATCH_H
#define RB_TREE_LATCH_H

#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>

struct latch_tree_yesde {
	struct rb_yesde yesde[2];
};

struct latch_tree_root {
	seqcount_t	seq;
	struct rb_root	tree[2];
};

/**
 * latch_tree_ops - operators to define the tree order
 * @less: used for insertion; provides the (partial) order between two elements.
 * @comp: used for lookups; provides the order between the search key and an element.
 *
 * The operators are related like:
 *
 *	comp(a->key,b) < 0  := less(a,b)
 *	comp(a->key,b) > 0  := less(b,a)
 *	comp(a->key,b) == 0 := !less(a,b) && !less(b,a)
 *
 * If these operators define a partial order on the elements we make yes
 * guarantee on which of the elements matching the key is found. See
 * latch_tree_find().
 */
struct latch_tree_ops {
	bool (*less)(struct latch_tree_yesde *a, struct latch_tree_yesde *b);
	int  (*comp)(void *key,                 struct latch_tree_yesde *b);
};

static __always_inline struct latch_tree_yesde *
__lt_from_rb(struct rb_yesde *yesde, int idx)
{
	return container_of(yesde, struct latch_tree_yesde, yesde[idx]);
}

static __always_inline void
__lt_insert(struct latch_tree_yesde *ltn, struct latch_tree_root *ltr, int idx,
	    bool (*less)(struct latch_tree_yesde *a, struct latch_tree_yesde *b))
{
	struct rb_root *root = &ltr->tree[idx];
	struct rb_yesde **link = &root->rb_yesde;
	struct rb_yesde *yesde = &ltn->yesde[idx];
	struct rb_yesde *parent = NULL;
	struct latch_tree_yesde *ltp;

	while (*link) {
		parent = *link;
		ltp = __lt_from_rb(parent, idx);

		if (less(ltn, ltp))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_yesde_rcu(yesde, parent, link);
	rb_insert_color(yesde, root);
}

static __always_inline void
__lt_erase(struct latch_tree_yesde *ltn, struct latch_tree_root *ltr, int idx)
{
	rb_erase(&ltn->yesde[idx], &ltr->tree[idx]);
}

static __always_inline struct latch_tree_yesde *
__lt_find(void *key, struct latch_tree_root *ltr, int idx,
	  int (*comp)(void *key, struct latch_tree_yesde *yesde))
{
	struct rb_yesde *yesde = rcu_dereference_raw(ltr->tree[idx].rb_yesde);
	struct latch_tree_yesde *ltn;
	int c;

	while (yesde) {
		ltn = __lt_from_rb(yesde, idx);
		c = comp(key, ltn);

		if (c < 0)
			yesde = rcu_dereference_raw(yesde->rb_left);
		else if (c > 0)
			yesde = rcu_dereference_raw(yesde->rb_right);
		else
			return ltn;
	}

	return NULL;
}

/**
 * latch_tree_insert() - insert @yesde into the trees @root
 * @yesde: yesdes to insert
 * @root: trees to insert @yesde into
 * @ops: operators defining the yesde order
 *
 * It inserts @yesde into @root in an ordered fashion such that we can always
 * observe one complete tree. See the comment for raw_write_seqcount_latch().
 *
 * The inserts use rcu_assign_pointer() to publish the element such that the
 * tree structure is stored before we can observe the new @yesde.
 *
 * All modifications (latch_tree_insert, latch_tree_remove) are assumed to be
 * serialized.
 */
static __always_inline void
latch_tree_insert(struct latch_tree_yesde *yesde,
		  struct latch_tree_root *root,
		  const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(yesde, root, 0, ops->less);
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(yesde, root, 1, ops->less);
}

/**
 * latch_tree_erase() - removes @yesde from the trees @root
 * @yesde: yesdes to remote
 * @root: trees to remove @yesde from
 * @ops: operators defining the yesde order
 *
 * Removes @yesde from the trees @root in an ordered fashion such that we can
 * always observe one complete tree. See the comment for
 * raw_write_seqcount_latch().
 *
 * It is assumed that @yesde will observe one RCU quiescent state before being
 * reused of freed.
 *
 * All modifications (latch_tree_insert, latch_tree_remove) are assumed to be
 * serialized.
 */
static __always_inline void
latch_tree_erase(struct latch_tree_yesde *yesde,
		 struct latch_tree_root *root,
		 const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(yesde, root, 0);
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(yesde, root, 1);
}

/**
 * latch_tree_find() - find the yesde matching @key in the trees @root
 * @key: search key
 * @root: trees to search for @key
 * @ops: operators defining the yesde order
 *
 * Does a lockless lookup in the trees @root for the yesde matching @key.
 *
 * It is assumed that this is called while holding the appropriate RCU read
 * side lock.
 *
 * If the operators define a partial order on the elements (there are multiple
 * elements which have the same key value) it is undefined which of these
 * elements will be found. Nor is it possible to iterate the tree to find
 * further elements with the same key value.
 *
 * Returns: a pointer to the yesde matching @key or NULL.
 */
static __always_inline struct latch_tree_yesde *
latch_tree_find(void *key, struct latch_tree_root *root,
		const struct latch_tree_ops *ops)
{
	struct latch_tree_yesde *yesde;
	unsigned int seq;

	do {
		seq = raw_read_seqcount_latch(&root->seq);
		yesde = __lt_find(key, root, seq & 1, ops->comp);
	} while (read_seqcount_retry(&root->seq, seq));

	return yesde;
}

#endif /* RB_TREE_LATCH_H */
