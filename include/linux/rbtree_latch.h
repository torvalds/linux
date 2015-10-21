/*
 * Latched RB-trees
 *
 * Copyright (C) 2015 Intel Corp., Peter Zijlstra <peterz@infradead.org>
 *
 * Since RB-trees have non-atomic modifications they're not immediately suited
 * for RCU/lockless queries. Even though we made RB-tree lookups non-fatal for
 * lockless lookups; we cannot guarantee they return a correct result.
 *
 * The simplest solution is a seqlock + RB-tree, this will allow lockless
 * lookups; but has the constraint (inherent to the seqlock) that read sides
 * cannot nest in write sides.
 *
 * If we need to allow unconditional lookups (say as required for NMI context
 * usage) we need a more complex setup; this data structure provides this by
 * employing the latch technique -- see @raw_write_seqcount_latch -- to
 * implement a latched RB-tree which does allow for unconditional lookups by
 * virtue of always having (at least) one stable copy of the tree.
 *
 * However, while we have the guarantee that there is at all times one stable
 * copy, this does not guarantee an iteration will not observe modifications.
 * What might have been a stable copy at the start of the iteration, need not
 * remain so for the duration of the iteration.
 *
 * Therefore, this does require a lockless RB-tree iteration to be non-fatal;
 * see the comment in lib/rbtree.c. Note however that we only require the first
 * condition -- not seeing partial stores -- because the latch thing isolates
 * us from loops. If we were to interrupt a modification the lookup would be
 * pointed at the stable tree and complete while the modification was halted.
 */

#ifndef RB_TREE_LATCH_H
#define RB_TREE_LATCH_H

#include <linux/rbtree.h>
#include <linux/seqlock.h>

struct latch_tree_node {
	struct rb_node node[2];
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
 * If these operators define a partial order on the elements we make no
 * guarantee on which of the elements matching the key is found. See
 * latch_tree_find().
 */
struct latch_tree_ops {
	bool (*less)(struct latch_tree_node *a, struct latch_tree_node *b);
	int  (*comp)(void *key,                 struct latch_tree_node *b);
};

static __always_inline struct latch_tree_node *
__lt_from_rb(struct rb_node *node, int idx)
{
	return container_of(node, struct latch_tree_node, node[idx]);
}

static __always_inline void
__lt_insert(struct latch_tree_node *ltn, struct latch_tree_root *ltr, int idx,
	    bool (*less)(struct latch_tree_node *a, struct latch_tree_node *b))
{
	struct rb_root *root = &ltr->tree[idx];
	struct rb_node **link = &root->rb_node;
	struct rb_node *node = &ltn->node[idx];
	struct rb_node *parent = NULL;
	struct latch_tree_node *ltp;

	while (*link) {
		parent = *link;
		ltp = __lt_from_rb(parent, idx);

		if (less(ltn, ltp))
			link = &parent->rb_left;
		else
			link = &parent->rb_right;
	}

	rb_link_node_rcu(node, parent, link);
	rb_insert_color(node, root);
}

static __always_inline void
__lt_erase(struct latch_tree_node *ltn, struct latch_tree_root *ltr, int idx)
{
	rb_erase(&ltn->node[idx], &ltr->tree[idx]);
}

static __always_inline struct latch_tree_node *
__lt_find(void *key, struct latch_tree_root *ltr, int idx,
	  int (*comp)(void *key, struct latch_tree_node *node))
{
	struct rb_node *node = rcu_dereference_raw(ltr->tree[idx].rb_node);
	struct latch_tree_node *ltn;
	int c;

	while (node) {
		ltn = __lt_from_rb(node, idx);
		c = comp(key, ltn);

		if (c < 0)
			node = rcu_dereference_raw(node->rb_left);
		else if (c > 0)
			node = rcu_dereference_raw(node->rb_right);
		else
			return ltn;
	}

	return NULL;
}

/**
 * latch_tree_insert() - insert @node into the trees @root
 * @node: nodes to insert
 * @root: trees to insert @node into
 * @ops: operators defining the node order
 *
 * It inserts @node into @root in an ordered fashion such that we can always
 * observe one complete tree. See the comment for raw_write_seqcount_latch().
 *
 * The inserts use rcu_assign_pointer() to publish the element such that the
 * tree structure is stored before we can observe the new @node.
 *
 * All modifications (latch_tree_insert, latch_tree_remove) are assumed to be
 * serialized.
 */
static __always_inline void
latch_tree_insert(struct latch_tree_node *node,
		  struct latch_tree_root *root,
		  const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(node, root, 0, ops->less);
	raw_write_seqcount_latch(&root->seq);
	__lt_insert(node, root, 1, ops->less);
}

/**
 * latch_tree_erase() - removes @node from the trees @root
 * @node: nodes to remote
 * @root: trees to remove @node from
 * @ops: operators defining the node order
 *
 * Removes @node from the trees @root in an ordered fashion such that we can
 * always observe one complete tree. See the comment for
 * raw_write_seqcount_latch().
 *
 * It is assumed that @node will observe one RCU quiescent state before being
 * reused of freed.
 *
 * All modifications (latch_tree_insert, latch_tree_remove) are assumed to be
 * serialized.
 */
static __always_inline void
latch_tree_erase(struct latch_tree_node *node,
		 struct latch_tree_root *root,
		 const struct latch_tree_ops *ops)
{
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(node, root, 0);
	raw_write_seqcount_latch(&root->seq);
	__lt_erase(node, root, 1);
}

/**
 * latch_tree_find() - find the node matching @key in the trees @root
 * @key: search key
 * @root: trees to search for @key
 * @ops: operators defining the node order
 *
 * Does a lockless lookup in the trees @root for the node matching @key.
 *
 * It is assumed that this is called while holding the appropriate RCU read
 * side lock.
 *
 * If the operators define a partial order on the elements (there are multiple
 * elements which have the same key value) it is undefined which of these
 * elements will be found. Nor is it possible to iterate the tree to find
 * further elements with the same key value.
 *
 * Returns: a pointer to the node matching @key or NULL.
 */
static __always_inline struct latch_tree_node *
latch_tree_find(void *key, struct latch_tree_root *root,
		const struct latch_tree_ops *ops)
{
	struct latch_tree_node *node;
	unsigned int seq;

	do {
		seq = raw_read_seqcount_latch(&root->seq);
		node = __lt_find(key, root, seq & 1, ops->comp);
	} while (read_seqcount_retry(&root->seq, seq));

	return node;
}

#endif /* RB_TREE_LATCH_H */
