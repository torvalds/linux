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

/*
 * Notes on an efficient, low latency fstrim algorithm
 *
 * We need to walk the filesystem free space and issue discards on the free
 * space that meet the search criteria (size and location). We cannot issue
 * discards on extents that might be in use, or are so recently in use they are
 * still marked as busy. To serialise against extent state changes whilst we are
 * gathering extents to trim, we must hold the AGF lock to lock out other
 * allocations and extent free operations that might change extent state.
 *
 * However, we cannot just hold the AGF for the entire AG free space walk whilst
 * we issue discards on each free space that is found. Storage devices can have
 * extremely slow discard implementations (e.g. ceph RBD) and so walking a
 * couple of million free extents and issuing synchronous discards on each
 * extent can take a *long* time. Whilst we are doing this walk, nothing else
 * can access the AGF, and we can stall transactions and hence the log whilst
 * modifications wait for the AGF lock to be released. This can lead hung tasks
 * kicking the hung task timer and rebooting the system. This is bad.
 *
 * Hence we need to take a leaf from the bulkstat playbook. It takes the AGI
 * lock, gathers a range of inode cluster buffers that are allocated, drops the
 * AGI lock and then reads all the inode cluster buffers and processes them. It
 * loops doing this, using a cursor to keep track of where it is up to in the AG
 * for each iteration to restart the INOBT lookup from.
 *
 * We can't do this exactly with free space - once we drop the AGF lock, the
 * state of the free extent is out of our control and we cannot run a discard
 * safely on it in this situation. Unless, of course, we've marked the free
 * extent as busy and undergoing a discard operation whilst we held the AGF
 * locked.
 *
 * This is exactly how online discard works - free extents are marked busy when
 * they are freed, and once the extent free has been committed to the journal,
 * the busy extent record is marked as "undergoing discard" and the discard is
 * then issued on the free extent. Once the discard completes, the busy extent
 * record is removed and the extent is able to be allocated again.
 *
 * In the context of fstrim, if we find a free extent we need to discard, we
 * don't have to discard it immediately. All we need to do it record that free
 * extent as being busy and under discard, and all the allocation routines will
 * now avoid trying to allocate it. Hence if we mark the extent as busy under
 * the AGF lock, we can safely discard it without holding the AGF lock because
 * nothing will attempt to allocate that free space until the discard completes.
 *
 * This also allows us to issue discards asynchronously like we do with online
 * discard, and so for fast devices fstrim will run much faster as we can have
 * multiple discard operations in flight at once, as well as pipeline the free
 * extent search so that it overlaps in flight discard IO.
 */

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


static int
xfs_trim_gather_extents(
	struct xfs_perag	*pag,
	xfs_daddr_t		start,
	xfs_daddr_t		end,
	xfs_daddr_t		minlen,
	struct xfs_alloc_rec_incore *tcur,
	struct xfs_busy_extents	*extents,
	uint64_t		*blocks_trimmed)
{
	struct xfs_mount	*mp = pag->pag_mount;
	struct xfs_btree_cur	*cur;
	struct xfs_buf		*agbp;
	int			error;
	int			i;
	int			batch = 100;

	/*
	 * Force out the log.  This means any transactions that might have freed
	 * space before we take the AGF buffer lock are now on disk, and the
	 * volatile disk cache is flushed.
	 */
	xfs_log_force(mp, XFS_LOG_SYNC);

	error = xfs_alloc_read_agf(pag, NULL, 0, &agbp);
	if (error)
		return error;

	cur = xfs_allocbt_init_cursor(mp, NULL, agbp, pag, XFS_BTNUM_CNT);

	/*
	 * Look up the extent length requested in the AGF and start with it.
	 */
	if (tcur->ar_startblock == NULLAGBLOCK)
		error = xfs_alloc_lookup_ge(cur, 0, tcur->ar_blockcount, &i);
	else
		error = xfs_alloc_lookup_le(cur, tcur->ar_startblock,
				tcur->ar_blockcount, &i);
	if (error)
		goto out_del_cursor;
	if (i == 0) {
		/* nothing of that length left in the AG, we are done */
		tcur->ar_blockcount = 0;
		goto out_del_cursor;
	}

	/*
	 * Loop until we are done with all extents that are large
	 * enough to be worth discarding or we hit batch limits.
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

		if (--batch <= 0) {
			/*
			 * Update the cursor to point at this extent so we
			 * restart the next batch from this extent.
			 */
			tcur->ar_startblock = fbno;
			tcur->ar_blockcount = flen;
			break;
		}

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
			tcur->ar_blockcount = 0;
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

		xfs_extent_busy_insert_discard(pag, fbno, flen,
				&extents->extent_list);
		*blocks_trimmed += flen;
next_extent:
		error = xfs_btree_decrement(cur, 0, &i);
		if (error)
			break;

		/*
		 * If there's no more records in the tree, we are done. Set the
		 * cursor block count to 0 to indicate to the caller that there
		 * is no more extents to search.
		 */
		if (i == 0)
			tcur->ar_blockcount = 0;
	}

	/*
	 * If there was an error, release all the gathered busy extents because
	 * we aren't going to issue a discard on them any more.
	 */
	if (error)
		xfs_extent_busy_clear(mp, &extents->extent_list, false);
out_del_cursor:
	xfs_btree_del_cursor(cur, error);
	xfs_buf_relse(agbp);
	return error;
}

static bool
xfs_trim_should_stop(void)
{
	return fatal_signal_pending(current) || freezing(current);
}

/*
 * Iterate the free list gathering extents and discarding them. We need a cursor
 * for the repeated iteration of gather/discard loop, so use the longest extent
 * we found in the last batch as the key to start the next.
 */
static int
xfs_trim_extents(
	struct xfs_perag	*pag,
	xfs_daddr_t		start,
	xfs_daddr_t		end,
	xfs_daddr_t		minlen,
	uint64_t		*blocks_trimmed)
{
	struct xfs_alloc_rec_incore tcur = {
		.ar_blockcount = pag->pagf_longest,
		.ar_startblock = NULLAGBLOCK,
	};
	int			error = 0;

	do {
		struct xfs_busy_extents	*extents;

		extents = kzalloc(sizeof(*extents), GFP_KERNEL);
		if (!extents) {
			error = -ENOMEM;
			break;
		}

		extents->mount = pag->pag_mount;
		extents->owner = extents;
		INIT_LIST_HEAD(&extents->extent_list);

		error = xfs_trim_gather_extents(pag, start, end, minlen,
				&tcur, extents, blocks_trimmed);
		if (error) {
			kfree(extents);
			break;
		}

		/*
		 * We hand the extent list to the discard function here so the
		 * discarded extents can be removed from the busy extent list.
		 * This allows the discards to run asynchronously with gathering
		 * the next round of extents to discard.
		 *
		 * However, we must ensure that we do not reference the extent
		 * list  after this function call, as it may have been freed by
		 * the time control returns to us.
		 */
		error = xfs_discard_extents(pag->pag_mount, extents);
		if (error)
			break;

		if (xfs_trim_should_stop())
			break;

	} while (tcur.ar_blockcount != 0);

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
		if (error)
			last_error = error;

		if (xfs_trim_should_stop()) {
			xfs_perag_rele(pag);
			break;
		}
	}

	if (last_error)
		return last_error;

	range.len = XFS_FSB_TO_B(mp, blocks_trimmed);
	if (copy_to_user(urange, &range, sizeof(range)))
		return -EFAULT;
	return 0;
}
