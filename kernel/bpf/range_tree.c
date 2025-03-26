// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2024 Meta Platforms, Inc. and affiliates. */
#include <linux/interval_tree_generic.h>
#include <linux/slab.h>
#include <linux/bpf_mem_alloc.h>
#include <linux/bpf.h>
#include "range_tree.h"

/*
 * struct range_tree is a data structure used to allocate contiguous memory
 * ranges in bpf arena. It's a large bitmap. The contiguous sequence of bits is
 * represented by struct range_node or 'rn' for short.
 * rn->rn_rbnode links it into an interval tree while
 * rn->rb_range_size links it into a second rbtree sorted by size of the range.
 * __find_range() performs binary search and best fit algorithm to find the
 * range less or equal requested size.
 * range_tree_clear/set() clears or sets a range of bits in this bitmap. The
 * adjacent ranges are merged or split at the same time.
 *
 * The split/merge logic is based/borrowed from XFS's xbitmap32 added
 * in commit 6772fcc8890a ("xfs: convert xbitmap to interval tree").
 *
 * The implementation relies on external lock to protect rbtree-s.
 * The alloc/free of range_node-s is done via bpf_mem_alloc.
 *
 * bpf arena is using range_tree to represent unallocated slots.
 * At init time:
 *   range_tree_set(rt, 0, max);
 * Then:
 *   start = range_tree_find(rt, len);
 *   if (start >= 0)
 *     range_tree_clear(rt, start, len);
 * to find free range and mark slots as allocated and later:
 *   range_tree_set(rt, start, len);
 * to mark as unallocated after use.
 */
struct range_node {
	struct rb_node rn_rbnode;
	struct rb_node rb_range_size;
	u32 rn_start;
	u32 rn_last; /* inclusive */
	u32 __rn_subtree_last;
};

static struct range_node *rb_to_range_node(struct rb_node *rb)
{
	return rb_entry(rb, struct range_node, rb_range_size);
}

static u32 rn_size(struct range_node *rn)
{
	return rn->rn_last - rn->rn_start + 1;
}

/* Find range that fits best to requested size */
static inline struct range_node *__find_range(struct range_tree *rt, u32 len)
{
	struct rb_node *rb = rt->range_size_root.rb_root.rb_node;
	struct range_node *best = NULL;

	while (rb) {
		struct range_node *rn = rb_to_range_node(rb);

		if (len <= rn_size(rn)) {
			best = rn;
			rb = rb->rb_right;
		} else {
			rb = rb->rb_left;
		}
	}

	return best;
}

s64 range_tree_find(struct range_tree *rt, u32 len)
{
	struct range_node *rn;

	rn = __find_range(rt, len);
	if (!rn)
		return -ENOENT;
	return rn->rn_start;
}

/* Insert the range into rbtree sorted by the range size */
static inline void __range_size_insert(struct range_node *rn,
				       struct rb_root_cached *root)
{
	struct rb_node **link = &root->rb_root.rb_node, *rb = NULL;
	u64 size = rn_size(rn);
	bool leftmost = true;

	while (*link) {
		rb = *link;
		if (size > rn_size(rb_to_range_node(rb))) {
			link = &rb->rb_left;
		} else {
			link = &rb->rb_right;
			leftmost = false;
		}
	}

	rb_link_node(&rn->rb_range_size, rb, link);
	rb_insert_color_cached(&rn->rb_range_size, root, leftmost);
}

#define START(node) ((node)->rn_start)
#define LAST(node)  ((node)->rn_last)

INTERVAL_TREE_DEFINE(struct range_node, rn_rbnode, u32,
		     __rn_subtree_last, START, LAST,
		     static inline __maybe_unused,
		     __range_it)

static inline __maybe_unused void
range_it_insert(struct range_node *rn, struct range_tree *rt)
{
	__range_size_insert(rn, &rt->range_size_root);
	__range_it_insert(rn, &rt->it_root);
}

static inline __maybe_unused void
range_it_remove(struct range_node *rn, struct range_tree *rt)
{
	rb_erase_cached(&rn->rb_range_size, &rt->range_size_root);
	RB_CLEAR_NODE(&rn->rb_range_size);
	__range_it_remove(rn, &rt->it_root);
}

static inline __maybe_unused struct range_node *
range_it_iter_first(struct range_tree *rt, u32 start, u32 last)
{
	return __range_it_iter_first(&rt->it_root, start, last);
}

/* Clear the range in this range tree */
int range_tree_clear(struct range_tree *rt, u32 start, u32 len)
{
	u32 last = start + len - 1;
	struct range_node *new_rn;
	struct range_node *rn;

	while ((rn = range_it_iter_first(rt, start, last))) {
		if (rn->rn_start < start && rn->rn_last > last) {
			u32 old_last = rn->rn_last;

			/* Overlaps with the entire clearing range */
			range_it_remove(rn, rt);
			rn->rn_last = start - 1;
			range_it_insert(rn, rt);

			/* Add a range */
			migrate_disable();
			new_rn = bpf_mem_alloc(&bpf_global_ma, sizeof(struct range_node));
			migrate_enable();
			if (!new_rn)
				return -ENOMEM;
			new_rn->rn_start = last + 1;
			new_rn->rn_last = old_last;
			range_it_insert(new_rn, rt);
		} else if (rn->rn_start < start) {
			/* Overlaps with the left side of the clearing range */
			range_it_remove(rn, rt);
			rn->rn_last = start - 1;
			range_it_insert(rn, rt);
		} else if (rn->rn_last > last) {
			/* Overlaps with the right side of the clearing range */
			range_it_remove(rn, rt);
			rn->rn_start = last + 1;
			range_it_insert(rn, rt);
			break;
		} else {
			/* in the middle of the clearing range */
			range_it_remove(rn, rt);
			migrate_disable();
			bpf_mem_free(&bpf_global_ma, rn);
			migrate_enable();
		}
	}
	return 0;
}

/* Is the whole range set ? */
int is_range_tree_set(struct range_tree *rt, u32 start, u32 len)
{
	u32 last = start + len - 1;
	struct range_node *left;

	/* Is this whole range set ? */
	left = range_it_iter_first(rt, start, last);
	if (left && left->rn_start <= start && left->rn_last >= last)
		return 0;
	return -ESRCH;
}

/* Set the range in this range tree */
int range_tree_set(struct range_tree *rt, u32 start, u32 len)
{
	u32 last = start + len - 1;
	struct range_node *right;
	struct range_node *left;
	int err;

	/* Is this whole range already set ? */
	left = range_it_iter_first(rt, start, last);
	if (left && left->rn_start <= start && left->rn_last >= last)
		return 0;

	/* Clear out everything in the range we want to set. */
	err = range_tree_clear(rt, start, len);
	if (err)
		return err;

	/* Do we have a left-adjacent range ? */
	left = range_it_iter_first(rt, start - 1, start - 1);
	if (left && left->rn_last + 1 != start)
		return -EFAULT;

	/* Do we have a right-adjacent range ? */
	right = range_it_iter_first(rt, last + 1, last + 1);
	if (right && right->rn_start != last + 1)
		return -EFAULT;

	if (left && right) {
		/* Combine left and right adjacent ranges */
		range_it_remove(left, rt);
		range_it_remove(right, rt);
		left->rn_last = right->rn_last;
		range_it_insert(left, rt);
		migrate_disable();
		bpf_mem_free(&bpf_global_ma, right);
		migrate_enable();
	} else if (left) {
		/* Combine with the left range */
		range_it_remove(left, rt);
		left->rn_last = last;
		range_it_insert(left, rt);
	} else if (right) {
		/* Combine with the right range */
		range_it_remove(right, rt);
		right->rn_start = start;
		range_it_insert(right, rt);
	} else {
		migrate_disable();
		left = bpf_mem_alloc(&bpf_global_ma, sizeof(struct range_node));
		migrate_enable();
		if (!left)
			return -ENOMEM;
		left->rn_start = start;
		left->rn_last = last;
		range_it_insert(left, rt);
	}
	return 0;
}

void range_tree_destroy(struct range_tree *rt)
{
	struct range_node *rn;

	while ((rn = range_it_iter_first(rt, 0, -1U))) {
		range_it_remove(rn, rt);
		bpf_mem_free(&bpf_global_ma, rn);
	}
}

void range_tree_init(struct range_tree *rt)
{
	rt->it_root = RB_ROOT_CACHED;
	rt->range_size_root = RB_ROOT_CACHED;
}
