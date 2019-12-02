// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "scrub/bitmap.h"

/*
 * Set a range of this bitmap.  Caller must ensure the range is not set.
 *
 * This is the logical equivalent of bitmap |= mask(start, len).
 */
int
xfs_bitmap_set(
	struct xfs_bitmap	*bitmap,
	uint64_t		start,
	uint64_t		len)
{
	struct xfs_bitmap_range	*bmr;

	bmr = kmem_alloc(sizeof(struct xfs_bitmap_range), KM_MAYFAIL);
	if (!bmr)
		return -ENOMEM;

	INIT_LIST_HEAD(&bmr->list);
	bmr->start = start;
	bmr->len = len;
	list_add_tail(&bmr->list, &bitmap->list);

	return 0;
}

/* Free everything related to this bitmap. */
void
xfs_bitmap_destroy(
	struct xfs_bitmap	*bitmap)
{
	struct xfs_bitmap_range	*bmr;
	struct xfs_bitmap_range	*n;

	for_each_xfs_bitmap_extent(bmr, n, bitmap) {
		list_del(&bmr->list);
		kmem_free(bmr);
	}
}

/* Set up a per-AG block bitmap. */
void
xfs_bitmap_init(
	struct xfs_bitmap	*bitmap)
{
	INIT_LIST_HEAD(&bitmap->list);
}

/* Compare two btree extents. */
static int
xfs_bitmap_range_cmp(
	void			*priv,
	struct list_head	*a,
	struct list_head	*b)
{
	struct xfs_bitmap_range	*ap;
	struct xfs_bitmap_range	*bp;

	ap = container_of(a, struct xfs_bitmap_range, list);
	bp = container_of(b, struct xfs_bitmap_range, list);

	if (ap->start > bp->start)
		return 1;
	if (ap->start < bp->start)
		return -1;
	return 0;
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
#define LEFT_ALIGNED	(1 << 0)
#define RIGHT_ALIGNED	(1 << 1)
int
xfs_bitmap_disunion(
	struct xfs_bitmap	*bitmap,
	struct xfs_bitmap	*sub)
{
	struct list_head	*lp;
	struct xfs_bitmap_range	*br;
	struct xfs_bitmap_range	*new_br;
	struct xfs_bitmap_range	*sub_br;
	uint64_t		sub_start;
	uint64_t		sub_len;
	int			state;
	int			error = 0;

	if (list_empty(&bitmap->list) || list_empty(&sub->list))
		return 0;
	ASSERT(!list_empty(&sub->list));

	list_sort(NULL, &bitmap->list, xfs_bitmap_range_cmp);
	list_sort(NULL, &sub->list, xfs_bitmap_range_cmp);

	/*
	 * Now that we've sorted both lists, we iterate bitmap once, rolling
	 * forward through sub and/or bitmap as necessary until we find an
	 * overlap or reach the end of either list.  We do not reset lp to the
	 * head of bitmap nor do we reset sub_br to the head of sub.  The
	 * list traversal is similar to merge sort, but we're deleting
	 * instead.  In this manner we avoid O(n^2) operations.
	 */
	sub_br = list_first_entry(&sub->list, struct xfs_bitmap_range,
			list);
	lp = bitmap->list.next;
	while (lp != &bitmap->list) {
		br = list_entry(lp, struct xfs_bitmap_range, list);

		/*
		 * Advance sub_br and/or br until we find a pair that
		 * intersect or we run out of extents.
		 */
		while (sub_br->start + sub_br->len <= br->start) {
			if (list_is_last(&sub_br->list, &sub->list))
				goto out;
			sub_br = list_next_entry(sub_br, list);
		}
		if (sub_br->start >= br->start + br->len) {
			lp = lp->next;
			continue;
		}

		/* trim sub_br to fit the extent we have */
		sub_start = sub_br->start;
		sub_len = sub_br->len;
		if (sub_br->start < br->start) {
			sub_len -= br->start - sub_br->start;
			sub_start = br->start;
		}
		if (sub_len > br->len)
			sub_len = br->len;

		state = 0;
		if (sub_start == br->start)
			state |= LEFT_ALIGNED;
		if (sub_start + sub_len == br->start + br->len)
			state |= RIGHT_ALIGNED;
		switch (state) {
		case LEFT_ALIGNED:
			/* Coincides with only the left. */
			br->start += sub_len;
			br->len -= sub_len;
			break;
		case RIGHT_ALIGNED:
			/* Coincides with only the right. */
			br->len -= sub_len;
			lp = lp->next;
			break;
		case LEFT_ALIGNED | RIGHT_ALIGNED:
			/* Total overlap, just delete ex. */
			lp = lp->next;
			list_del(&br->list);
			kmem_free(br);
			break;
		case 0:
			/*
			 * Deleting from the middle: add the new right extent
			 * and then shrink the left extent.
			 */
			new_br = kmem_alloc(sizeof(struct xfs_bitmap_range),
					KM_MAYFAIL);
			if (!new_br) {
				error = -ENOMEM;
				goto out;
			}
			INIT_LIST_HEAD(&new_br->list);
			new_br->start = sub_start + sub_len;
			new_br->len = br->start + br->len - new_br->start;
			list_add(&new_br->list, &br->list);
			br->len = sub_start - br->start;
			lp = lp->next;
			break;
		default:
			ASSERT(0);
			break;
		}
	}

out:
	return error;
}
#undef LEFT_ALIGNED
#undef RIGHT_ALIGNED

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
 * the first btree record, we'll observe that bc_ptrs[0] == 1, so we record
 * that we saw block 1.  Then we observe that bc_ptrs[1] == 1, so we record
 * block 4.  The list is [1, 4].
 *
 * For the second btree record, we see that bc_ptrs[0] == 2, so we exit the
 * loop.  The list remains [1, 4].
 *
 * For the 101st btree record, we've moved onto leaf block 2.  Now
 * bc_ptrs[0] == 1 again, so we record that we saw block 2.  We see that
 * bc_ptrs[1] == 2, so we exit the loop.  The list is now [1, 4, 2].
 *
 * For the 102nd record, bc_ptrs[0] == 2, so we continue.
 *
 * For the 201st record, we've moved on to leaf block 3.  bc_ptrs[0] == 1, so
 * we add 3 to the list.  Now it is [1, 4, 2, 3].
 *
 * For the 300th record we just exit, with the list being [1, 4, 2, 3].
 */

/*
 * Record all the buffers pointed to by the btree cursor.  Callers already
 * engaged in a btree walk should call this function to capture the list of
 * blocks going from the leaf towards the root.
 */
int
xfs_bitmap_set_btcur_path(
	struct xfs_bitmap	*bitmap,
	struct xfs_btree_cur	*cur)
{
	struct xfs_buf		*bp;
	xfs_fsblock_t		fsb;
	int			i;
	int			error;

	for (i = 0; i < cur->bc_nlevels && cur->bc_ptrs[i] == 1; i++) {
		xfs_btree_get_block(cur, i, &bp);
		if (!bp)
			continue;
		fsb = XFS_DADDR_TO_FSB(cur->bc_mp, bp->b_bn);
		error = xfs_bitmap_set(bitmap, fsb, 1);
		if (error)
			return error;
	}

	return 0;
}

/* Collect a btree's block in the bitmap. */
STATIC int
xfs_bitmap_collect_btblock(
	struct xfs_btree_cur	*cur,
	int			level,
	void			*priv)
{
	struct xfs_bitmap	*bitmap = priv;
	struct xfs_buf		*bp;
	xfs_fsblock_t		fsbno;

	xfs_btree_get_block(cur, level, &bp);
	if (!bp)
		return 0;

	fsbno = XFS_DADDR_TO_FSB(cur->bc_mp, bp->b_bn);
	return xfs_bitmap_set(bitmap, fsbno, 1);
}

/* Walk the btree and mark the bitmap wherever a btree block is found. */
int
xfs_bitmap_set_btblocks(
	struct xfs_bitmap	*bitmap,
	struct xfs_btree_cur	*cur)
{
	return xfs_btree_visit_blocks(cur, xfs_bitmap_collect_btblock,
			XFS_BTREE_VISIT_ALL, bitmap);
}
