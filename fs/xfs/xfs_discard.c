// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010, 2023 Red Hat, Inc.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_btree.h"
#include "xfs_alloc_btree.h"
#include "xfs_alloc.h"
#include "xfs_discard.h"
#include "xfs_error.h"
#include "xfs_extent_busy.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_ag.h"

struct workqueue_struct *xfs_discard_wq;

static void
xfs_discard_endio_work(
	struct work_struct	*work)
{
	struct xfs_busy_extents	*extents =
		container_of(work, struct xfs_busy_extents, endio_work);

	xfs_extent_busy_clear(extents->mount, &extents->extent_list, false);
	kmem_free(extents->owner);
}

/*
 * Queue up the actual completion to a thread to avoid IRQ-safe locking for
 * pagb_lock.
 */
static void
xfs_discard_endio(
	struct bio		*bio)
{
	struct xfs_busy_extents	*extents = bio->bi_private;

	INIT_WORK(&extents->endio_work, xfs_discard_endio_work);
	queue_work(xfs_discard_wq, &extents->endio_work);
	bio_put(bio);
}

/*
 * Walk the discard list and issue discards on all the busy extents in the
 * list. We plug and chain the bios so that we only need a single completion
 * call to clear all the busy extents once the discards are complete.
 */
int
xfs_discard_extents(
	struct xfs_mount	*mp,
	struct xfs_busy_extents	*extents)
{
	struct xfs_extent_busy	*busyp;
	struct bio		*bio = NULL;
	struct blk_plug		plug;
	int			error = 0;

	blk_start_plug(&plug);
	list_for_each_entry(busyp, &extents->extent_list, list) {
		trace_xfs_discard_extent(mp, busyp->agno, busyp->bno,
					 busyp->length);

		error = __blkdev_issue_discard(mp->m_ddev_targp->bt_bdev,
				XFS_AGB_TO_DADDR(mp, busyp->agno, busyp->bno),
				XFS_FSB_TO_BB(mp, busyp->length),
				GFP_NOFS, &bio);
		if (error && error != -EOPNOTSUPP) {
			xfs_info(mp,
	 "discard failed for extent [0x%llx,%u], error %d",
				 (unsigned long long)busyp->bno,
				 busyp->length,
				 error);
			break;
		}
	}

	if (bio) {
		bio->bi_private = extents;
		bio->bi_end_io = xfs_discard_endio;
		submit_bio(bio);
	} else {
		xfs_discard_endio_work(&extents->endio_work);
	}
	blk_finish_plug(&plug);

	return error;
}


STATIC int
xfs_trim_extents(
	struct xfs_perag	*pag,
	xfs_daddr_t		start,
	xfs_daddr_t		end,
	xfs_daddr_t		minlen,
	uint64_t		*blocks_trimmed)
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct block_device	*bdev = mp->m_ddev_targp->bt_bdev;
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agbp;
	struct xfs_agf		*agf;
	int			error;
	int			i;

	/*
	 * Force out the log.  This means any transactions that might have freed
	 * space before we take the AGF buffer lock are now on disk, and the
	 * volatile disk cache is flushed.
	 */
	xfs_log_force(mp, XFS_LOG_SYNC);

	error = xfs_alloc_read_agf(pag, NULL, 0, &agbp);
	if (error)
		return error;
	agf = agbp->b_addr;

	cur = xfs_allocbt_init_cursor(mp, NULL, agbp, pag, XFS_BTNUM_CNT);

	/*
	 * Look up the longest btree in the AGF and start with it.
	 */
	error = xfs_alloc_lookup_ge(cur, 0, be32_to_cpu(agf->agf_longest), &i);
	if (error)
		goto out_del_cursor;

	/*
	 * Loop until we are done with all extents that are large
	 * enough to be worth discarding.
	 */
	while (i) {
		xfs_agblock_t	fbno;
		xfs_extlen_t	flen;
		xfs_daddr_t	dbno;
		xfs_extlen_t	dlen;

		error = xfs_alloc_get_rec(cur, &fbno, &flen, &i);
		if (error)
			break;
		if (XFS_IS_CORRUPT(mp, i != 1)) {
			error = -EFSCORRUPTED;
			break;
		}
		ASSERT(flen <= be32_to_cpu(agf->agf_longest));

		/*
		 * use daddr format for all range/len calculations as that is
		 * the format the range/len variables are supplied in by
		 * userspace.
		 */
		dbno = XFS_AGB_TO_DADDR(mp, pag->pag_agno, fbno);
		dlen = XFS_FSB_TO_BB(mp, flen);

		/*
		 * Too small?  Give up.
		 */
		if (dlen < minlen) {
			trace_xfs_discard_toosmall(mp, pag->pag_agno, fbno, flen);
			break;
		}

		/*
		 * If the extent is entirely outside of the range we are
		 * supposed to discard skip it.  Do not bother to trim
		 * down partially overlapping ranges for now.
		 */
		if (dbno + dlen < start || dbno > end) {
			trace_xfs_discard_exclude(mp, pag->pag_agno, fbno, flen);
			goto next_extent;
		}

		/*
		 * If any blocks in the range are still busy, skip the
		 * discard and try again the next time.
		 */
		if (xfs_extent_busy_search(mp, pag, fbno, flen)) {
			trace_xfs_discard_busy(mp, pag->pag_agno, fbno, flen);
			goto next_extent;
		}

		trace_xfs_discard_extent(mp, pag->pag_agno, fbno, flen);
		error = blkdev_issue_discard(bdev, dbno, dlen, GFP_NOFS);
		if (error)
			break;
		*blocks_trimmed += flen;

next_extent:
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			break;

		if (fatal_signal_pending(current)) {
			error = -ERESTARTSYS;
			break;
		}
	}

out_del_cursor:
	xfs_btree_del_cursor(cur, error);
	xfs_buf_relse(agbp);
	return error;
}

/*
 * trim a range of the filesystem.
 *
 * Note: the parameters passed from userspace are byte ranges into the
 * filesystem which does not match to the format we use for filesystem block
 * addressing. FSB addressing is sparse (AGNO|AGBNO), while the incoming format
 * is a linear address range. Hence we need to use DADDR based conversions and
 * comparisons for determining the correct offset and regions to trim.
 */
int
xfs_ioc_trim(
	struct xfs_mount		*mp,
	struct fstrim_range __user	*urange)
{
	struct xfs_perag	*pag;
	unsigned int		granularity =
		bdev_discard_granularity(mp->m_ddev_targp->bt_bdev);
	struct fstrim_range	range;
	xfs_daddr_t		start, end, minlen;
	xfs_agnumber_t		agno;
	uint64_t		blocks_trimmed = 0;
	int			error, last_error = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (!bdev_max_discard_sectors(mp->m_ddev_targp->bt_bdev))
		return -EOPNOTSUPP;

	/*
	 * We haven't recovered the log, so we cannot use our bnobt-guided
	 * storage zapping commands.
	 */
	if (xfs_has_norecovery(mp))
		return -EROFS;

	if (copy_from_user(&range, urange, sizeof(range)))
		return -EFAULT;

	range.minlen = max_t(u64, granularity, range.minlen);
	minlen = BTOBB(range.minlen);
	/*
	 * Truncating down the len isn't actually quite correct, but using
	 * BBTOB would mean we trivially get overflows for values
	 * of ULLONG_MAX or slightly lower.  And ULLONG_MAX is the default
	 * used by the fstrim application.  In the end it really doesn't
	 * matter as trimming blocks is an advisory interface.
	 */
	if (range.start >= XFS_FSB_TO_B(mp, mp->m_sb.sb_dblocks) ||
	    range.minlen > XFS_FSB_TO_B(mp, mp->m_ag_max_usable) ||
	    range.len < mp->m_sb.sb_blocksize)
		return -EINVAL;

	start = BTOBB(range.start);
	end = start + BTOBBT(range.len) - 1;

	if (end > XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks) - 1)
		end = XFS_FSB_TO_BB(mp, mp->m_sb.sb_dblocks) - 1;

	agno = xfs_daddr_to_agno(mp, start);
	for_each_perag_range(mp, agno, xfs_daddr_to_agno(mp, end), pag) {
		error = xfs_trim_extents(pag, start, end, minlen,
					  &blocks_trimmed);
		if (error) {
			last_error = error;
			if (error == -ERESTARTSYS) {
				xfs_perag_rele(pag);
				break;
			}
		}
	}

	if (last_error)
		return last_error;

	range.len = XFS_FSB_TO_B(mp, blocks_trimmed);
	if (copy_to_user(urange, &range, sizeof(range)))
		return -EFAULT;
	return 0;
}
