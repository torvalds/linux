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

/* Collect a dead btree extent for later disposal. */
int
xrep_collect_btree_extent(
	struct xfs_scrub	*sc,
	struct xrep_extent_list	*exlist,
	xfs_fsblock_t		fsbno,
	xfs_extlen_t		len)
{
	struct xrep_extent	*rex;

	trace_xrep_collect_btree_extent(sc->mp,
			XFS_FSB_TO_AGNO(sc->mp, fsbno),
			XFS_FSB_TO_AGBNO(sc->mp, fsbno), len);

	rex = kmem_alloc(sizeof(struct xrep_extent), KM_MAYFAIL);
	if (!rex)
		return -ENOMEM;

	INIT_LIST_HEAD(&rex->list);
	rex->fsbno = fsbno;
	rex->len = len;
	list_add_tail(&rex->list, &exlist->list);

	return 0;
}

/*
 * An error happened during the rebuild so the transaction will be cancelled.
 * The fs will shut down, and the administrator has to unmount and run repair.
 * Therefore, free all the memory associated with the list so we can die.
 */
void
xrep_cancel_btree_extents(
	struct xfs_scrub	*sc,
	struct xrep_extent_list	*exlist)
{
	struct xrep_extent	*rex;
	struct xrep_extent	*n;

	for_each_xrep_extent_safe(rex, n, exlist) {
		list_del(&rex->list);
		kmem_free(rex);
	}
}

/* Compare two btree extents. */
static int
xrep_btree_extent_cmp(
	void			*priv,
	struct list_head	*a,
	struct list_head	*b)
{
	struct xrep_extent	*ap;
	struct xrep_extent	*bp;

	ap = container_of(a, struct xrep_extent, list);
	bp = container_of(b, struct xrep_extent, list);

	if (ap->fsbno > bp->fsbno)
		return 1;
	if (ap->fsbno < bp->fsbno)
		return -1;
	return 0;
}

/*
 * Remove all the blocks mentioned in @sublist from the extents in @exlist.
 *
 * The intent is that callers will iterate the rmapbt for all of its records
 * for a given owner to generate @exlist; and iterate all the blocks of the
 * metadata structures that are not being rebuilt and have the same rmapbt
 * owner to generate @sublist.  This routine subtracts all the extents
 * mentioned in sublist from all the extents linked in @exlist, which leaves
 * @exlist as the list of blocks that are not accounted for, which we assume
 * are the dead blocks of the old metadata structure.  The blocks mentioned in
 * @exlist can be reaped.
 */
#define LEFT_ALIGNED	(1 << 0)
#define RIGHT_ALIGNED	(1 << 1)
int
xrep_subtract_extents(
	struct xfs_scrub	*sc,
	struct xrep_extent_list	*exlist,
	struct xrep_extent_list	*sublist)
{
	struct list_head	*lp;
	struct xrep_extent	*ex;
	struct xrep_extent	*newex;
	struct xrep_extent	*subex;
	xfs_fsblock_t		sub_fsb;
	xfs_extlen_t		sub_len;
	int			state;
	int			error = 0;

	if (list_empty(&exlist->list) || list_empty(&sublist->list))
		return 0;
	ASSERT(!list_empty(&sublist->list));

	list_sort(NULL, &exlist->list, xrep_btree_extent_cmp);
	list_sort(NULL, &sublist->list, xrep_btree_extent_cmp);

	/*
	 * Now that we've sorted both lists, we iterate exlist once, rolling
	 * forward through sublist and/or exlist as necessary until we find an
	 * overlap or reach the end of either list.  We do not reset lp to the
	 * head of exlist nor do we reset subex to the head of sublist.  The
	 * list traversal is similar to merge sort, but we're deleting
	 * instead.  In this manner we avoid O(n^2) operations.
	 */
	subex = list_first_entry(&sublist->list, struct xrep_extent,
			list);
	lp = exlist->list.next;
	while (lp != &exlist->list) {
		ex = list_entry(lp, struct xrep_extent, list);

		/*
		 * Advance subex and/or ex until we find a pair that
		 * intersect or we run out of extents.
		 */
		while (subex->fsbno + subex->len <= ex->fsbno) {
			if (list_is_last(&subex->list, &sublist->list))
				goto out;
			subex = list_next_entry(subex, list);
		}
		if (subex->fsbno >= ex->fsbno + ex->len) {
			lp = lp->next;
			continue;
		}

		/* trim subex to fit the extent we have */
		sub_fsb = subex->fsbno;
		sub_len = subex->len;
		if (subex->fsbno < ex->fsbno) {
			sub_len -= ex->fsbno - subex->fsbno;
			sub_fsb = ex->fsbno;
		}
		if (sub_len > ex->len)
			sub_len = ex->len;

		state = 0;
		if (sub_fsb == ex->fsbno)
			state |= LEFT_ALIGNED;
		if (sub_fsb + sub_len == ex->fsbno + ex->len)
			state |= RIGHT_ALIGNED;
		switch (state) {
		case LEFT_ALIGNED:
			/* Coincides with only the left. */
			ex->fsbno += sub_len;
			ex->len -= sub_len;
			break;
		case RIGHT_ALIGNED:
			/* Coincides with only the right. */
			ex->len -= sub_len;
			lp = lp->next;
			break;
		case LEFT_ALIGNED | RIGHT_ALIGNED:
			/* Total overlap, just delete ex. */
			lp = lp->next;
			list_del(&ex->list);
			kmem_free(ex);
			break;
		case 0:
			/*
			 * Deleting from the middle: add the new right extent
			 * and then shrink the left extent.
			 */
			newex = kmem_alloc(sizeof(struct xrep_extent),
					KM_MAYFAIL);
			if (!newex) {
				error = -ENOMEM;
				goto out;
			}
			INIT_LIST_HEAD(&newex->list);
			newex->fsbno = sub_fsb + sub_len;
			newex->len = ex->fsbno + ex->len - newex->fsbno;
			list_add(&newex->list, &ex->list);
			ex->len = sub_fsb - ex->fsbno;
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
