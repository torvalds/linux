/*
 * Copyright (C) 2010 Red Hat, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include "xfs.h"
#include "xfs_sb.h"
#include "xfs_inum.h"
#include "xfs_log.h"
#include "xfs_ag.h"
#include "xfs_mount.h"
#include "xfs_quota.h"
#include "xfs_trans.h"
#include "xfs_alloc_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_ialloc_btree.h"
#include "xfs_btree.h"
#include "xfs_inode.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_discard.h"
#include "xfs_trace.h"

STATIC int
xfs_trim_extents(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno,
	xfs_fsblock_t		start,
	xfs_fsblock_t		end,
	xfs_fsblock_t		minlen,
	__uint64_t		*blocks_trimmed)
{
	struct block_device	*bdev = mp->m_ddev_targp->bt_bdev;
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agbp;
	struct xfs_perag	*pag;
	int			error;
	int			i;

	pag = xfs_perag_get(mp, agno);

	error = xfs_alloc_read_agf(mp, NULL, agno, 0, &agbp);
	if (error || !agbp)
		goto out_put_perag;

	cur = xfs_allocbt_init_cursor(mp, NULL, agbp, agno, XFS_BTNUM_CNT);

	/*
	 * Force out the log.  This means any transactions that might have freed
	 * space before we took the AGF buffer lock are now on disk, and the
	 * volatile disk cache is flushed.
	 */
	xfs_log_force(mp, XFS_LOG_SYNC);

	/*
	 * Look up the longest btree in the AGF and start with it.
	 */
	error = xfs_alloc_lookup_le(cur, 0,
				    XFS_BUF_TO_AGF(agbp)->agf_longest, &i);
	if (error)
		goto out_del_cursor;

	/*
	 * Loop until we are done with all extents that are large
	 * enough to be worth discarding.
	 */
	while (i) {
		xfs_agblock_t fbno;
		xfs_extlen_t flen;

		error = xfs_alloc_get_rec(cur, &fbno, &flen, &i);
		if (error)
			goto out_del_cursor;
		XFS_WANT_CORRUPTED_GOTO(i == 1, out_del_cursor);
		ASSERT(flen <= XFS_BUF_TO_AGF(agbp)->agf_longest);

		/*
		 * Too small?  Give up.
		 */
		if (flen < minlen) {
			trace_xfs_discard_toosmall(mp, agno, fbno, flen);
			goto out_del_cursor;
		}

		/*
		 * If the extent is entirely outside of the range we are
		 * supposed to discard skip it.  Do not bother to trim
		 * down partially overlapping ranges for now.
		 */
		if (XFS_AGB_TO_FSB(mp, agno, fbno) + flen < start ||
		    XFS_AGB_TO_FSB(mp, agno, fbno) > end) {
			trace_xfs_discard_exclude(mp, agno, fbno, flen);
			goto next_extent;
		}

		/*
		 * If any blocks in the range are still busy, skip the
		 * discard and try again the next time.
		 */
		if (xfs_alloc_busy_search(mp, agno, fbno, flen)) {
			trace_xfs_discard_busy(mp, agno, fbno, flen);
			goto next_extent;
		}

		trace_xfs_discard_extent(mp, agno, fbno, flen);
		error = -blkdev_issue_discard(bdev,
				XFS_AGB_TO_DADDR(mp, agno, fbno),
				XFS_FSB_TO_BB(mp, flen),
				GFP_NOFS, 0);
		if (error)
			goto out_del_cursor;
		*blocks_trimmed += flen;

next_extent:
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			goto out_del_cursor;
	}

out_del_cursor:
	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);
	xfs_buf_relse(agbp);
out_put_perag:
	xfs_perag_put(pag);
	return error;
}

int
xfs_ioc_trim(
	struct xfs_mount		*mp,
	struct fstrim_range __user	*urange)
{
	struct request_queue	*q = mp->m_ddev_targp->bt_bdev->bd_disk->queue;
	unsigned int		granularity = q->limits.discard_granularity;
	struct fstrim_range	range;
	xfs_fsblock_t		start, end, minlen;
	xfs_agnumber_t		start_agno, end_agno, agno;
	__uint64_t		blocks_trimmed = 0;
	int			error, last_error = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -XFS_ERROR(EPERM);
	if (!blk_queue_discard(q))
		return -XFS_ERROR(EOPNOTSUPP);
	if (copy_from_user(&range, urange, sizeof(range)))
		return -XFS_ERROR(EFAULT);

	/*
	 * Truncating down the len isn't actually quite correct, but using
	 * XFS_B_TO_FSB would mean we trivially get overflows for values
	 * of ULLONG_MAX or slightly lower.  And ULLONG_MAX is the default
	 * used by the fstrim application.  In the end it really doesn't
	 * matter as trimming blocks is an advisory interface.
	 */
	start = XFS_B_TO_FSBT(mp, range.start);
	end = start + XFS_B_TO_FSBT(mp, range.len) - 1;
	minlen = XFS_B_TO_FSB(mp, max_t(u64, granularity, range.minlen));

	if (start >= mp->m_sb.sb_dblocks)
		return -XFS_ERROR(EINVAL);
	if (end > mp->m_sb.sb_dblocks - 1)
		end = mp->m_sb.sb_dblocks - 1;

	start_agno = XFS_FSB_TO_AGNO(mp, start);
	end_agno = XFS_FSB_TO_AGNO(mp, end);

	for (agno = start_agno; agno <= end_agno; agno++) {
		error = -xfs_trim_extents(mp, agno, start, end, minlen,
					  &blocks_trimmed);
		if (error)
			last_error = error;
	}

	if (last_error)
		return last_error;

	range.len = XFS_FSB_TO_B(mp, blocks_trimmed);
	if (copy_to_user(urange, &range, sizeof(range)))
		return -XFS_ERROR(EFAULT);
	return 0;
}

int
xfs_discard_extents(
	struct xfs_mount	*mp,
	struct list_head	*list)
{
	struct xfs_busy_extent	*busyp;
	int			error = 0;

	list_for_each_entry(busyp, list, list) {
		trace_xfs_discard_extent(mp, busyp->agno, busyp->bno,
					 busyp->length);

		error = -blkdev_issue_discard(mp->m_ddev_targp->bt_bdev,
				XFS_AGB_TO_DADDR(mp, busyp->agno, busyp->bno),
				XFS_FSB_TO_BB(mp, busyp->length),
				GFP_NOFS, 0);
		if (error && error != EOPNOTSUPP) {
			xfs_info(mp,
	 "discard failed for extent [0x%llu,%u], error %d",
				 (unsigned long long)busyp->bno,
				 busyp->length,
				 error);
			return error;
		}
	}

	return 0;
}
