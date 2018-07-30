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
#include "scrub/xfs_scrub.h"
#include "scrub/scrub.h"
#include "scrub/common.h"
#include "scrub/trace.h"
#include "scrub/repair.h"
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
