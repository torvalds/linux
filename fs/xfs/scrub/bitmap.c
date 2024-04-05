// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2018-2023 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_bit.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "scrub/scrub.h"
#include "scrub/bitmap.h"

#include <linux/interval_tree_generic.h>

/* u64 bitmap */

struct xbitmap64_node {
	struct rb_node	bn_rbnode;

	/* First set bit of this interval and subtree. */
	uint64_t	bn_start;

	/* Last set bit of this interval. */
	uint64_t	bn_last;

	/* Last set bit of this subtree.  Do not touch this. */
	uint64_t	__bn_subtree_last;
};

/* Define our own interval tree type with uint64_t parameters. */

#define START(node) ((node)->bn_start)
#define LAST(node)  ((node)->bn_last)

/*
 * These functions are defined by the INTERVAL_TREE_DEFINE macro, but we'll
 * forward-declare them anyway for clarity.
 */
static inline void
xbitmap64_tree_insert(struct xbitmap64_node *node, struct rb_root_cached *root);

static inline void
xbitmap64_tree_remove(struct xbitmap64_node *node, struct rb_root_cached *root);

static inline struct xbitmap64_node *
xbitmap64_tree_iter_first(struct rb_root_cached *root, uint64_t start,
			uint64_t last);

static inline struct xbitmap64_node *
xbitmap64_tree_iter_next(struct xbitmap64_node *node, uint64_t start,
		       uint64_t last);

INTERVAL_TREE_DEFINE(struct xbitmap64_node, bn_rbnode, uint64_t,
		__bn_subtree_last, START, LAST, static inline, xbitmap64_tree)

/* Iterate each interval of a bitmap.  Do not change the bitmap. */
#define for_each_xbitmap64_extent(bn, bitmap) \
	for ((bn) = rb_entry_safe(rb_first(&(bitmap)->xb_root.rb_root), \
				   struct xbitmap64_node, bn_rbnode); \
	     (bn) != NULL; \
	     (bn) = rb_entry_safe(rb_next(&(bn)->bn_rbnode), \
				   struct xbitmap64_node, bn_rbnode))

/* Clear a range of this bitmap. */
int
xbitmap64_clear(
	struct xbitmap64	*bitmap,
	uint64_t		start,
	uint64_t		len)
{
	struct xbitmap64_node	*bn;
	struct xbitmap64_node	*new_bn;
	uint64_t		last = start + len - 1;

	while ((bn = xbitmap64_tree_iter_first(&bitmap->xb_root, start, last))) {
		if (bn->bn_start < start && bn->bn_last > last) {
			uint64_t	old_last = bn->bn_last;

			/* overlaps with the entire clearing range */
			xbitmap64_tree_remove(bn, &bitmap->xb_root);
			bn->bn_last = start - 1;
			xbitmap64_tree_insert(bn, &bitmap->xb_root);

			/* add an extent */
			new_bn = kmalloc(sizeof(struct xbitmap64_node),
					XCHK_GFP_FLAGS);
			if (!new_bn)
				return -ENOMEM;
			new_bn->bn_start = last + 1;
			new_bn->bn_last = old_last;
			xbitmap64_tree_insert(new_bn, &bitmap->xb_root);
		} else if (bn->bn_start < start) {
			/* overlaps with the left side of the clearing range */
			xbitmap64_tree_remove(bn, &bitmap->xb_root);
			bn->bn_last = start - 1;
			xbitmap64_tree_insert(bn, &bitmap->xb_root);
		} else if (bn->bn_last > last) {
			/* overlaps with the right side of the clearing range */
			xbitmap64_tree_remove(bn, &bitmap->xb_root);
			bn->bn_start = last + 1;
			xbitmap64_tree_insert(bn, &bitmap->xb_root);
			break;
		} else {
			/* in the middle of the clearing range */
			xbitmap64_tree_remove(bn, &bitmap->xb_root);
			kfree(bn);
		}
	}

	return 0;
}

/* Set a range of this bitmap. */
int
xbitmap64_set(
	struct xbitmap64	*bitmap,
	uint64_t		start,
	uint64_t		len)
{
	struct xbitmap64_node	*left;
	struct xbitmap64_node	*right;
	uint64_t		last = start + len - 1;
	int			error;

	/* Is this whole range already set? */
	left = xbitmap64_tree_iter_first(&bitmap->xb_root, start, last);
	if (left && left->bn_start <= start && left->bn_last >= last)
		return 0;

	/* Clear out everything in the range we want to set. */
	error = xbitmap64_clear(bitmap, start, len);
	if (error)
		return error;

	/* Do we have a left-adjacent extent? */
	left = xbitmap64_tree_iter_first(&bitmap->xb_root, start - 1, start - 1);
	ASSERT(!left || left->bn_last + 1 == start);

	/* Do we have a right-adjacent extent? */
	right = xbitmap64_tree_iter_first(&bitmap->xb_root, last + 1, last + 1);
	ASSERT(!right || right->bn_start == last + 1);

	if (left && right) {
		/* combine left and right adjacent extent */
		xbitmap64_tree_remove(left, &bitmap->xb_root);
		xbitmap64_tree_remove(right, &bitmap->xb_root);
		left->bn_last = right->bn_last;
		xbitmap64_tree_insert(left, &bitmap->xb_root);
		kfree(right);
	} else if (left) {
		/* combine with left extent */
		xbitmap64_tree_remove(left, &bitmap->xb_root);
		left->bn_last = last;
		xbitmap64_tree_insert(left, &bitmap->xb_root);
	} else if (right) {
		/* combine with right extent */
		xbitmap64_tree_remove(right, &bitmap->xb_root);
		right->bn_start = start;
		xbitmap64_tree_insert(right, &bitmap->xb_root);
	} else {
		/* add an extent */
		left = kmalloc(sizeof(struct xbitmap64_node), XCHK_GFP_FLAGS);
		if (!left)
			return -ENOMEM;
		left->bn_start = start;
		left->bn_last = last;
		xbitmap64_tree_insert(left, &bitmap->xb_root);
	}

	return 0;
}

/* Free everything related to this bitmap. */
void
xbitmap64_destroy(
	struct xbitmap64	*bitmap)
{
	struct xbitmap64_node	*bn;

	while ((bn = xbitmap64_tree_iter_first(&bitmap->xb_root, 0, -1ULL))) {
		xbitmap64_tree_remove(bn, &bitmap->xb_root);
		kfree(bn);
	}
}

/* Set up a per-AG block bitmap. */
void
xbitmap64_init(
	struct xbitmap64	*bitmap)
{
	bitmap->xb_root = RB_ROOT_CACHED;
}

/*
 * Remove all the blocks mentioned in @sub from the extents in @bitmap.
 *
 * The intent is that callers will iterate the rmapbt for all of its records
 * for a given owner to generate @bitmap; and iterate all the blocks of the
 * metadata structures that are not being rebuilt and have the same rmapbt
 * owner to generate @sub.  This routine subtracts all the extents
 * mentioned in sub from all the extents linked in @bitmap, which leaves
 * @bitmap as the list of blocks that are not accounted for, which we assume
 * are the dead blocks of the old metadata structure.  The blocks mentioned in
 * @bitmap can be reaped.
 *
 * This is the logical equivalent of bitmap &= ~sub.
 */
int
xbitmap64_disunion(
	struct xbitmap64	*bitmap,
	struct xbitmap64	*sub)
{
	struct xbitmap64_node	*bn;
	int			error;

	if (xbitmap64_empty(bitmap) || xbitmap64_empty(sub))
		return 0;

	for_each_xbitmap64_extent(bn, sub) {
		error = xbitmap64_clear(bitmap, bn->bn_start,
				bn->bn_last - bn->bn_start + 1);
		if (error)
			return error;
	}

	return 0;
}

/* How many bits are set in this bitmap? */
uint64_t
xbitmap64_hweight(
	struct xbitmap64	*bitmap)
{
	struct xbitmap64_node	*bn;
	uint64_t		ret = 0;

	for_each_xbitmap64_extent(bn, bitmap)
		ret += bn->bn_last - bn->bn_start + 1;

	return ret;
}

/* Call a function for every run of set bits in this bitmap. */
int
xbitmap64_walk(
	struct xbitmap64	*bitmap,
	xbitmap64_walk_fn		fn,
	void			*priv)
{
	struct xbitmap64_node	*bn;
	int			error = 0;

	for_each_xbitmap64_extent(bn, bitmap) {
		error = fn(bn->bn_start, bn->bn_last - bn->bn_start + 1, priv);
		if (error)
			break;
	}

	return error;
}

/* Does this bitmap have no bits set at all? */
bool
xbitmap64_empty(
	struct xbitmap64	*bitmap)
{
	return bitmap->xb_root.rb_root.rb_node == NULL;
}

/* Is the start of the range set or clear?  And for how long? */
bool
xbitmap64_test(
	struct xbitmap64	*bitmap,
	uint64_t		start,
	uint64_t		*len)
{
	struct xbitmap64_node	*bn;
	uint64_t		last = start + *len - 1;

	bn = xbitmap64_tree_iter_first(&bitmap->xb_root, start, last);
	if (!bn)
		return false;
	if (bn->bn_start <= start) {
		if (bn->bn_last < last)
			*len = bn->bn_last - start + 1;
		return true;
	}
	*len = bn->bn_start - start;
	return false;
}

/* u32 bitmap */

struct xbitmap32_node {
	struct rb_node	bn_rbnode;

	/* First set bit of this interval and subtree. */
	uint32_t	bn_start;

	/* Last set bit of this interval. */
	uint32_t	bn_last;

	/* Last set bit of this subtree.  Do not touch this. */
	uint32_t	__bn_subtree_last;
};

/* Define our own interval tree type with uint32_t parameters. */

/*
 * These functions are defined by the INTERVAL_TREE_DEFINE macro, but we'll
 * forward-declare them anyway for clarity.
 */
static inline void
xbitmap32_tree_insert(struct xbitmap32_node *node, struct rb_root_cached *root);

static inline void
xbitmap32_tree_remove(struct xbitmap32_node *node, struct rb_root_cached *root);

static inline struct xbitmap32_node *
xbitmap32_tree_iter_first(struct rb_root_cached *root, uint32_t start,
			  uint32_t last);

static inline struct xbitmap32_node *
xbitmap32_tree_iter_next(struct xbitmap32_node *node, uint32_t start,
			 uint32_t last);

INTERVAL_TREE_DEFINE(struct xbitmap32_node, bn_rbnode, uint32_t,
		__bn_subtree_last, START, LAST, static inline, xbitmap32_tree)

/* Iterate each interval of a bitmap.  Do not change the bitmap. */
#define for_each_xbitmap32_extent(bn, bitmap) \
	for ((bn) = rb_entry_safe(rb_first(&(bitmap)->xb_root.rb_root), \
				   struct xbitmap32_node, bn_rbnode); \
	     (bn) != NULL; \
	     (bn) = rb_entry_safe(rb_next(&(bn)->bn_rbnode), \
				   struct xbitmap32_node, bn_rbnode))

/* Clear a range of this bitmap. */
int
xbitmap32_clear(
	struct xbitmap32	*bitmap,
	uint32_t		start,
	uint32_t		len)
{
	struct xbitmap32_node	*bn;
	struct xbitmap32_node	*new_bn;
	uint32_t		last = start + len - 1;

	while ((bn = xbitmap32_tree_iter_first(&bitmap->xb_root, start, last))) {
		if (bn->bn_start < start && bn->bn_last > last) {
			uint32_t	old_last = bn->bn_last;

			/* overlaps with the entire clearing range */
			xbitmap32_tree_remove(bn, &bitmap->xb_root);
			bn->bn_last = start - 1;
			xbitmap32_tree_insert(bn, &bitmap->xb_root);

			/* add an extent */
			new_bn = kmalloc(sizeof(struct xbitmap32_node),
					XCHK_GFP_FLAGS);
			if (!new_bn)
				return -ENOMEM;
			new_bn->bn_start = last + 1;
			new_bn->bn_last = old_last;
			xbitmap32_tree_insert(new_bn, &bitmap->xb_root);
		} else if (bn->bn_start < start) {
			/* overlaps with the left side of the clearing range */
			xbitmap32_tree_remove(bn, &bitmap->xb_root);
			bn->bn_last = start - 1;
			xbitmap32_tree_insert(bn, &bitmap->xb_root);
		} else if (bn->bn_last > last) {
			/* overlaps with the right side of the clearing range */
			xbitmap32_tree_remove(bn, &bitmap->xb_root);
			bn->bn_start = last + 1;
			xbitmap32_tree_insert(bn, &bitmap->xb_root);
			break;
		} else {
			/* in the middle of the clearing range */
			xbitmap32_tree_remove(bn, &bitmap->xb_root);
			kfree(bn);
		}
	}

	return 0;
}

/* Set a range of this bitmap. */
int
xbitmap32_set(
	struct xbitmap32	*bitmap,
	uint32_t		start,
	uint32_t		len)
{
	struct xbitmap32_node	*left;
	struct xbitmap32_node	*right;
	uint32_t		last = start + len - 1;
	int			error;

	/* Is this whole range already set? */
	left = xbitmap32_tree_iter_first(&bitmap->xb_root, start, last);
	if (left && left->bn_start <= start && left->bn_last >= last)
		return 0;

	/* Clear out everything in the range we want to set. */
	error = xbitmap32_clear(bitmap, start, len);
	if (error)
		return error;

	/* Do we have a left-adjacent extent? */
	left = xbitmap32_tree_iter_first(&bitmap->xb_root, start - 1, start - 1);
	ASSERT(!left || left->bn_last + 1 == start);

	/* Do we have a right-adjacent extent? */
	right = xbitmap32_tree_iter_first(&bitmap->xb_root, last + 1, last + 1);
	ASSERT(!right || right->bn_start == last + 1);

	if (left && right) {
		/* combine left and right adjacent extent */
		xbitmap32_tree_remove(left, &bitmap->xb_root);
		xbitmap32_tree_remove(right, &bitmap->xb_root);
		left->bn_last = right->bn_last;
		xbitmap32_tree_insert(left, &bitmap->xb_root);
		kfree(right);
	} else if (left) {
		/* combine with left extent */
		xbitmap32_tree_remove(left, &bitmap->xb_root);
		left->bn_last = last;
		xbitmap32_tree_insert(left, &bitmap->xb_root);
	} else if (right) {
		/* combine with right extent */
		xbitmap32_tree_remove(right, &bitmap->xb_root);
		right->bn_start = start;
		xbitmap32_tree_insert(right, &bitmap->xb_root);
	} else {
		/* add an extent */
		left = kmalloc(sizeof(struct xbitmap32_node), XCHK_GFP_FLAGS);
		if (!left)
			return -ENOMEM;
		left->bn_start = start;
		left->bn_last = last;
		xbitmap32_tree_insert(left, &bitmap->xb_root);
	}

	return 0;
}

/* Free everything related to this bitmap. */
void
xbitmap32_destroy(
	struct xbitmap32	*bitmap)
{
	struct xbitmap32_node	*bn;

	while ((bn = xbitmap32_tree_iter_first(&bitmap->xb_root, 0, -1U))) {
		xbitmap32_tree_remove(bn, &bitmap->xb_root);
		kfree(bn);
	}
}

/* Set up a per-AG block bitmap. */
void
xbitmap32_init(
	struct xbitmap32	*bitmap)
{
	bitmap->xb_root = RB_ROOT_CACHED;
}

/*
 * Remove all the blocks mentioned in @sub from the extents in @bitmap.
 *
 * The intent is that callers will iterate the rmapbt for all of its records
 * for a given owner to generate @bitmap; and iterate all the blocks of the
 * metadata structures that are not being rebuilt and have the same rmapbt
 * owner to generate @sub.  This routine subtracts all the extents
 * mentioned in sub from all the extents linked in @bitmap, which leaves
 * @bitmap as the list of blocks that are not accounted for, which we assume
 * are the dead blocks of the old metadata structure.  The blocks mentioned in
 * @bitmap can be reaped.
 *
 * This is the logical equivalent of bitmap &= ~sub.
 */
int
xbitmap32_disunion(
	struct xbitmap32	*bitmap,
	struct xbitmap32	*sub)
{
	struct xbitmap32_node	*bn;
	int			error;

	if (xbitmap32_empty(bitmap) || xbitmap32_empty(sub))
		return 0;

	for_each_xbitmap32_extent(bn, sub) {
		error = xbitmap32_clear(bitmap, bn->bn_start,
				bn->bn_last - bn->bn_start + 1);
		if (error)
			return error;
	}

	return 0;
}

/* How many bits are set in this bitmap? */
uint32_t
xbitmap32_hweight(
	struct xbitmap32	*bitmap)
{
	struct xbitmap32_node	*bn;
	uint32_t		ret = 0;

	for_each_xbitmap32_extent(bn, bitmap)
		ret += bn->bn_last - bn->bn_start + 1;

	return ret;
}

/* Call a function for every run of set bits in this bitmap. */
int
xbitmap32_walk(
	struct xbitmap32	*bitmap,
	xbitmap32_walk_fn	fn,
	void			*priv)
{
	struct xbitmap32_node	*bn;
	int			error = 0;

	for_each_xbitmap32_extent(bn, bitmap) {
		error = fn(bn->bn_start, bn->bn_last - bn->bn_start + 1, priv);
		if (error)
			break;
	}

	return error;
}

/* Does this bitmap have no bits set at all? */
bool
xbitmap32_empty(
	struct xbitmap32	*bitmap)
{
	return bitmap->xb_root.rb_root.rb_node == NULL;
}

/* Is the start of the range set or clear?  And for how long? */
bool
xbitmap32_test(
	struct xbitmap32	*bitmap,
	uint32_t		start,
	uint32_t		*len)
{
	struct xbitmap32_node	*bn;
	uint32_t		last = start + *len - 1;

	bn = xbitmap32_tree_iter_first(&bitmap->xb_root, start, last);
	if (!bn)
		return false;
	if (bn->bn_start <= start) {
		if (bn->bn_last < last)
			*len = bn->bn_last - start + 1;
		return true;
	}
	*len = bn->bn_start - start;
	return false;
}

/* Count the number of set regions in this bitmap. */
uint32_t
xbitmap32_count_set_regions(
	struct xbitmap32	*bitmap)
{
	struct xbitmap32_node	*bn;
	uint32_t		nr = 0;

	for_each_xbitmap32_extent(bn, bitmap)
		nr++;

	return nr;
}
