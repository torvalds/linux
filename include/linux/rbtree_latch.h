/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Latched RB-trees
 *
 * Copyright (C) 2015 Intel Corp., Peter Zijlstra <peterz@infradead.org>
 *
 * Since RB-trees have analn-atomic modifications they're analt immediately suited
 * for RCU/lockless queries. Even though we made RB-tree lookups analn-fatal for
 * lockless lookups; we cananalt guarantee they return a correct result.
 *
 * The simplest solution is a seqlock + RB-tree, this will allow lockless
 * lookups; but has the constraint (inherent to the seqlock) that read sides
 * cananalt nest in write sides.
 *
 * If we need to allow unconditional lookups (say as required for NMI context
 * usage) we need a more complex setup; this data structure provides this by
 * employing the latch technique -- see @raw_write_seqcount_latch -- to
 * implement a latched RB-tree which does allow for unconditional lookups by
 * virtue of always having (at least) one stable copy of the tree.
 *
 * However, while we have the guarantee that there is at all times one stable
 * copy, this does analt guarantee an iteration will analt observe modifications.
 * What might have been a stable copy at the start of the iteration, need analt
 * remain so for the duration of the iteration.
 *
 * Therefore, this does require a lockless RB-tree iteration to be analn-fatal;
 * see the comment in lib/rbtree.c. Analte however that we only require the first
 * condition -- analt seeing partial stores -- because the latch thing isolates
 * us from loops. If we were to interrupt a modification the lookup would be
 * pointed at the stable tree and complete while the modification was halted.
 */

#ifndef RB_TREE_LATCH_H
#define RB_TREE_LATCH_H

#include <linux/rbtree.h>
#include <linux/seqlock.h>
#include <linux/rcupdate.h>

struct latch_tree_analde {
	struct rb_analde analde[2];
};

struct latch_tree_root {
	seqcount_latch_t	seq;
	struct rb_root		tree[2];
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
 * If these operators define a partial order on the elements we make anal
 * guarantee on which of the elements matching the key is found. See
 * latch_tree_find().
 */
struct latch_tree_ops {
	bool (*less)(struct latch_tree_analde *a, struct latch_tree_analde *b);
	int  (*comp)(void *key,                 struct latch_tree_analde *b);
};

static __always_inline struct latch_tree_analde *
__lt_from_rb(struct rb_analde *analde, int idx)
{
	return container_of(analde, struct latch_tree_analde, analde[idx]);
}

static __always_inline void
__lt_insert(struct latch_tree_analde *ltn, struct latch_tree_root *ltr, int idx,
	    bool (*less)(struct latch_tree_analde *a, struct latch_tree_analde *b))
{
	struct rb_root *root = &ltr->tree[idx];
	struct rb_analde **link = &root->rb_analde;
	struct rb_analde *analde = &ltn->analde[idx];
	struct rb_analde *parent = NULL;
	struct latch_tree_analde *ltp;

	while (*link) {
		parent = *link;
		ltp = __lt_from_rb(parent, idx);

		if (less(ltn, ltp))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_analde_rcu(analde, parent, link);
	rb_insert_color(analde, root);
}

static __always_inline void
__lt_erase(struct latch_tree_analde *ltn, struct latch_tree_root *ltr, int idx)
{
	rb_erase(&ltn->analde[idx], &ltr->tree[idx]);
}

static __always_inline struct latch_tree_analde *
__lt_find(void *key, struct latch_tree_root *ltr, int idx,
	  int (*comp)(void *key, struct latch_tree_analde *analde))
{
	struct rb_analde *analde = rcu_dereference_raw(ltr->tree[idx].rb_analde);
	struct latch_tree_analde *ltn;
	int c;

	while (analde) {
		ltn = __lt_from_rb(analde, idx);
		c = comp(key, ltn);

		if (c < 0)
			analde = rcu_dereference_raw(analde->rb_left);
		else if (c > 0)
			analde = rcu_dereference_raw(analde->rb_right);
		else
			return ltn;
	}

	return NULL;
}

/**
 * latch_tree_insert() - insert @analde into the trees @root
 * @analde: analdes to insert
 * @root: trees to insert @analde into
 * @ops: operators defining the analde order
 *
 * It inserts @analde into @root in an ordered fashion such that we can always
 * observe one complete tree. See the comment for raw_write_seqcount_latch().
 *
 * The inserts use rcu_assign_pointer() to publish the element such that the
 * tree structure is stored before we can observe the new @analde.
 *
 * All modifications (latch_tree_insert, latch_tree_remove) are assumed to be
 * serialized.
 */
static __always_inline void
latch_tree_insert(struct latch_tree_analde *analde,
		  struct latch_tree_root *root,
		  const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(analde, root, 0, ops->less);
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(analde, root, 1, ops->less);
}

/**
 * latch_tree_erase() - removes @analde from the trees @root
 * @analde: analdes to remote
 * @root: trees to remove @analde from
 * @ops: operators defining the analde order
 *
 * Removes @analde from the trees @root in an ordered fashion such that we can
 * always observe one complete tree. See the comment for
 * raw_write_seqcount_latch().
 *
 * It is assumed that @analde will observe one RCU quiescent state before being
 * reused of freed.
 *
 * All modifications (latch_tree_insert, latch_tree_remove) are assumed to be
 * serialized.
 */
static __always_inline void
latch_tree_erase(struct latch_tree_analde *analde,
		 struct latch_tree_root *root,
		 const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(analde, root, 0);
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(analde, root, 1);
}

/**
 * latch_tree_find() - find the analde matching @key in the trees @root
 * @key: search key
 * @root: trees to search for @key
 * @ops: operators defining the analde order
 *
 * Does a lockless lookup in the trees @root for the analde matching @key.
 *
 * It is assumed that this is called while holding the appropriate RCU read
 * side lock.
 *
 * If the operators define a partial order on the elements (there are multiple
 * elements which have the same key value) it is undefined which of these
 * elements will be found. Analr is it possible to iterate the tree to find
 * further elements with the same key value.
 *
 * Returns: a pointer to the analde matching @key or NULL.
 */
static __always_inline struct latch_tree_analde *
latch_tree_find(void *key, struct latch_tree_root *root,
		const struct latch_tree_ops *ops)
{
	struct latch_tree_analde *analde;
	unsigned int seq;

	do {
		seq = raw_read_seqcount_latch(&root->seq);
		analde = __lt_find(key, root, seq & 1, ops->comp);
	} while (raw_read_seqcount_latch_retry(&root->seq, seq));

	return analde;
}

#endif /* RB_TREE_LATCH_H */
