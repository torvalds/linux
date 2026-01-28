// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2026 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <djwong@kernel.org>
 */
#include "xfs_platform.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_bit.h"
#include "xfs_btree.h"
#include "xfs_inode.h"
#include "xfs_icache.h"
#include "xfs_trans.h"
#include "xfs_alloc.h"
#include "xfs_ag.h"
#include "xfs_rmap.h"
#include "xfs_rmap_btree.h"
#include "xfs_rtgroup.h"
#include "xfs_rtrmap_btree.h"
#include "xfs_health.h"
#include "xfs_healthmon.h"
#include "xfs_trace.h"
#include "xfs_verify_media.h"

#include <linux/fserror.h>

struct xfs_group_data_lost {
	xfs_agblock_t		startblock;
	xfs_extlen_t		blockcount;
};

/* Report lost file data from rmap records */
static int
xfs_verify_report_data_lost(
	struct xfs_btree_cur		*cur,
	const struct xfs_rmap_irec	*rec,
	void				*data)
{
	struct xfs_mount		*mp = cur->bc_mp;
	struct xfs_inode		*ip;
	struct xfs_group_data_lost	*lost = data;
	xfs_fileoff_t			fileoff = rec->rm_offset;
	xfs_extlen_t			blocks = rec->rm_blockcount;
	const bool			is_attr =
			(rec->rm_flags & XFS_RMAP_ATTR_FORK);
	const xfs_agblock_t		lost_end =
			lost->startblock + lost->blockcount;
	const xfs_agblock_t		rmap_end =
			rec->rm_startblock + rec->rm_blockcount;
	int				error = 0;

	if (XFS_RMAP_NON_INODE_OWNER(rec->rm_owner))
	       return 0;

	error = xfs_iget(mp, cur->bc_tp, rec->rm_owner, 0, 0, &ip);
	if (error)
		return 0;

	if (rec->rm_flags & XFS_RMAP_BMBT_BLOCK) {
		xfs_bmap_mark_sick(ip, is_attr ? XFS_ATTR_FORK : XFS_DATA_FORK);
		goto out_rele;
	}

	if (is_attr) {
		xfs_inode_mark_sick(ip, XFS_SICK_INO_XATTR);
		goto out_rele;
	}

	if (lost->startblock > rec->rm_startblock) {
		fileoff += lost->startblock - rec->rm_startblock;
		blocks -= lost->startblock - rec->rm_startblock;
	}
	if (rmap_end > lost_end)
		blocks -= rmap_end - lost_end;

	fserror_report_data_lost(VFS_I(ip), XFS_FSB_TO_B(mp, fileoff),
			XFS_FSB_TO_B(mp, blocks), GFP_NOFS);

out_rele:
	xfs_irele(ip);
	return 0;
}

/* Walk reverse mappings to look for all file data loss */
static int
xfs_verify_report_losses(
	struct xfs_mount	*mp,
	enum xfs_group_type	type,
	xfs_daddr_t		daddr,
	u64			bblen)
{
	struct xfs_group	*xg = NULL;
	struct xfs_trans	*tp;
	xfs_fsblock_t		start_bno, end_bno;
	uint32_t		start_gno, end_gno;
	int			error;

	if (type == XG_TYPE_RTG) {
		start_bno = xfs_daddr_to_rtb(mp, daddr);
		end_bno = xfs_daddr_to_rtb(mp, daddr + bblen - 1);
	} else {
		start_bno = XFS_DADDR_TO_FSB(mp, daddr);
		end_bno = XFS_DADDR_TO_FSB(mp, daddr + bblen - 1);
	}

	tp = xfs_trans_alloc_empty(mp);
	start_gno = xfs_fsb_to_gno(mp, start_bno, type);
	end_gno = xfs_fsb_to_gno(mp, end_bno, type);
	while ((xg = xfs_group_next_range(mp, xg, start_gno, end_gno, type))) {
		struct xfs_buf		*agf_bp = NULL;
		struct xfs_rtgroup	*rtg = NULL;
		struct xfs_btree_cur	*cur;
		struct xfs_rmap_irec	ri_low = { };
		struct xfs_rmap_irec	ri_high;
		struct xfs_group_data_lost lost;

		if (type == XG_TYPE_AG) {
			struct xfs_perag	*pag = to_perag(xg);

			error = xfs_alloc_read_agf(pag, tp, 0, &agf_bp);
			if (error) {
				xfs_perag_put(pag);
				break;
			}

			cur = xfs_rmapbt_init_cursor(mp, tp, agf_bp, pag);
		} else {
			rtg = to_rtg(xg);
			xfs_rtgroup_lock(rtg, XFS_RTGLOCK_RMAP);
			cur = xfs_rtrmapbt_init_cursor(tp, rtg);
		}

		/*
		 * Set the rmap range from ri_low to ri_high, which represents
		 * a [start, end] where we looking for the files or metadata.
		 */
		memset(&ri_high, 0xFF, sizeof(ri_high));
		if (xg->xg_gno == start_gno)
			ri_low.rm_startblock =
				xfs_fsb_to_gbno(mp, start_bno, type);
		if (xg->xg_gno == end_gno)
			ri_high.rm_startblock =
				xfs_fsb_to_gbno(mp, end_bno, type);

		lost.startblock = ri_low.rm_startblock;
		lost.blockcount = min(xg->xg_block_count,
				      ri_high.rm_startblock + 1) -
							ri_low.rm_startblock;

		error = xfs_rmap_query_range(cur, &ri_low, &ri_high,
				xfs_verify_report_data_lost, &lost);
		xfs_btree_del_cursor(cur, error);
		if (agf_bp)
			xfs_trans_brelse(tp, agf_bp);
		if (rtg)
			xfs_rtgroup_unlock(rtg, XFS_RTGLOCK_RMAP);
		if (error) {
			xfs_group_put(xg);
			break;
		}
	}

	xfs_trans_cancel(tp);
	return 0;
}

/*
 * Compute the desired verify IO size.
 *
 * To minimize command overhead, we'd like to create bios that are 1MB, though
 * we allow the user to ask for a smaller size.
 */
static unsigned int
xfs_verify_iosize(
	const struct xfs_verify_media	*me,
	struct xfs_buftarg		*btp,
	uint64_t			bbcount)
{
	unsigned int			iosize =
			min_not_zero(SZ_1M, me->me_max_io_size);

	BUILD_BUG_ON(BBSHIFT != SECTOR_SHIFT);
	ASSERT(BBTOB(bbcount) >= bdev_logical_block_size(btp->bt_bdev));

	return clamp(iosize, bdev_logical_block_size(btp->bt_bdev),
			BBTOB(bbcount));
}

/* Allocate as much memory as we can get for verification buffer. */
static struct folio *
xfs_verify_alloc_folio(
	const unsigned int	iosize)
{
	unsigned int		order = get_order(iosize);

	while (order > 0) {
		struct folio	*folio =
			folio_alloc(GFP_KERNEL | __GFP_NORETRY, order);

		if (folio)
			return folio;
		order--;
	}

	return folio_alloc(GFP_KERNEL, 0);
}

/* Report any kind of problem verifying media */
static void
xfs_verify_media_error(
	struct xfs_mount	*mp,
	struct xfs_verify_media	*me,
	struct xfs_buftarg	*btp,
	xfs_daddr_t		daddr,
	unsigned int		bio_bbcount,
	blk_status_t		bio_status)
{
	trace_xfs_verify_media_error(mp, me, btp->bt_bdev->bd_dev, daddr,
			bio_bbcount, bio_status);

	/*
	 * Pass any error, I/O or otherwise, up to the caller if we didn't
	 * successfully verify any bytes at all.
	 */
	if (me->me_start_daddr == daddr)
		me->me_ioerror = -blk_status_to_errno(bio_status);

	/*
	 * PI validation failures, medium errors, or general IO errors are
	 * treated as indicators of data loss.  Everything else are (hopefully)
	 * transient errors and are not reported to healthmon or fsnotify.
	 */
	switch (bio_status) {
	case BLK_STS_PROTECTION:
	case BLK_STS_IOERR:
	case BLK_STS_MEDIUM:
		break;
	default:
		return;
	}

	if (!(me->me_flags & XFS_VERIFY_MEDIA_REPORT))
		return;

	xfs_healthmon_report_media(mp, me->me_dev, daddr, bio_bbcount);

	if (!xfs_has_rmapbt(mp))
		return;

	switch (me->me_dev) {
	case XFS_DEV_DATA:
		xfs_verify_report_losses(mp, XG_TYPE_AG, daddr, bio_bbcount);
		break;
	case XFS_DEV_RT:
		xfs_verify_report_losses(mp, XG_TYPE_RTG, daddr, bio_bbcount);
		break;
	}
}

/* Verify the media of an xfs device by submitting read requests to the disk. */
static int
xfs_verify_media(
	struct xfs_mount	*mp,
	struct xfs_verify_media	*me)
{
	struct xfs_buftarg	*btp = NULL;
	struct bio		*bio;
	struct folio		*folio;
	xfs_daddr_t		daddr;
	uint64_t		bbcount;
	int			error = 0;

	me->me_ioerror = 0;

	switch (me->me_dev) {
	case XFS_DEV_DATA:
		btp = mp->m_ddev_targp;
		break;
	case XFS_DEV_LOG:
		if (mp->m_logdev_targp->bt_bdev != mp->m_ddev_targp->bt_bdev)
			btp = mp->m_logdev_targp;
		break;
	case XFS_DEV_RT:
		btp = mp->m_rtdev_targp;
		break;
	}
	if (!btp)
		return -ENODEV;

	/*
	 * If the caller told us to verify beyond the end of the disk, tell the
	 * user exactly where that was.
	 */
	if (me->me_end_daddr > btp->bt_nr_sectors)
		me->me_end_daddr = btp->bt_nr_sectors;

	/* start and end have to be aligned to the lba size */
	if (!IS_ALIGNED(BBTOB(me->me_start_daddr | me->me_end_daddr),
			bdev_logical_block_size(btp->bt_bdev)))
		return -EINVAL;

	/*
	 * end_daddr is the exclusive end of the range, so if start_daddr
	 * reaches there (or beyond), there's no work to be done.
	 */
	if (me->me_start_daddr >= me->me_end_daddr)
		return 0;

	/*
	 * There are three ranges involved here:
	 *
	 *  - [me->me_start_daddr, me->me_end_daddr) is the range that the
	 *    user wants to verify.  end_daddr can be beyond the end of the
	 *    disk; we'll constrain it to the end if necessary.
	 *
	 *  - [daddr, me->me_end_daddr) is the range that we have not yet
	 *    verified.  We update daddr after each successful read.
	 *    me->me_start_daddr is set to daddr before returning.
	 *
	 *  - [daddr, daddr + bio_bbcount) is the range that we're currently
	 *    verifying.
	 */
	daddr = me->me_start_daddr;
	bbcount = min_t(sector_t, me->me_end_daddr, btp->bt_nr_sectors) -
			  me->me_start_daddr;

	folio = xfs_verify_alloc_folio(xfs_verify_iosize(me, btp, bbcount));
	if (!folio)
		return -ENOMEM;

	trace_xfs_verify_media(mp, me, btp->bt_bdev->bd_dev, daddr, bbcount,
			folio);

	bio = bio_alloc(btp->bt_bdev, 1, REQ_OP_READ, GFP_KERNEL);
	if (!bio) {
		error = -ENOMEM;
		goto out_folio;
	}

	while (bbcount > 0) {
		unsigned int	bio_bbcount;
		blk_status_t	bio_status;

		bio_reset(bio, btp->bt_bdev, REQ_OP_READ);
		bio->bi_iter.bi_sector = daddr;
		bio_add_folio_nofail(bio, folio,
				min(bbcount << SECTOR_SHIFT, folio_size(folio)),
				0);

		/*
		 * Save the length of the bio before we submit it, because we
		 * need the original daddr and length for reporting IO errors
		 * if the bio fails.
		 */
		bio_bbcount = bio->bi_iter.bi_size >> SECTOR_SHIFT;
		submit_bio_wait(bio);
		bio_status = bio->bi_status;
		if (bio_status != BLK_STS_OK) {
			xfs_verify_media_error(mp, me, btp, daddr, bio_bbcount,
					bio_status);
			error = 0;
			break;
		}

		daddr += bio_bbcount;
		bbcount -= bio_bbcount;

		if (bbcount == 0)
			break;

		if (me->me_rest_us) {
			ktime_t	expires;

			expires = ktime_add_ns(ktime_get(),
					me->me_rest_us * 1000);
			set_current_state(TASK_KILLABLE);
			schedule_hrtimeout(&expires, HRTIMER_MODE_ABS);
		}

		if (fatal_signal_pending(current)) {
			error = -EINTR;
			break;
		}

		cond_resched();
	}

	bio_put(bio);
out_folio:
	folio_put(folio);

	if (error)
		return error;

	/*
	 * Advance start_daddr to the end of what we verified if there wasn't
	 * an operational error.
	 */
	me->me_start_daddr = daddr;
	trace_xfs_verify_media_end(mp, me, btp->bt_bdev->bd_dev);
	return 0;
}

int
xfs_ioc_verify_media(
	struct file			*file,
	struct xfs_verify_media __user	*arg)
{
	struct xfs_verify_media		me;
	struct xfs_inode		*ip = XFS_I(file_inode(file));
	struct xfs_mount		*mp = ip->i_mount;
	int				error;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&me, arg, sizeof(me)))
		return -EFAULT;

	if (me.me_pad)
		return -EINVAL;
	if (me.me_flags & ~XFS_VERIFY_MEDIA_FLAGS)
		return -EINVAL;

	switch (me.me_dev) {
	case XFS_DEV_DATA:
	case XFS_DEV_LOG:
	case XFS_DEV_RT:
		break;
	default:
		return -EINVAL;
	}

	error = xfs_verify_media(mp, &me);
	if (error)
		return error;

	if (copy_to_user(arg, &me, sizeof(me)))
		return -EFAULT;

	return 0;
}
