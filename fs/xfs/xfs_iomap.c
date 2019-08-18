// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2016-2018 Christoph Hellwig.
 * All Rights Reserved.
 */
#include <linux/iomap.h>
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_btree.h"
#include "xfs_bmap_btree.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_trans.h"
#include "xfs_trans_space.h"
#include "xfs_inode_item.h"
#include "xfs_iomap.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_quota.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_reflink.h"


#define XFS_WRITEIO_ALIGN(mp,off)	(((off) >> mp->m_writeio_log) \
						<< mp->m_writeio_log)

static int
xfs_alert_fsblock_zero(
	xfs_inode_t	*ip,
	xfs_bmbt_irec_t	*imap)
{
	xfs_alert_tag(ip->i_mount, XFS_PTAG_FSBLOCK_ZERO,
			"Access to block zero in inode %llu "
			"start_block: %llx start_off: %llx "
			"blkcnt: %llx extent-state: %x",
		(unsigned long long)ip->i_ino,
		(unsigned long long)imap->br_startblock,
		(unsigned long long)imap->br_startoff,
		(unsigned long long)imap->br_blockcount,
		imap->br_state);
	return -EFSCORRUPTED;
}

int
xfs_bmbt_to_iomap(
	struct xfs_inode	*ip,
	struct iomap		*iomap,
	struct xfs_bmbt_irec	*imap,
	bool			shared)
{
	struct xfs_mount	*mp = ip->i_mount;

	if (unlikely(!imap->br_startblock && !XFS_IS_REALTIME_INODE(ip)))
		return xfs_alert_fsblock_zero(ip, imap);

	if (imap->br_startblock == HOLESTARTBLOCK) {
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->type = IOMAP_HOLE;
	} else if (imap->br_startblock == DELAYSTARTBLOCK ||
		   isnullstartblock(imap->br_startblock)) {
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->type = IOMAP_DELALLOC;
	} else {
		iomap->addr = BBTOB(xfs_fsb_to_db(ip, imap->br_startblock));
		if (imap->br_state == XFS_EXT_UNWRITTEN)
			iomap->type = IOMAP_UNWRITTEN;
		else
			iomap->type = IOMAP_MAPPED;
	}
	iomap->offset = XFS_FSB_TO_B(mp, imap->br_startoff);
	iomap->length = XFS_FSB_TO_B(mp, imap->br_blockcount);
	iomap->bdev = xfs_find_bdev_for_inode(VFS_I(ip));
	iomap->dax_dev = xfs_find_daxdev_for_inode(VFS_I(ip));

	if (xfs_ipincount(ip) &&
	    (ip->i_itemp->ili_fsync_fields & ~XFS_ILOG_TIMESTAMP))
		iomap->flags |= IOMAP_F_DIRTY;
	if (shared)
		iomap->flags |= IOMAP_F_SHARED;
	return 0;
}

static void
xfs_hole_to_iomap(
	struct xfs_inode	*ip,
	struct iomap		*iomap,
	xfs_fileoff_t		offset_fsb,
	xfs_fileoff_t		end_fsb)
{
	iomap->addr = IOMAP_NULL_ADDR;
	iomap->type = IOMAP_HOLE;
	iomap->offset = XFS_FSB_TO_B(ip->i_mount, offset_fsb);
	iomap->length = XFS_FSB_TO_B(ip->i_mount, end_fsb - offset_fsb);
	iomap->bdev = xfs_find_bdev_for_inode(VFS_I(ip));
	iomap->dax_dev = xfs_find_daxdev_for_inode(VFS_I(ip));
}

xfs_extlen_t
xfs_eof_alignment(
	struct xfs_inode	*ip,
	xfs_extlen_t		extsize)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_extlen_t		align = 0;

	if (!XFS_IS_REALTIME_INODE(ip)) {
		/*
		 * Round up the allocation request to a stripe unit
		 * (m_dalign) boundary if the file size is >= stripe unit
		 * size, and we are allocating past the allocation eof.
		 *
		 * If mounted with the "-o swalloc" option the alignment is
		 * increased from the strip unit size to the stripe width.
		 */
		if (mp->m_swidth && (mp->m_flags & XFS_MOUNT_SWALLOC))
			align = mp->m_swidth;
		else if (mp->m_dalign)
			align = mp->m_dalign;

		if (align && XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, align))
			align = 0;
	}

	/*
	 * Always round up the allocation request to an extent boundary
	 * (when file on a real-time subvolume or has di_extsize hint).
	 */
	if (extsize) {
		if (align)
			align = roundup_64(align, extsize);
		else
			align = extsize;
	}

	return align;
}

STATIC int
xfs_iomap_eof_align_last_fsb(
	struct xfs_inode	*ip,
	xfs_extlen_t		extsize,
	xfs_fileoff_t		*last_fsb)
{
	xfs_extlen_t		align = xfs_eof_alignment(ip, extsize);

	if (align) {
		xfs_fileoff_t	new_last_fsb = roundup_64(*last_fsb, align);
		int		eof, error;

		error = xfs_bmap_eof(ip, new_last_fsb, XFS_DATA_FORK, &eof);
		if (error)
			return error;
		if (eof)
			*last_fsb = new_last_fsb;
	}
	return 0;
}

int
xfs_iomap_write_direct(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	size_t		count,
	xfs_bmbt_irec_t *imap,
	int		nmaps)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	offset_fsb;
	xfs_fileoff_t	last_fsb;
	xfs_filblks_t	count_fsb, resaligned;
	xfs_extlen_t	extsz;
	int		nimaps;
	int		quota_flag;
	int		rt;
	xfs_trans_t	*tp;
	uint		qblocks, resblks, resrtextents;
	int		error;
	int		lockmode;
	int		bmapi_flags = XFS_BMAPI_PREALLOC;
	uint		tflags = 0;

	rt = XFS_IS_REALTIME_INODE(ip);
	extsz = xfs_get_extsz_hint(ip);
	lockmode = XFS_ILOCK_SHARED;	/* locked by caller */

	ASSERT(xfs_isilocked(ip, lockmode));

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	last_fsb = XFS_B_TO_FSB(mp, ((xfs_ufsize_t)(offset + count)));
	if ((offset + count) > XFS_ISIZE(ip)) {
		/*
		 * Assert that the in-core extent list is present since this can
		 * call xfs_iread_extents() and we only have the ilock shared.
		 * This should be safe because the lock was held around a bmapi
		 * call in the caller and we only need it to access the in-core
		 * list.
		 */
		ASSERT(XFS_IFORK_PTR(ip, XFS_DATA_FORK)->if_flags &
								XFS_IFEXTENTS);
		error = xfs_iomap_eof_align_last_fsb(ip, extsz, &last_fsb);
		if (error)
			goto out_unlock;
	} else {
		if (nmaps && (imap->br_startblock == HOLESTARTBLOCK))
			last_fsb = min(last_fsb, (xfs_fileoff_t)
					imap->br_blockcount +
					imap->br_startoff);
	}
	count_fsb = last_fsb - offset_fsb;
	ASSERT(count_fsb > 0);
	resaligned = xfs_aligned_fsb_count(offset_fsb, count_fsb, extsz);

	if (unlikely(rt)) {
		resrtextents = qblocks = resaligned;
		resrtextents /= mp->m_sb.sb_rextsize;
		resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
		quota_flag = XFS_QMOPT_RES_RTBLKS;
	} else {
		resrtextents = 0;
		resblks = qblocks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);
		quota_flag = XFS_QMOPT_RES_REGBLKS;
	}

	/*
	 * Drop the shared lock acquired by the caller, attach the dquot if
	 * necessary and move on to transaction setup.
	 */
	xfs_iunlock(ip, lockmode);
	error = xfs_qm_dqattach(ip);
	if (error)
		return error;

	/*
	 * For DAX, we do not allocate unwritten extents, but instead we zero
	 * the block before we commit the transaction.  Ideally we'd like to do
	 * this outside the transaction context, but if we commit and then crash
	 * we may not have zeroed the blocks and this will be exposed on
	 * recovery of the allocation. Hence we must zero before commit.
	 *
	 * Further, if we are mapping unwritten extents here, we need to zero
	 * and convert them to written so that we don't need an unwritten extent
	 * callback for DAX. This also means that we need to be able to dip into
	 * the reserve block pool for bmbt block allocation if there is no space
	 * left but we need to do unwritten extent conversion.
	 */
	if (IS_DAX(VFS_I(ip))) {
		bmapi_flags = XFS_BMAPI_CONVERT | XFS_BMAPI_ZERO;
		if (imap->br_state == XFS_EXT_UNWRITTEN) {
			tflags |= XFS_TRANS_RESERVE;
			resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;
		}
	}
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, resrtextents,
			tflags, &tp);
	if (error)
		return error;

	lockmode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, lockmode);

	error = xfs_trans_reserve_quota_nblks(tp, ip, qblocks, 0, quota_flag);
	if (error)
		goto out_trans_cancel;

	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * From this point onwards we overwrite the imap pointer that the
	 * caller gave to us.
	 */
	nimaps = 1;
	error = xfs_bmapi_write(tp, ip, offset_fsb, count_fsb,
				bmapi_flags, resblks, imap, &nimaps);
	if (error)
		goto out_res_cancel;

	/*
	 * Complete the transaction
	 */
	error = xfs_trans_commit(tp);
	if (error)
		goto out_unlock;

	/*
	 * Copy any maps to caller's array and return any error.
	 */
	if (nimaps == 0) {
		error = -ENOSPC;
		goto out_unlock;
	}

	if (!(imap->br_startblock || XFS_IS_REALTIME_INODE(ip)))
		error = xfs_alert_fsblock_zero(ip, imap);

out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;

out_res_cancel:
	xfs_trans_unreserve_quota_nblks(tp, ip, (long)qblocks, 0, quota_flag);
out_trans_cancel:
	xfs_trans_cancel(tp);
	goto out_unlock;
}

STATIC bool
xfs_quota_need_throttle(
	struct xfs_inode *ip,
	int type,
	xfs_fsblock_t alloc_blocks)
{
	struct xfs_dquot *dq = xfs_inode_dquot(ip, type);

	if (!dq || !xfs_this_quota_on(ip->i_mount, type))
		return false;

	/* no hi watermark, no throttle */
	if (!dq->q_prealloc_hi_wmark)
		return false;

	/* under the lo watermark, no throttle */
	if (dq->q_res_bcount + alloc_blocks < dq->q_prealloc_lo_wmark)
		return false;

	return true;
}

STATIC void
xfs_quota_calc_throttle(
	struct xfs_inode *ip,
	int type,
	xfs_fsblock_t *qblocks,
	int *qshift,
	int64_t	*qfreesp)
{
	int64_t freesp;
	int shift = 0;
	struct xfs_dquot *dq = xfs_inode_dquot(ip, type);

	/* no dq, or over hi wmark, squash the prealloc completely */
	if (!dq || dq->q_res_bcount >= dq->q_prealloc_hi_wmark) {
		*qblocks = 0;
		*qfreesp = 0;
		return;
	}

	freesp = dq->q_prealloc_hi_wmark - dq->q_res_bcount;
	if (freesp < dq->q_low_space[XFS_QLOWSP_5_PCNT]) {
		shift = 2;
		if (freesp < dq->q_low_space[XFS_QLOWSP_3_PCNT])
			shift += 2;
		if (freesp < dq->q_low_space[XFS_QLOWSP_1_PCNT])
			shift += 2;
	}

	if (freesp < *qfreesp)
		*qfreesp = freesp;

	/* only overwrite the throttle values if we are more aggressive */
	if ((freesp >> shift) < (*qblocks >> *qshift)) {
		*qblocks = freesp;
		*qshift = shift;
	}
}

/*
 * If we are doing a write at the end of the file and there are no allocations
 * past this one, then extend the allocation out to the file system's write
 * iosize.
 *
 * If we don't have a user specified preallocation size, dynamically increase
 * the preallocation size as the size of the file grows.  Cap the maximum size
 * at a single extent or less if the filesystem is near full. The closer the
 * filesystem is to full, the smaller the maximum prealocation.
 *
 * As an exception we don't do any preallocation at all if the file is smaller
 * than the minimum preallocation and we are using the default dynamic
 * preallocation scheme, as it is likely this is the only write to the file that
 * is going to be done.
 *
 * We clean up any extra space left over when the file is closed in
 * xfs_inactive().
 */
STATIC xfs_fsblock_t
xfs_iomap_prealloc_size(
	struct xfs_inode	*ip,
	int			whichfork,
	loff_t			offset,
	loff_t			count,
	struct xfs_iext_cursor	*icur)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, whichfork);
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	struct xfs_bmbt_irec	prev;
	int			shift = 0;
	int64_t			freesp;
	xfs_fsblock_t		qblocks;
	int			qshift = 0;
	xfs_fsblock_t		alloc_blocks = 0;

	if (offset + count <= XFS_ISIZE(ip))
		return 0;

	if (!(mp->m_flags & XFS_MOUNT_DFLT_IOSIZE) &&
	    (XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, mp->m_writeio_blocks)))
		return 0;

	/*
	 * If an explicit allocsize is set, the file is small, or we
	 * are writing behind a hole, then use the minimum prealloc:
	 */
	if ((mp->m_flags & XFS_MOUNT_DFLT_IOSIZE) ||
	    XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, mp->m_dalign) ||
	    !xfs_iext_peek_prev_extent(ifp, icur, &prev) ||
	    prev.br_startoff + prev.br_blockcount < offset_fsb)
		return mp->m_writeio_blocks;

	/*
	 * Determine the initial size of the preallocation. We are beyond the
	 * current EOF here, but we need to take into account whether this is
	 * a sparse write or an extending write when determining the
	 * preallocation size.  Hence we need to look up the extent that ends
	 * at the current write offset and use the result to determine the
	 * preallocation size.
	 *
	 * If the extent is a hole, then preallocation is essentially disabled.
	 * Otherwise we take the size of the preceding data extent as the basis
	 * for the preallocation size. If the size of the extent is greater than
	 * half the maximum extent length, then use the current offset as the
	 * basis. This ensures that for large files the preallocation size
	 * always extends to MAXEXTLEN rather than falling short due to things
	 * like stripe unit/width alignment of real extents.
	 */
	if (prev.br_blockcount <= (MAXEXTLEN >> 1))
		alloc_blocks = prev.br_blockcount << 1;
	else
		alloc_blocks = XFS_B_TO_FSB(mp, offset);
	if (!alloc_blocks)
		goto check_writeio;
	qblocks = alloc_blocks;

	/*
	 * MAXEXTLEN is not a power of two value but we round the prealloc down
	 * to the nearest power of two value after throttling. To prevent the
	 * round down from unconditionally reducing the maximum supported prealloc
	 * size, we round up first, apply appropriate throttling, round down and
	 * cap the value to MAXEXTLEN.
	 */
	alloc_blocks = XFS_FILEOFF_MIN(roundup_pow_of_two(MAXEXTLEN),
				       alloc_blocks);

	freesp = percpu_counter_read_positive(&mp->m_fdblocks);
	if (freesp < mp->m_low_space[XFS_LOWSP_5_PCNT]) {
		shift = 2;
		if (freesp < mp->m_low_space[XFS_LOWSP_4_PCNT])
			shift++;
		if (freesp < mp->m_low_space[XFS_LOWSP_3_PCNT])
			shift++;
		if (freesp < mp->m_low_space[XFS_LOWSP_2_PCNT])
			shift++;
		if (freesp < mp->m_low_space[XFS_LOWSP_1_PCNT])
			shift++;
	}

	/*
	 * Check each quota to cap the prealloc size, provide a shift value to
	 * throttle with and adjust amount of available space.
	 */
	if (xfs_quota_need_throttle(ip, XFS_DQ_USER, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQ_USER, &qblocks, &qshift,
					&freesp);
	if (xfs_quota_need_throttle(ip, XFS_DQ_GROUP, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQ_GROUP, &qblocks, &qshift,
					&freesp);
	if (xfs_quota_need_throttle(ip, XFS_DQ_PROJ, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQ_PROJ, &qblocks, &qshift,
					&freesp);

	/*
	 * The final prealloc size is set to the minimum of free space available
	 * in each of the quotas and the overall filesystem.
	 *
	 * The shift throttle value is set to the maximum value as determined by
	 * the global low free space values and per-quota low free space values.
	 */
	alloc_blocks = min(alloc_blocks, qblocks);
	shift = max(shift, qshift);

	if (shift)
		alloc_blocks >>= shift;
	/*
	 * rounddown_pow_of_two() returns an undefined result if we pass in
	 * alloc_blocks = 0.
	 */
	if (alloc_blocks)
		alloc_blocks = rounddown_pow_of_two(alloc_blocks);
	if (alloc_blocks > MAXEXTLEN)
		alloc_blocks = MAXEXTLEN;

	/*
	 * If we are still trying to allocate more space than is
	 * available, squash the prealloc hard. This can happen if we
	 * have a large file on a small filesystem and the above
	 * lowspace thresholds are smaller than MAXEXTLEN.
	 */
	while (alloc_blocks && alloc_blocks >= freesp)
		alloc_blocks >>= 4;
check_writeio:
	if (alloc_blocks < mp->m_writeio_blocks)
		alloc_blocks = mp->m_writeio_blocks;
	trace_xfs_iomap_prealloc_size(ip, alloc_blocks, shift,
				      mp->m_writeio_blocks);
	return alloc_blocks;
}

static int
xfs_file_iomap_begin_delay(
	struct inode		*inode,
	loff_t			offset,
	loff_t			count,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		maxbytes_fsb =
		XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes);
	xfs_fileoff_t		end_fsb;
	struct xfs_bmbt_irec	imap, cmap;
	struct xfs_iext_cursor	icur, ccur;
	xfs_fsblock_t		prealloc_blocks = 0;
	bool			eof = false, cow_eof = false, shared = false;
	int			whichfork = XFS_DATA_FORK;
	int			error = 0;

	ASSERT(!XFS_IS_REALTIME_INODE(ip));
	ASSERT(!xfs_get_extsz_hint(ip));

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (unlikely(XFS_TEST_ERROR(
	    (XFS_IFORK_FORMAT(ip, XFS_DATA_FORK) != XFS_DINODE_FMT_EXTENTS &&
	     XFS_IFORK_FORMAT(ip, XFS_DATA_FORK) != XFS_DINODE_FMT_BTREE),
	     mp, XFS_ERRTAG_BMAPIFORMAT))) {
		XFS_ERROR_REPORT(__func__, XFS_ERRLEVEL_LOW, mp);
		error = -EFSCORRUPTED;
		goto out_unlock;
	}

	XFS_STATS_INC(mp, xs_blk_mapw);

	if (!(ip->i_df.if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
		if (error)
			goto out_unlock;
	}

	end_fsb = min(XFS_B_TO_FSB(mp, offset + count), maxbytes_fsb);

	/*
	 * Search the data fork fork first to look up our source mapping.  We
	 * always need the data fork map, as we have to return it to the
	 * iomap code so that the higher level write code can read data in to
	 * perform read-modify-write cycles for unaligned writes.
	 */
	eof = !xfs_iext_lookup_extent(ip, &ip->i_df, offset_fsb, &icur, &imap);
	if (eof)
		imap.br_startoff = end_fsb; /* fake hole until the end */

	/* We never need to allocate blocks for zeroing a hole. */
	if ((flags & IOMAP_ZERO) && imap.br_startoff > offset_fsb) {
		xfs_hole_to_iomap(ip, iomap, offset_fsb, imap.br_startoff);
		goto out_unlock;
	}

	/*
	 * Search the COW fork extent list even if we did not find a data fork
	 * extent.  This serves two purposes: first this implements the
	 * speculative preallocation using cowextsize, so that we also unshare
	 * block adjacent to shared blocks instead of just the shared blocks
	 * themselves.  Second the lookup in the extent list is generally faster
	 * than going out to the shared extent tree.
	 */
	if (xfs_is_cow_inode(ip)) {
		if (!ip->i_cowfp) {
			ASSERT(!xfs_is_reflink_inode(ip));
			xfs_ifork_init_cow(ip);
		}
		cow_eof = !xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb,
				&ccur, &cmap);
		if (!cow_eof && cmap.br_startoff <= offset_fsb) {
			trace_xfs_reflink_cow_found(ip, &cmap);
			whichfork = XFS_COW_FORK;
			goto done;
		}
	}

	if (imap.br_startoff <= offset_fsb) {
		/*
		 * For reflink files we may need a delalloc reservation when
		 * overwriting shared extents.   This includes zeroing of
		 * existing extents that contain data.
		 */
		if (!xfs_is_cow_inode(ip) ||
		    ((flags & IOMAP_ZERO) && imap.br_state != XFS_EXT_NORM)) {
			trace_xfs_iomap_found(ip, offset, count, XFS_DATA_FORK,
					&imap);
			goto done;
		}

		xfs_trim_extent(&imap, offset_fsb, end_fsb - offset_fsb);

		/* Trim the mapping to the nearest shared extent boundary. */
		error = xfs_inode_need_cow(ip, &imap, &shared);
		if (error)
			goto out_unlock;

		/* Not shared?  Just report the (potentially capped) extent. */
		if (!shared) {
			trace_xfs_iomap_found(ip, offset, count, XFS_DATA_FORK,
					&imap);
			goto done;
		}

		/*
		 * Fork all the shared blocks from our write offset until the
		 * end of the extent.
		 */
		whichfork = XFS_COW_FORK;
		end_fsb = imap.br_startoff + imap.br_blockcount;
	} else {
		/*
		 * We cap the maximum length we map here to MAX_WRITEBACK_PAGES
		 * pages to keep the chunks of work done where somewhat
		 * symmetric with the work writeback does.  This is a completely
		 * arbitrary number pulled out of thin air.
		 *
		 * Note that the values needs to be less than 32-bits wide until
		 * the lower level functions are updated.
		 */
		count = min_t(loff_t, count, 1024 * PAGE_SIZE);
		end_fsb = min(XFS_B_TO_FSB(mp, offset + count), maxbytes_fsb);

		if (xfs_is_always_cow_inode(ip))
			whichfork = XFS_COW_FORK;
	}

	error = xfs_qm_dqattach_locked(ip, false);
	if (error)
		goto out_unlock;

	if (eof) {
		prealloc_blocks = xfs_iomap_prealloc_size(ip, whichfork, offset,
				count, &icur);
		if (prealloc_blocks) {
			xfs_extlen_t	align;
			xfs_off_t	end_offset;
			xfs_fileoff_t	p_end_fsb;

			end_offset = XFS_WRITEIO_ALIGN(mp, offset + count - 1);
			p_end_fsb = XFS_B_TO_FSBT(mp, end_offset) +
					prealloc_blocks;

			align = xfs_eof_alignment(ip, 0);
			if (align)
				p_end_fsb = roundup_64(p_end_fsb, align);

			p_end_fsb = min(p_end_fsb, maxbytes_fsb);
			ASSERT(p_end_fsb > offset_fsb);
			prealloc_blocks = p_end_fsb - end_fsb;
		}
	}

retry:
	error = xfs_bmapi_reserve_delalloc(ip, whichfork, offset_fsb,
			end_fsb - offset_fsb, prealloc_blocks,
			whichfork == XFS_DATA_FORK ? &imap : &cmap,
			whichfork == XFS_DATA_FORK ? &icur : &ccur,
			whichfork == XFS_DATA_FORK ? eof : cow_eof);
	switch (error) {
	case 0:
		break;
	case -ENOSPC:
	case -EDQUOT:
		/* retry without any preallocation */
		trace_xfs_delalloc_enospc(ip, offset, count);
		if (prealloc_blocks) {
			prealloc_blocks = 0;
			goto retry;
		}
		/*FALLTHRU*/
	default:
		goto out_unlock;
	}

	/*
	 * Flag newly allocated delalloc blocks with IOMAP_F_NEW so we punch
	 * them out if the write happens to fail.
	 */
	iomap->flags |= IOMAP_F_NEW;
	trace_xfs_iomap_alloc(ip, offset, count, whichfork,
			whichfork == XFS_DATA_FORK ? &imap : &cmap);
done:
	if (whichfork == XFS_COW_FORK) {
		if (imap.br_startoff > offset_fsb) {
			xfs_trim_extent(&cmap, offset_fsb,
					imap.br_startoff - offset_fsb);
			error = xfs_bmbt_to_iomap(ip, iomap, &cmap, true);
			goto out_unlock;
		}
		/* ensure we only report blocks we have a reservation for */
		xfs_trim_extent(&imap, cmap.br_startoff, cmap.br_blockcount);
		shared = true;
	}
	error = xfs_bmbt_to_iomap(ip, iomap, &imap, shared);
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

int
xfs_iomap_write_unwritten(
	xfs_inode_t	*ip,
	xfs_off_t	offset,
	xfs_off_t	count,
	bool		update_isize)
{
	xfs_mount_t	*mp = ip->i_mount;
	xfs_fileoff_t	offset_fsb;
	xfs_filblks_t	count_fsb;
	xfs_filblks_t	numblks_fsb;
	int		nimaps;
	xfs_trans_t	*tp;
	xfs_bmbt_irec_t imap;
	struct inode	*inode = VFS_I(ip);
	xfs_fsize_t	i_size;
	uint		resblks;
	int		error;

	trace_xfs_unwritten_convert(ip, offset, count);

	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	count_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	count_fsb = (xfs_filblks_t)(count_fsb - offset_fsb);

	/*
	 * Reserve enough blocks in this transaction for two complete extent
	 * btree splits.  We may be converting the middle part of an unwritten
	 * extent and in this case we will insert two new extents in the btree
	 * each of which could cause a full split.
	 *
	 * This reservation amount will be used in the first call to
	 * xfs_bmbt_split() to select an AG with enough space to satisfy the
	 * rest of the operation.
	 */
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;

	do {
		/*
		 * Set up a transaction to convert the range of extents
		 * from unwritten to real. Do allocations in a loop until
		 * we have covered the range passed in.
		 *
		 * Note that we can't risk to recursing back into the filesystem
		 * here as we might be asked to write out the same inode that we
		 * complete here and might deadlock on the iolock.
		 */
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0,
				XFS_TRANS_RESERVE | XFS_TRANS_NOFS, &tp);
		if (error)
			return error;

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		xfs_trans_ijoin(tp, ip, 0);

		/*
		 * Modify the unwritten extent state of the buffer.
		 */
		nimaps = 1;
		error = xfs_bmapi_write(tp, ip, offset_fsb, count_fsb,
					XFS_BMAPI_CONVERT, resblks, &imap,
					&nimaps);
		if (error)
			goto error_on_bmapi_transaction;

		/*
		 * Log the updated inode size as we go.  We have to be careful
		 * to only log it up to the actual write offset if it is
		 * halfway into a block.
		 */
		i_size = XFS_FSB_TO_B(mp, offset_fsb + count_fsb);
		if (i_size > offset + count)
			i_size = offset + count;
		if (update_isize && i_size > i_size_read(inode))
			i_size_write(inode, i_size);
		i_size = xfs_new_eof(ip, i_size);
		if (i_size) {
			ip->i_d.di_size = i_size;
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}

		error = xfs_trans_commit(tp);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return error;

		if (!(imap.br_startblock || XFS_IS_REALTIME_INODE(ip)))
			return xfs_alert_fsblock_zero(ip, &imap);

		if ((numblks_fsb = imap.br_blockcount) == 0) {
			/*
			 * The numblks_fsb value should always get
			 * smaller, otherwise the loop is stuck.
			 */
			ASSERT(imap.br_blockcount);
			break;
		}
		offset_fsb += numblks_fsb;
		count_fsb -= numblks_fsb;
	} while (count_fsb > 0);

	return 0;

error_on_bmapi_transaction:
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

static inline bool
imap_needs_alloc(
	struct inode		*inode,
	struct xfs_bmbt_irec	*imap,
	int			nimaps)
{
	return !nimaps ||
		imap->br_startblock == HOLESTARTBLOCK ||
		imap->br_startblock == DELAYSTARTBLOCK ||
		(IS_DAX(inode) && imap->br_state == XFS_EXT_UNWRITTEN);
}

static inline bool
needs_cow_for_zeroing(
	struct xfs_bmbt_irec	*imap,
	int			nimaps)
{
	return nimaps &&
		imap->br_startblock != HOLESTARTBLOCK &&
		imap->br_state != XFS_EXT_UNWRITTEN;
}

static int
xfs_ilock_for_iomap(
	struct xfs_inode	*ip,
	unsigned		flags,
	unsigned		*lockmode)
{
	unsigned		mode = XFS_ILOCK_SHARED;
	bool			is_write = flags & (IOMAP_WRITE | IOMAP_ZERO);

	/*
	 * COW writes may allocate delalloc space or convert unwritten COW
	 * extents, so we need to make sure to take the lock exclusively here.
	 */
	if (xfs_is_cow_inode(ip) && is_write) {
		/*
		 * FIXME: It could still overwrite on unshared extents and not
		 * need allocation.
		 */
		if (flags & IOMAP_NOWAIT)
			return -EAGAIN;
		mode = XFS_ILOCK_EXCL;
	}

	/*
	 * Extents not yet cached requires exclusive access, don't block.  This
	 * is an opencoded xfs_ilock_data_map_shared() call but with
	 * non-blocking behaviour.
	 */
	if (!(ip->i_df.if_flags & XFS_IFEXTENTS)) {
		if (flags & IOMAP_NOWAIT)
			return -EAGAIN;
		mode = XFS_ILOCK_EXCL;
	}

relock:
	if (flags & IOMAP_NOWAIT) {
		if (!xfs_ilock_nowait(ip, mode))
			return -EAGAIN;
	} else {
		xfs_ilock(ip, mode);
	}

	/*
	 * The reflink iflag could have changed since the earlier unlocked
	 * check, so if we got ILOCK_SHARED for a write and but we're now a
	 * reflink inode we have to switch to ILOCK_EXCL and relock.
	 */
	if (mode == XFS_ILOCK_SHARED && is_write && xfs_is_cow_inode(ip)) {
		xfs_iunlock(ip, mode);
		mode = XFS_ILOCK_EXCL;
		goto relock;
	}

	*lockmode = mode;
	return 0;
}

static int
xfs_file_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	imap;
	xfs_fileoff_t		offset_fsb, end_fsb;
	int			nimaps = 1, error = 0;
	bool			shared = false;
	unsigned		lockmode;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	if ((flags & (IOMAP_WRITE | IOMAP_ZERO)) && !(flags & IOMAP_DIRECT) &&
			!IS_DAX(inode) && !xfs_get_extsz_hint(ip)) {
		/* Reserve delalloc blocks for regular writeback. */
		return xfs_file_iomap_begin_delay(inode, offset, length, flags,
				iomap);
	}

	/*
	 * Lock the inode in the manner required for the specified operation and
	 * check for as many conditions that would result in blocking as
	 * possible. This removes most of the non-blocking checks from the
	 * mapping code below.
	 */
	error = xfs_ilock_for_iomap(ip, flags, &lockmode);
	if (error)
		return error;

	ASSERT(offset <= mp->m_super->s_maxbytes);
	if (offset > mp->m_super->s_maxbytes - length)
		length = mp->m_super->s_maxbytes - offset;
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	end_fsb = XFS_B_TO_FSB(mp, offset + length);

	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, 0);
	if (error)
		goto out_unlock;

	if (flags & IOMAP_REPORT) {
		/* Trim the mapping to the nearest shared extent boundary. */
		error = xfs_reflink_trim_around_shared(ip, &imap, &shared);
		if (error)
			goto out_unlock;
	}

	/* Non-modifying mapping requested, so we are done */
	if (!(flags & (IOMAP_WRITE | IOMAP_ZERO)))
		goto out_found;

	/*
	 * Break shared extents if necessary. Checks for non-blocking IO have
	 * been done up front, so we don't need to do them here.
	 */
	if (xfs_is_cow_inode(ip)) {
		struct xfs_bmbt_irec	cmap;
		bool			directio = (flags & IOMAP_DIRECT);

		/* if zeroing doesn't need COW allocation, then we are done. */
		if ((flags & IOMAP_ZERO) &&
		    !needs_cow_for_zeroing(&imap, nimaps))
			goto out_found;

		/* may drop and re-acquire the ilock */
		cmap = imap;
		error = xfs_reflink_allocate_cow(ip, &cmap, &shared, &lockmode,
				directio);
		if (error)
			goto out_unlock;

		/*
		 * For buffered writes we need to report the address of the
		 * previous block (if there was any) so that the higher level
		 * write code can perform read-modify-write operations; we
		 * won't need the CoW fork mapping until writeback.  For direct
		 * I/O, which must be block aligned, we need to report the
		 * newly allocated address.  If the data fork has a hole, copy
		 * the COW fork mapping to avoid allocating to the data fork.
		 */
		if (directio || imap.br_startblock == HOLESTARTBLOCK)
			imap = cmap;

		end_fsb = imap.br_startoff + imap.br_blockcount;
		length = XFS_FSB_TO_B(mp, end_fsb) - offset;
	}

	/* Don't need to allocate over holes when doing zeroing operations. */
	if (flags & IOMAP_ZERO)
		goto out_found;

	if (!imap_needs_alloc(inode, &imap, nimaps))
		goto out_found;

	/* If nowait is set bail since we are going to make allocations. */
	if (flags & IOMAP_NOWAIT) {
		error = -EAGAIN;
		goto out_unlock;
	}

	/*
	 * We cap the maximum length we map to a sane size  to keep the chunks
	 * of work done where somewhat symmetric with the work writeback does.
	 * This is a completely arbitrary number pulled out of thin air as a
	 * best guess for initial testing.
	 *
	 * Note that the values needs to be less than 32-bits wide until the
	 * lower level functions are updated.
	 */
	length = min_t(loff_t, length, 1024 * PAGE_SIZE);

	/*
	 * xfs_iomap_write_direct() expects the shared lock. It is unlocked on
	 * return.
	 */
	if (lockmode == XFS_ILOCK_EXCL)
		xfs_ilock_demote(ip, lockmode);
	error = xfs_iomap_write_direct(ip, offset, length, &imap,
			nimaps);
	if (error)
		return error;

	iomap->flags |= IOMAP_F_NEW;
	trace_xfs_iomap_alloc(ip, offset, length, XFS_DATA_FORK, &imap);

out_finish:
	return xfs_bmbt_to_iomap(ip, iomap, &imap, shared);

out_found:
	ASSERT(nimaps);
	xfs_iunlock(ip, lockmode);
	trace_xfs_iomap_found(ip, offset, length, XFS_DATA_FORK, &imap);
	goto out_finish;

out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;
}

static int
xfs_file_iomap_end_delalloc(
	struct xfs_inode	*ip,
	loff_t			offset,
	loff_t			length,
	ssize_t			written,
	struct iomap		*iomap)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		start_fsb;
	xfs_fileoff_t		end_fsb;
	int			error = 0;

	/*
	 * Behave as if the write failed if drop writes is enabled. Set the NEW
	 * flag to force delalloc cleanup.
	 */
	if (XFS_TEST_ERROR(false, mp, XFS_ERRTAG_DROP_WRITES)) {
		iomap->flags |= IOMAP_F_NEW;
		written = 0;
	}

	/*
	 * start_fsb refers to the first unused block after a short write. If
	 * nothing was written, round offset down to point at the first block in
	 * the range.
	 */
	if (unlikely(!written))
		start_fsb = XFS_B_TO_FSBT(mp, offset);
	else
		start_fsb = XFS_B_TO_FSB(mp, offset + written);
	end_fsb = XFS_B_TO_FSB(mp, offset + length);

	/*
	 * Trim delalloc blocks if they were allocated by this write and we
	 * didn't manage to write the whole range.
	 *
	 * We don't need to care about racing delalloc as we hold i_mutex
	 * across the reserve/allocate/unreserve calls. If there are delalloc
	 * blocks in the range, they are ours.
	 */
	if ((iomap->flags & IOMAP_F_NEW) && start_fsb < end_fsb) {
		truncate_pagecache_range(VFS_I(ip), XFS_FSB_TO_B(mp, start_fsb),
					 XFS_FSB_TO_B(mp, end_fsb) - 1);

		error = xfs_bmap_punch_delalloc_range(ip, start_fsb,
					       end_fsb - start_fsb);
		if (error && !XFS_FORCED_SHUTDOWN(mp)) {
			xfs_alert(mp, "%s: unable to clean up ino %lld",
				__func__, ip->i_ino);
			return error;
		}
	}

	return 0;
}

static int
xfs_file_iomap_end(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	ssize_t			written,
	unsigned		flags,
	struct iomap		*iomap)
{
	if ((flags & IOMAP_WRITE) && iomap->type == IOMAP_DELALLOC)
		return xfs_file_iomap_end_delalloc(XFS_I(inode), offset,
				length, written, iomap);
	return 0;
}

const struct iomap_ops xfs_iomap_ops = {
	.iomap_begin		= xfs_file_iomap_begin,
	.iomap_end		= xfs_file_iomap_end,
};

static int
xfs_seek_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + length);
	xfs_fileoff_t		cow_fsb = NULLFILEOFF, data_fsb = NULLFILEOFF;
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	imap, cmap;
	int			error = 0;
	unsigned		lockmode;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	lockmode = xfs_ilock_data_map_shared(ip);
	if (!(ip->i_df.if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
		if (error)
			goto out_unlock;
	}

	if (xfs_iext_lookup_extent(ip, &ip->i_df, offset_fsb, &icur, &imap)) {
		/*
		 * If we found a data extent we are done.
		 */
		if (imap.br_startoff <= offset_fsb)
			goto done;
		data_fsb = imap.br_startoff;
	} else {
		/*
		 * Fake a hole until the end of the file.
		 */
		data_fsb = min(XFS_B_TO_FSB(mp, offset + length),
			       XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes));
	}

	/*
	 * If a COW fork extent covers the hole, report it - capped to the next
	 * data fork extent:
	 */
	if (xfs_inode_has_cow_data(ip) &&
	    xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &cmap))
		cow_fsb = cmap.br_startoff;
	if (cow_fsb != NULLFILEOFF && cow_fsb <= offset_fsb) {
		if (data_fsb < cow_fsb + cmap.br_blockcount)
			end_fsb = min(end_fsb, data_fsb);
		xfs_trim_extent(&cmap, offset_fsb, end_fsb);
		error = xfs_bmbt_to_iomap(ip, iomap, &cmap, true);
		/*
		 * This is a COW extent, so we must probe the page cache
		 * because there could be dirty page cache being backed
		 * by this extent.
		 */
		iomap->type = IOMAP_UNWRITTEN;
		goto out_unlock;
	}

	/*
	 * Else report a hole, capped to the next found data or COW extent.
	 */
	if (cow_fsb != NULLFILEOFF && cow_fsb < data_fsb)
		imap.br_blockcount = cow_fsb - offset_fsb;
	else
		imap.br_blockcount = data_fsb - offset_fsb;
	imap.br_startoff = offset_fsb;
	imap.br_startblock = HOLESTARTBLOCK;
	imap.br_state = XFS_EXT_NORM;
done:
	xfs_trim_extent(&imap, offset_fsb, end_fsb);
	error = xfs_bmbt_to_iomap(ip, iomap, &imap, false);
out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;
}

const struct iomap_ops xfs_seek_iomap_ops = {
	.iomap_begin		= xfs_seek_iomap_begin,
};

static int
xfs_xattr_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + length);
	struct xfs_bmbt_irec	imap;
	int			nimaps = 1, error = 0;
	unsigned		lockmode;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	lockmode = xfs_ilock_attr_map_shared(ip);

	/* if there are no attribute fork or extents, return ENOENT */
	if (!XFS_IFORK_Q(ip) || !ip->i_d.di_anextents) {
		error = -ENOENT;
		goto out_unlock;
	}

	ASSERT(ip->i_d.di_aformat != XFS_DINODE_FMT_LOCAL);
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, XFS_BMAPI_ATTRFORK);
out_unlock:
	xfs_iunlock(ip, lockmode);

	if (error)
		return error;
	ASSERT(nimaps);
	return xfs_bmbt_to_iomap(ip, iomap, &imap, false);
}

const struct iomap_ops xfs_xattr_iomap_ops = {
	.iomap_begin		= xfs_xattr_iomap_begin,
};
