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

struct xbitmap_node {
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
xbitmap_tree_insert(struct xbitmap_node *node, struct rb_root_cached *root);

static inline void
xbitmap_tree_remove(struct xbitmap_node *node, struct rb_root_cached *root);

static inline struct xbitmap_node *
xbitmap_tree_iter_first(struct rb_root_cached *root, uint64_t start,
			uint64_t last);

static inline struct xbitmap_node *
xbitmap_tree_iter_next(struct xbitmap_node *node, uint64_t start,
		       uint64_t last);

INTERVAL_TREE_DEFINE(struct xbitmap_node, bn_rbnode, uint64_t,
		__bn_subtree_last, START, LAST, static inline, xbitmap_tree)

/* Iterate each interval of a bitmap.  Do not change the bitmap. */
#define for_each_xbitmap_extent(bn, bitmap) \
	for ((bn) = rb_entry_safe(rb_first(&(bitmap)->xb_root.rb_root), \
				   struct xbitmap_node, bn_rbnode); \
	     (bn) != NULL; \
	     (bn) = rb_entry_safe(rb_next(&(bn)->bn_rbnode), \
				   struct xbitmap_node, bn_rbnode))

/* Clear a range of this bitmap. */
int
xbitmap_clear(
	struct xbitmap		*bitmap,
	uint64_t		start,
	uint64_t		len)
{
	struct xbitmap_node	*bn;
	struct xbitmap_node	*new_bn;
	uint64_t		last = start + len - 1;

	while ((bn = xbitmap_tree_iter_first(&bitmap->xb_root, start, last))) {
		if (bn->bn_start < start && bn->bn_last > last) {
			uint64_t	old_last = bn->bn_last;

			/* overlaps with the entire clearing range */
			xbitmap_tree_remove(bn, &bitmap->xb_root);
			bn->bn_last = start - 1;
			xbitmap_tree_insert(bn, &bitmap->xb_root);

			/* add an extent */
			new_bn = kmalloc(sizeof(struct xbitmap_node),
					XCHK_GFP_FLAGS);
			if (!new_bn)
				return -ENOMEM;
			new_bn->bn_start = last + 1;
			new_bn->bn_last = old_last;
			xbitmap_tree_insert(new_bn, &bitmap->xb_root);
		} else if (bn->bn_start < start) {
			/* overlaps with the left side of the clearing range */
			xbitmap_tree_remove(bn, &bitmap->xb_root);
			bn->bn_last = start - 1;
			xbitmap_tree_insert(bn, &bitmap->xb_root);
		} else if (bn->bn_last > last) {
			/* overlaps with the right side of the clearing range */
			xbitmap_tree_remove(bn, &bitmap->xb_root);
			bn->bn_start = last + 1;
			xbitmap_tree_insert(bn, &bitmap->xb_root);
			break;
		} else {
			/* in the middle of the clearing range */
			xbitmap_tree_remove(bn, &bitmap->xb_root);
			kfree(bn);
		}
	}

	return 0;
}

/* Set a range of this bitmap. */
int
xbitmap_set(
	struct xbitmap		*bitmap,
	uint64_t		start,
	uint64_t		len)
{
	struct xbitmap_node	*left;
	struct xbitmap_node	*right;
	uint64_t		last = start + len - 1;
	int			error;

	/* Is this whole range already set? */
	left = xbitmap_tree_iter_first(&bitmap->xb_root, start, last);
	if (left && left->bn_start <= start && left->bn_last >= last)
		return 0;

	/* Clear out everything in the range we want to set. */
	error = xbitmap_clear(bitmap, start, len);
	if (error)
		return error;

	/* Do we have a left-adjacent extent? */
	left = xbitmap_tree_iter_first(&bitmap->xb_root, start - 1, start - 1);
	ASSERT(!left || left->bn_last + 1 == start);

	/* Do we have a right-adjacent extent? */
	right = xbitmap_tree_iter_first(&bitmap->xb_root, last + 1, last + 1);
	ASSERT(!right || right->bn_start == last + 1);

	if (left && right) {
		/* combine left and right adjacent extent */
		xbitmap_tree_remove(left, &bitmap->xb_root);
		xbitmap_tree_remove(right, &bitmap->xb_root);
		left->bn_last = right->bn_last;
		xbitmap_tree_insert(left, &bitmap->xb_root);
		kfree(right);
	} else if (left) {
		/* combine with left extent */
		xbitmap_tree_remove(left, &bitmap->xb_root);
		left->bn_last = last;
		xbitmap_tree_insert(left, &bitmap->xb_root);
	} else if (right) {
		/* combine with right extent */
		xbitmap_tree_remove(right, &bitmap->xb_root);
		right->bn_start = start;
		xbitmap_tree_insert(right, &bitmap->xb_root);
	} else {
		/* add an extent */
		left = kmalloc(sizeof(struct xbitmap_node), XCHK_GFP_FLAGS);
		if (!left)
			return -ENOMEM;
		left->bn_start = start;
		left->bn_last = last;
		xbitmap_tree_insert(left, &bitmap->xb_root);
	}

	return 0;
}

/* Free everything related to this bitmap. */
void
xbitmap_destroy(
	struct xbitmap		*bitmap)
{
	struct xbitmap_node	*bn;

	while ((bn = xbitmap_tree_iter_first(&bitmap->xb_root, 0, -1ULL))) {
		xbitmap_tree_remove(bn, &bitmap->xb_root);
		kfree(bn);
	}
}

/* Set up a per-AG block bitmap. */
void
xbitmap_init(
	struct xbitmap		*bitmap)
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
xbitmap_disunion(
	struct xbitmap		*bitmap,
	struct xbitmap		*sub)
{
	struct xbitmap_node	*bn;
	int			error;

	if (xbitmap_empty(bitmap) || xbitmap_empty(sub))
		return 0;

	for_each_xbitmap_extent(bn, sub) {
		error = xbitmap_clear(bitmap, bn->bn_start,
				bn->bn_last - bn->bn_start + 1);
		if (error)
			return error;
	}

	return 0;
}

/*
 * Record all btree blocks seen while iterating all records of a btree.
 *
 * We know that the btree query_all function starts at the left edge and walks
 * towards the right edge of the tree.  Therefore, we know that we can walk up
 * the btree cursor towards the root; if the pointer for a given level points
 * to the first record/key in that block, we haven't seen this block before;
 * and therefore we need to remember that we saw this block in the btree.
 *
 * So if our btree is:
 *
 *    4
 *  / | \
 * 1  2  3
 *
 * Pretend for this example that each leaf block has 100 btree records.  For
 * the first btree record, we'll observe that bc_levels[0].ptr == 1, so we
 * record that we saw block 1.  Then we observe that bc_levels[1].ptr == 1, so
 * we record block 4.  The list is [1, 4].
 *
 * For the second btree record, we see that bc_levels[0].ptr == 2, so we exit
 * the loop.  The list remains [1, 4].
 *
 * For the 101st btree record, we've moved onto leaf block 2.  Now
 * bc_levels[0].ptr == 1 again, so we record that we saw block 2.  We see that
 * bc_levels[1].ptr == 2, so we exit the loop.  The list is now [1, 4, 2].
 *
 * For the 102nd record, bc_levels[0].ptr == 2, so we continue.
 *
 * For the 201st record, we've moved on to leaf block 3.
 * bc_levels[0].ptr == 1, so we add 3 to the list.  Now it is [1, 4, 2, 3].
 *
 * For the 300th record we just exit, with the list being [1, 4, 2, 3].
 */

/* Mark a btree block to the agblock bitmap. */
STATIC int
xagb_bitmap_visit_btblock(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*priv)
{
	struct xagb_bitmap	*bitmap = priv;
	struct xfs_buf		*bp;
	xfs_fsblock_t		fsbno;
	xfs_agblock_t		agbno;

	xfs_btree_get_block(cur, level, &bp);
	if (!bp)
		return 0;

	fsbno = XFS_DADDR_TO_FSB(cur->bc_mp, xfs_buf_daddr(bp));
	agbno = XFS_FSB_TO_AGBNO(cur->bc_mp, fsbno);

	return xagb_bitmap_set(bitmap, agbno, 1);
}

/* Mark all (per-AG) btree blocks in the agblock bitmap. */
int
xagb_bitmap_set_btblocks(
	struct xagb_bitmap	*bitmap,
	struct xfs_btree_cur	*cur)
{
	return xfs_btree_visit_blocks(cur, xagb_bitmap_visit_btblock,
			XFS_BTREE_VISIT_ALL, bitmap);
}

/*
 * Record all the buffers pointed to by the btree cursor.  Callers already
 * engaged in a btree walk should call this function to capture the list of
 * blocks going from the leaf towards the root.
 */
int
xagb_bitmap_set_btcur_path(
	struct xagb_bitmap	*bitmap,
	struct xfs_btree_cur	*cur)
{
	int			i;
	int			error;

	for (i = 0; i < cur->bc_nlevels && cur->bc_levels[i].ptr == 1; i++) {
		error = xagb_bitmap_visit_btblock(cur, i, bitmap);
		if (error)
			return error;
	}

	return 0;
}

/* How many bits are set in this bitmap? */
uint64_t
xbitmap_hweight(
	struct xbitmap		*bitmap)
{
	struct xbitmap_node	*bn;
	uint64_t		ret = 0;

	for_each_xbitmap_extent(bn, bitmap)
		ret += bn->bn_last - bn->bn_start + 1;

	return ret;
}

/* Call a function for every run of set bits in this bitmap. */
int
xbitmap_walk(
	struct xbitmap		*bitmap,
	xbitmap_walk_fn		fn,
	void			*priv)
{
	struct xbitmap_node	*bn;
	int			error = 0;

	for_each_xbitmap_extent(bn, bitmap) {
		error = fn(bn->bn_start, bn->bn_last - bn->bn_start + 1, priv);
		if (error)
			break;
	}

	return error;
}

/* Does this bitmap have no bits set at all? */
bool
xbitmap_empty(
	struct xbitmap		*bitmap)
{
	return bitmap->xb_root.rb_root.rb_node == NULL;
}

/* Is the start of the range set or clear?  And for how long? */
bool
xbitmap_test(
	struct xbitmap		*bitmap,
	uint64_t		start,
	uint64_t		*len)
{
	struct xbitmap_node	*bn;
	uint64_t		last = start + *len - 1;

	bn = xbitmap_tree_iter_first(&bitmap->xb_root, start, last);
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
