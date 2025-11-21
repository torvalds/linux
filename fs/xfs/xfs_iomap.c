// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2006 Silicon Graphics, Inc.
 * Copyright (c) 2016-2018 Christoph Hellwig.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
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
#include "xfs_quota.h"
#include "xfs_rtgroup.h"
#include "xfs_dquot_item.h"
#include "xfs_dquot.h"
#include "xfs_reflink.h"
#include "xfs_health.h"
#include "xfs_rtbitmap.h"
#include "xfs_icache.h"
#include "xfs_zone_alloc.h"

#define XFS_ALLOC_ALIGN(mp, off) \
	(((off) >> mp->m_allocsize_log) << mp->m_allocsize_log)

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
	xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
	return -EFSCORRUPTED;
}

u64
xfs_iomap_inode_sequence(
	struct xfs_inode	*ip,
	u16			iomap_flags)
{
	u64			cookie = 0;

	if (iomap_flags & IOMAP_F_XATTR)
		return READ_ONCE(ip->i_af.if_seq);
	if ((iomap_flags & IOMAP_F_SHARED) && ip->i_cowfp)
		cookie = (u64)READ_ONCE(ip->i_cowfp->if_seq) << 32;
	return cookie | READ_ONCE(ip->i_df.if_seq);
}

/*
 * Check that the iomap passed to us is still valid for the given offset and
 * length.
 */
static bool
xfs_iomap_valid(
	struct inode		*inode,
	const struct iomap	*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);

	if (iomap->type == IOMAP_HOLE)
		return true;

	if (iomap->validity_cookie !=
			xfs_iomap_inode_sequence(ip, iomap->flags)) {
		trace_xfs_iomap_invalid(ip, iomap);
		return false;
	}

	XFS_ERRORTAG_DELAY(ip->i_mount, XFS_ERRTAG_WRITE_DELAY_MS);
	return true;
}

const struct iomap_write_ops xfs_iomap_write_ops = {
	.iomap_valid		= xfs_iomap_valid,
};

int
xfs_bmbt_to_iomap(
	struct xfs_inode	*ip,
	struct iomap		*iomap,
	struct xfs_bmbt_irec	*imap,
	unsigned int		mapping_flags,
	u16			iomap_flags,
	u64			sequence_cookie)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_buftarg	*target = xfs_inode_buftarg(ip);

	if (unlikely(!xfs_valid_startblock(ip, imap->br_startblock))) {
		xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
		return xfs_alert_fsblock_zero(ip, imap);
	}

	if (imap->br_startblock == HOLESTARTBLOCK) {
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->type = IOMAP_HOLE;
	} else if (imap->br_startblock == DELAYSTARTBLOCK ||
		   isnullstartblock(imap->br_startblock)) {
		iomap->addr = IOMAP_NULL_ADDR;
		iomap->type = IOMAP_DELALLOC;
	} else {
		xfs_daddr_t	daddr = xfs_fsb_to_db(ip, imap->br_startblock);

		iomap->addr = BBTOB(daddr);
		if (mapping_flags & IOMAP_DAX)
			iomap->addr += target->bt_dax_part_off;

		if (imap->br_state == XFS_EXT_UNWRITTEN)
			iomap->type = IOMAP_UNWRITTEN;
		else
			iomap->type = IOMAP_MAPPED;

		/*
		 * Mark iomaps starting at the first sector of a RTG as merge
		 * boundary so that each I/O completions is contained to a
		 * single RTG.
		 */
		if (XFS_IS_REALTIME_INODE(ip) && xfs_has_rtgroups(mp) &&
		    xfs_rtbno_is_group_start(mp, imap->br_startblock))
			iomap->flags |= IOMAP_F_BOUNDARY;
	}
	iomap->offset = XFS_FSB_TO_B(mp, imap->br_startoff);
	iomap->length = XFS_FSB_TO_B(mp, imap->br_blockcount);
	if (mapping_flags & IOMAP_DAX)
		iomap->dax_dev = target->bt_daxdev;
	else
		iomap->bdev = target->bt_bdev;
	iomap->flags = iomap_flags;

	/*
	 * If the inode is dirty for datasync purposes, let iomap know so it
	 * doesn't elide the IO completion journal flushes on O_DSYNC IO.
	 */
	if (ip->i_itemp) {
		struct xfs_inode_log_item *iip = ip->i_itemp;

		spin_lock(&iip->ili_lock);
		if (iip->ili_datasync_seq)
			iomap->flags |= IOMAP_F_DIRTY;
		spin_unlock(&iip->ili_lock);
	}

	iomap->validity_cookie = sequence_cookie;
	return 0;
}

static void
xfs_hole_to_iomap(
	struct xfs_inode	*ip,
	struct iomap		*iomap,
	xfs_fileoff_t		offset_fsb,
	xfs_fileoff_t		end_fsb)
{
	struct xfs_buftarg	*target = xfs_inode_buftarg(ip);

	iomap->addr = IOMAP_NULL_ADDR;
	iomap->type = IOMAP_HOLE;
	iomap->offset = XFS_FSB_TO_B(ip->i_mount, offset_fsb);
	iomap->length = XFS_FSB_TO_B(ip->i_mount, end_fsb - offset_fsb);
	iomap->bdev = target->bt_bdev;
	iomap->dax_dev = target->bt_daxdev;
}

static inline xfs_fileoff_t
xfs_iomap_end_fsb(
	struct xfs_mount	*mp,
	loff_t			offset,
	loff_t			count)
{
	ASSERT(offset <= mp->m_super->s_maxbytes);
	return min(XFS_B_TO_FSB(mp, offset + count),
		   XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes));
}

static xfs_extlen_t
xfs_eof_alignment(
	struct xfs_inode	*ip)
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
		if (mp->m_swidth && xfs_has_swalloc(mp))
			align = mp->m_swidth;
		else if (mp->m_dalign)
			align = mp->m_dalign;

		if (align && XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, align))
			align = 0;
	}

	return align;
}

/*
 * Check if last_fsb is outside the last extent, and if so grow it to the next
 * stripe unit boundary.
 */
xfs_fileoff_t
xfs_iomap_eof_align_last_fsb(
	struct xfs_inode	*ip,
	xfs_fileoff_t		end_fsb)
{
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	xfs_extlen_t		extsz = xfs_get_extsz_hint(ip);
	xfs_extlen_t		align = xfs_eof_alignment(ip);
	struct xfs_bmbt_irec	irec;
	struct xfs_iext_cursor	icur;

	ASSERT(!xfs_need_iread_extents(ifp));

	/*
	 * Always round up the allocation request to the extent hint boundary.
	 */
	if (extsz) {
		if (align)
			align = roundup_64(align, extsz);
		else
			align = extsz;
	}

	if (align) {
		xfs_fileoff_t	aligned_end_fsb = roundup_64(end_fsb, align);

		xfs_iext_last(ifp, &icur);
		if (!xfs_iext_get_extent(ifp, &icur, &irec) ||
		    aligned_end_fsb >= irec.br_startoff + irec.br_blockcount)
			return aligned_end_fsb;
	}

	return end_fsb;
}

int
xfs_iomap_write_direct(
	struct xfs_inode	*ip,
	xfs_fileoff_t		offset_fsb,
	xfs_fileoff_t		count_fsb,
	unsigned int		flags,
	struct xfs_bmbt_irec	*imap,
	u64			*seq)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	xfs_filblks_t		resaligned;
	int			nimaps;
	unsigned int		dblocks, rblocks;
	bool			force = false;
	int			error;
	int			bmapi_flags = XFS_BMAPI_PREALLOC;
	int			nr_exts = XFS_IEXT_ADD_NOSPLIT_CNT;

	ASSERT(count_fsb > 0);

	resaligned = xfs_aligned_fsb_count(offset_fsb, count_fsb,
					   xfs_get_extsz_hint(ip));
	if (unlikely(XFS_IS_REALTIME_INODE(ip))) {
		dblocks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
		rblocks = resaligned;
	} else {
		dblocks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);
		rblocks = 0;
	}

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
	if (flags & IOMAP_DAX) {
		bmapi_flags = XFS_BMAPI_CONVERT | XFS_BMAPI_ZERO;
		if (imap->br_state == XFS_EXT_UNWRITTEN) {
			force = true;
			nr_exts = XFS_IEXT_WRITE_UNWRITTEN_CNT;
			dblocks = XFS_DIOSTRAT_SPACE_RES(mp, 0) << 1;
		}
	}

	error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write, dblocks,
			rblocks, force, &tp);
	if (error)
		return error;

	error = xfs_iext_count_extend(tp, ip, XFS_DATA_FORK, nr_exts);
	if (error)
		goto out_trans_cancel;

	/*
	 * From this point onwards we overwrite the imap pointer that the
	 * caller gave to us.
	 */
	nimaps = 1;
	error = xfs_bmapi_write(tp, ip, offset_fsb, count_fsb, bmapi_flags, 0,
				imap, &nimaps);
	if (error)
		goto out_trans_cancel;

	/*
	 * Complete the transaction
	 */
	error = xfs_trans_commit(tp);
	if (error)
		goto out_unlock;

	if (unlikely(!xfs_valid_startblock(ip, imap->br_startblock))) {
		xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
		error = xfs_alert_fsblock_zero(ip, imap);
	}

out_unlock:
	*seq = xfs_iomap_inode_sequence(ip, 0);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;

out_trans_cancel:
	xfs_trans_cancel(tp);
	goto out_unlock;
}

STATIC bool
xfs_quota_need_throttle(
	struct xfs_inode	*ip,
	xfs_dqtype_t		type,
	xfs_fsblock_t		alloc_blocks)
{
	struct xfs_dquot	*dq = xfs_inode_dquot(ip, type);
	struct xfs_dquot_res	*res;
	struct xfs_dquot_pre	*pre;

	if (!dq || !xfs_this_quota_on(ip->i_mount, type))
		return false;

	if (XFS_IS_REALTIME_INODE(ip)) {
		res = &dq->q_rtb;
		pre = &dq->q_rtb_prealloc;
	} else {
		res = &dq->q_blk;
		pre = &dq->q_blk_prealloc;
	}

	/* no hi watermark, no throttle */
	if (!pre->q_prealloc_hi_wmark)
		return false;

	/* under the lo watermark, no throttle */
	if (res->reserved + alloc_blocks < pre->q_prealloc_lo_wmark)
		return false;

	return true;
}

STATIC void
xfs_quota_calc_throttle(
	struct xfs_inode	*ip,
	xfs_dqtype_t		type,
	xfs_fsblock_t		*qblocks,
	int			*qshift,
	int64_t			*qfreesp)
{
	struct xfs_dquot	*dq = xfs_inode_dquot(ip, type);
	struct xfs_dquot_res	*res;
	struct xfs_dquot_pre	*pre;
	int64_t			freesp;
	int			shift = 0;

	if (!dq) {
		res = NULL;
		pre = NULL;
	} else if (XFS_IS_REALTIME_INODE(ip)) {
		res = &dq->q_rtb;
		pre = &dq->q_rtb_prealloc;
	} else {
		res = &dq->q_blk;
		pre = &dq->q_blk_prealloc;
	}

	/* no dq, or over hi wmark, squash the prealloc completely */
	if (!res || res->reserved >= pre->q_prealloc_hi_wmark) {
		*qblocks = 0;
		*qfreesp = 0;
		return;
	}

	freesp = pre->q_prealloc_hi_wmark - res->reserved;
	if (freesp < pre->q_low_space[XFS_QLOWSP_5_PCNT]) {
		shift = 2;
		if (freesp < pre->q_low_space[XFS_QLOWSP_3_PCNT])
			shift += 2;
		if (freesp < pre->q_low_space[XFS_QLOWSP_1_PCNT])
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

static int64_t
xfs_iomap_freesp(
	struct xfs_mount	*mp,
	unsigned int		idx,
	uint64_t		low_space[XFS_LOWSP_MAX],
	int			*shift)
{
	int64_t			freesp;

	freesp = xfs_estimate_freecounter(mp, idx);
	if (freesp < low_space[XFS_LOWSP_5_PCNT]) {
		*shift = 2;
		if (freesp < low_space[XFS_LOWSP_4_PCNT])
			(*shift)++;
		if (freesp < low_space[XFS_LOWSP_3_PCNT])
			(*shift)++;
		if (freesp < low_space[XFS_LOWSP_2_PCNT])
			(*shift)++;
		if (freesp < low_space[XFS_LOWSP_1_PCNT])
			(*shift)++;
	}
	return freesp;
}

/*
 * If we don't have a user specified preallocation size, dynamically increase
 * the preallocation size as the size of the file grows.  Cap the maximum size
 * at a single extent or less if the filesystem is near full. The closer the
 * filesystem is to being full, the smaller the maximum preallocation.
 */
STATIC xfs_fsblock_t
xfs_iomap_prealloc_size(
	struct xfs_inode	*ip,
	int			whichfork,
	loff_t			offset,
	loff_t			count,
	struct xfs_iext_cursor	*icur)
{
	struct xfs_iext_cursor	ncur = *icur;
	struct xfs_bmbt_irec	prev, got;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	int64_t			freesp;
	xfs_fsblock_t		qblocks;
	xfs_fsblock_t		alloc_blocks = 0;
	xfs_extlen_t		plen;
	int			shift = 0;
	int			qshift = 0;

	/*
	 * As an exception we don't do any preallocation at all if the file is
	 * smaller than the minimum preallocation and we are using the default
	 * dynamic preallocation scheme, as it is likely this is the only write
	 * to the file that is going to be done.
	 */
	if (XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, mp->m_allocsize_blocks))
		return 0;

	/*
	 * Use the minimum preallocation size for small files or if we are
	 * writing right after a hole.
	 */
	if (XFS_ISIZE(ip) < XFS_FSB_TO_B(mp, mp->m_dalign) ||
	    !xfs_iext_prev_extent(ifp, &ncur, &prev) ||
	    prev.br_startoff + prev.br_blockcount < offset_fsb)
		return mp->m_allocsize_blocks;

	/*
	 * Take the size of the preceding data extents as the basis for the
	 * preallocation size. Note that we don't care if the previous extents
	 * are written or not.
	 */
	plen = prev.br_blockcount;
	while (xfs_iext_prev_extent(ifp, &ncur, &got)) {
		if (plen > XFS_MAX_BMBT_EXTLEN / 2 ||
		    isnullstartblock(got.br_startblock) ||
		    got.br_startoff + got.br_blockcount != prev.br_startoff ||
		    got.br_startblock + got.br_blockcount != prev.br_startblock)
			break;
		plen += got.br_blockcount;
		prev = got;
	}

	/*
	 * If the size of the extents is greater than half the maximum extent
	 * length, then use the current offset as the basis.  This ensures that
	 * for large files the preallocation size always extends to
	 * XFS_BMBT_MAX_EXTLEN rather than falling short due to things like stripe
	 * unit/width alignment of real extents.
	 */
	alloc_blocks = plen * 2;
	if (alloc_blocks > XFS_MAX_BMBT_EXTLEN)
		alloc_blocks = XFS_B_TO_FSB(mp, offset);
	qblocks = alloc_blocks;

	/*
	 * XFS_BMBT_MAX_EXTLEN is not a power of two value but we round the prealloc
	 * down to the nearest power of two value after throttling. To prevent
	 * the round down from unconditionally reducing the maximum supported
	 * prealloc size, we round up first, apply appropriate throttling, round
	 * down and cap the value to XFS_BMBT_MAX_EXTLEN.
	 */
	alloc_blocks = XFS_FILEOFF_MIN(roundup_pow_of_two(XFS_MAX_BMBT_EXTLEN),
				       alloc_blocks);

	if (unlikely(XFS_IS_REALTIME_INODE(ip)))
		freesp = xfs_rtbxlen_to_blen(mp,
				xfs_iomap_freesp(mp, XC_FREE_RTEXTENTS,
					mp->m_low_rtexts, &shift));
	else
		freesp = xfs_iomap_freesp(mp, XC_FREE_BLOCKS, mp->m_low_space,
				&shift);

	/*
	 * Check each quota to cap the prealloc size, provide a shift value to
	 * throttle with and adjust amount of available space.
	 */
	if (xfs_quota_need_throttle(ip, XFS_DQTYPE_USER, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQTYPE_USER, &qblocks, &qshift,
					&freesp);
	if (xfs_quota_need_throttle(ip, XFS_DQTYPE_GROUP, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQTYPE_GROUP, &qblocks, &qshift,
					&freesp);
	if (xfs_quota_need_throttle(ip, XFS_DQTYPE_PROJ, alloc_blocks))
		xfs_quota_calc_throttle(ip, XFS_DQTYPE_PROJ, &qblocks, &qshift,
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
	if (alloc_blocks > XFS_MAX_BMBT_EXTLEN)
		alloc_blocks = XFS_MAX_BMBT_EXTLEN;

	/*
	 * If we are still trying to allocate more space than is
	 * available, squash the prealloc hard. This can happen if we
	 * have a large file on a small filesystem and the above
	 * lowspace thresholds are smaller than XFS_BMBT_MAX_EXTLEN.
	 */
	while (alloc_blocks && alloc_blocks >= freesp)
		alloc_blocks >>= 4;
	if (alloc_blocks < mp->m_allocsize_blocks)
		alloc_blocks = mp->m_allocsize_blocks;
	trace_xfs_iomap_prealloc_size(ip, alloc_blocks, shift,
				      mp->m_allocsize_blocks);
	return alloc_blocks;
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

	/* Attach dquots so that bmbt splits are accounted correctly. */
	error = xfs_qm_dqattach(ip);
	if (error)
		return error;

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
		error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write, resblks,
				0, true, &tp);
		if (error)
			return error;

		error = xfs_iext_count_extend(tp, ip, XFS_DATA_FORK,
				XFS_IEXT_WRITE_UNWRITTEN_CNT);
		if (error)
			goto error_on_bmapi_transaction;

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
			ip->i_disk_size = i_size;
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}

		error = xfs_trans_commit(tp);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return error;

		if (unlikely(!xfs_valid_startblock(ip, imap.br_startblock))) {
			xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
			return xfs_alert_fsblock_zero(ip, &imap);
		}

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
	unsigned		flags,
	struct xfs_bmbt_irec	*imap,
	int			nimaps)
{
	/* don't allocate blocks when just zeroing */
	if (flags & IOMAP_ZERO)
		return false;
	if (!nimaps ||
	    imap->br_startblock == HOLESTARTBLOCK ||
	    imap->br_startblock == DELAYSTARTBLOCK)
		return true;
	/* we convert unwritten extents before copying the data for DAX */
	if ((flags & IOMAP_DAX) && imap->br_state == XFS_EXT_UNWRITTEN)
		return true;
	return false;
}

static inline bool
imap_needs_cow(
	struct xfs_inode	*ip,
	unsigned int		flags,
	struct xfs_bmbt_irec	*imap,
	int			nimaps)
{
	if (!xfs_is_cow_inode(ip))
		return false;

	/* when zeroing we don't have to COW holes or unwritten extents */
	if (flags & (IOMAP_UNSHARE | IOMAP_ZERO)) {
		if (!nimaps ||
		    imap->br_startblock == HOLESTARTBLOCK ||
		    imap->br_state == XFS_EXT_UNWRITTEN)
			return false;
	}

	return true;
}

/*
 * Extents not yet cached requires exclusive access, don't block for
 * IOMAP_NOWAIT.
 *
 * This is basically an opencoded xfs_ilock_data_map_shared() call, but with
 * support for IOMAP_NOWAIT.
 */
static int
xfs_ilock_for_iomap(
	struct xfs_inode	*ip,
	unsigned		flags,
	unsigned		*lockmode)
{
	if (flags & IOMAP_NOWAIT) {
		if (xfs_need_iread_extents(&ip->i_df))
			return -EAGAIN;
		if (!xfs_ilock_nowait(ip, *lockmode))
			return -EAGAIN;
	} else {
		if (xfs_need_iread_extents(&ip->i_df))
			*lockmode = XFS_ILOCK_EXCL;
		xfs_ilock(ip, *lockmode);
	}

	return 0;
}

/*
 * Check that the imap we are going to return to the caller spans the entire
 * range that the caller requested for the IO.
 */
static bool
imap_spans_range(
	struct xfs_bmbt_irec	*imap,
	xfs_fileoff_t		offset_fsb,
	xfs_fileoff_t		end_fsb)
{
	if (imap->br_startoff > offset_fsb)
		return false;
	if (imap->br_startoff + imap->br_blockcount < end_fsb)
		return false;
	return true;
}

static bool
xfs_bmap_hw_atomic_write_possible(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	xfs_fileoff_t		offset_fsb,
	xfs_fileoff_t		end_fsb)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fsize_t		len = XFS_FSB_TO_B(mp, end_fsb - offset_fsb);

	/*
	 * atomic writes are required to be naturally aligned for disk blocks,
	 * which ensures that we adhere to block layer rules that we won't
	 * straddle any boundary or violate write alignment requirement.
	 */
	if (!IS_ALIGNED(imap->br_startblock, imap->br_blockcount))
		return false;

	/*
	 * Spanning multiple extents would mean that multiple BIOs would be
	 * issued, and so would lose atomicity required for REQ_ATOMIC-based
	 * atomics.
	 */
	if (!imap_spans_range(imap, offset_fsb, end_fsb))
		return false;

	/*
	 * The ->iomap_begin caller should ensure this, but check anyway.
	 */
	return len <= xfs_inode_buftarg(ip)->bt_awu_max;
}

static int
xfs_direct_write_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	imap, cmap;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = xfs_iomap_end_fsb(mp, offset, length);
	xfs_fileoff_t		orig_end_fsb = end_fsb;
	int			nimaps = 1, error = 0;
	bool			shared = false;
	u16			iomap_flags = 0;
	bool			needs_alloc;
	unsigned int		lockmode;
	u64			seq;

	ASSERT(flags & (IOMAP_WRITE | IOMAP_ZERO));

	if (xfs_is_shutdown(mp))
		return -EIO;

	/*
	 * Writes that span EOF might trigger an IO size update on completion,
	 * so consider them to be dirty for the purposes of O_DSYNC even if
	 * there is no other metadata changes pending or have been made here.
	 */
	if (offset + length > i_size_read(inode))
		iomap_flags |= IOMAP_F_DIRTY;

	/* HW-offload atomics are always used in this path */
	if (flags & IOMAP_ATOMIC)
		iomap_flags |= IOMAP_F_ATOMIC_BIO;

	/*
	 * COW writes may allocate delalloc space or convert unwritten COW
	 * extents, so we need to make sure to take the lock exclusively here.
	 */
	if (xfs_is_cow_inode(ip))
		lockmode = XFS_ILOCK_EXCL;
	else
		lockmode = XFS_ILOCK_SHARED;

relock:
	error = xfs_ilock_for_iomap(ip, flags, &lockmode);
	if (error)
		return error;

	/*
	 * The reflink iflag could have changed since the earlier unlocked
	 * check, check if it again and relock if needed.
	 */
	if (xfs_is_cow_inode(ip) && lockmode == XFS_ILOCK_SHARED) {
		xfs_iunlock(ip, lockmode);
		lockmode = XFS_ILOCK_EXCL;
		goto relock;
	}

	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, 0);
	if (error)
		goto out_unlock;

	if (imap_needs_cow(ip, flags, &imap, nimaps)) {
		error = -EAGAIN;
		if (flags & IOMAP_NOWAIT)
			goto out_unlock;

		/* may drop and re-acquire the ilock */
		error = xfs_reflink_allocate_cow(ip, &imap, &cmap, &shared,
				&lockmode,
				(flags & IOMAP_DIRECT) || IS_DAX(inode));
		if (error)
			goto out_unlock;
		if (shared) {
			if ((flags & IOMAP_ATOMIC) &&
			    !xfs_bmap_hw_atomic_write_possible(ip, &cmap,
					offset_fsb, end_fsb)) {
				error = -ENOPROTOOPT;
				goto out_unlock;
			}
			goto out_found_cow;
		}
		end_fsb = imap.br_startoff + imap.br_blockcount;
		length = XFS_FSB_TO_B(mp, end_fsb) - offset;
	}

	needs_alloc = imap_needs_alloc(inode, flags, &imap, nimaps);

	if (flags & IOMAP_ATOMIC) {
		error = -ENOPROTOOPT;
		/*
		 * If we allocate less than what is required for the write
		 * then we may end up with multiple extents, which means that
		 * REQ_ATOMIC-based cannot be used, so avoid this possibility.
		 */
		if (needs_alloc && orig_end_fsb - offset_fsb > 1)
			goto out_unlock;

		if (!xfs_bmap_hw_atomic_write_possible(ip, &imap, offset_fsb,
				orig_end_fsb))
			goto out_unlock;
	}

	if (needs_alloc)
		goto allocate_blocks;

	/*
	 * NOWAIT and OVERWRITE I/O needs to span the entire requested I/O with
	 * a single map so that we avoid partial IO failures due to the rest of
	 * the I/O range not covered by this map triggering an EAGAIN condition
	 * when it is subsequently mapped and aborting the I/O.
	 */
	if (flags & (IOMAP_NOWAIT | IOMAP_OVERWRITE_ONLY)) {
		error = -EAGAIN;
		if (!imap_spans_range(&imap, offset_fsb, end_fsb))
			goto out_unlock;
	}

	/*
	 * For overwrite only I/O, we cannot convert unwritten extents without
	 * requiring sub-block zeroing.  This can only be done under an
	 * exclusive IOLOCK, hence return -EAGAIN if this is not a written
	 * extent to tell the caller to try again.
	 */
	if (flags & IOMAP_OVERWRITE_ONLY) {
		error = -EAGAIN;
		if (imap.br_state != XFS_EXT_NORM &&
	            ((offset | length) & mp->m_blockmask))
			goto out_unlock;
	}

	seq = xfs_iomap_inode_sequence(ip, iomap_flags);
	xfs_iunlock(ip, lockmode);
	trace_xfs_iomap_found(ip, offset, length, XFS_DATA_FORK, &imap);
	return xfs_bmbt_to_iomap(ip, iomap, &imap, flags, iomap_flags, seq);

allocate_blocks:
	error = -EAGAIN;
	if (flags & (IOMAP_NOWAIT | IOMAP_OVERWRITE_ONLY))
		goto out_unlock;

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
	end_fsb = xfs_iomap_end_fsb(mp, offset, length);

	if (offset + length > XFS_ISIZE(ip))
		end_fsb = xfs_iomap_eof_align_last_fsb(ip, end_fsb);
	else if (nimaps && imap.br_startblock == HOLESTARTBLOCK)
		end_fsb = min(end_fsb, imap.br_startoff + imap.br_blockcount);
	xfs_iunlock(ip, lockmode);

	error = xfs_iomap_write_direct(ip, offset_fsb, end_fsb - offset_fsb,
			flags, &imap, &seq);
	if (error)
		return error;

	trace_xfs_iomap_alloc(ip, offset, length, XFS_DATA_FORK, &imap);
	return xfs_bmbt_to_iomap(ip, iomap, &imap, flags,
				 iomap_flags | IOMAP_F_NEW, seq);

out_found_cow:
	length = XFS_FSB_TO_B(mp, cmap.br_startoff + cmap.br_blockcount);
	trace_xfs_iomap_found(ip, offset, length - offset, XFS_COW_FORK, &cmap);
	if (imap.br_startblock != HOLESTARTBLOCK) {
		seq = xfs_iomap_inode_sequence(ip, 0);
		error = xfs_bmbt_to_iomap(ip, srcmap, &imap, flags, 0, seq);
		if (error)
			goto out_unlock;
	}
	seq = xfs_iomap_inode_sequence(ip, IOMAP_F_SHARED);
	xfs_iunlock(ip, lockmode);
	return xfs_bmbt_to_iomap(ip, iomap, &cmap, flags, IOMAP_F_SHARED, seq);

out_unlock:
	if (lockmode)
		xfs_iunlock(ip, lockmode);
	return error;
}

const struct iomap_ops xfs_direct_write_iomap_ops = {
	.iomap_begin		= xfs_direct_write_iomap_begin,
};

#ifdef CONFIG_XFS_RT
/*
 * This is really simple.  The space has already been reserved before taking the
 * IOLOCK, the actual block allocation is done just before submitting the bio
 * and only recorded in the extent map on I/O completion.
 */
static int
xfs_zoned_direct_write_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	int			error;

	ASSERT(!(flags & IOMAP_OVERWRITE_ONLY));

	/*
	 * Needs to be pushed down into the allocator so that only writes into
	 * a single zone can be supported.
	 */
	if (flags & IOMAP_NOWAIT)
		return -EAGAIN;

	/*
	 * Ensure the extent list is in memory in so that we don't have to do
	 * read it from the I/O completion handler.
	 */
	if (xfs_need_iread_extents(&ip->i_df)) {
		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		if (error)
			return error;
	}

	iomap->type = IOMAP_MAPPED;
	iomap->flags = IOMAP_F_DIRTY;
	iomap->bdev = ip->i_mount->m_rtdev_targp->bt_bdev;
	iomap->offset = offset;
	iomap->length = length;
	iomap->flags = IOMAP_F_ANON_WRITE;
	return 0;
}

const struct iomap_ops xfs_zoned_direct_write_iomap_ops = {
	.iomap_begin		= xfs_zoned_direct_write_iomap_begin,
};
#endif /* CONFIG_XFS_RT */

#ifdef DEBUG
static void
xfs_check_atomic_cow_conversion(
	struct xfs_inode		*ip,
	xfs_fileoff_t			offset_fsb,
	xfs_filblks_t			count_fsb,
	const struct xfs_bmbt_irec	*cmap)
{
	struct xfs_iext_cursor		icur;
	struct xfs_bmbt_irec		cmap2 = { };

	if (xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &cmap2))
		xfs_trim_extent(&cmap2, offset_fsb, count_fsb);

	ASSERT(cmap2.br_startoff == cmap->br_startoff);
	ASSERT(cmap2.br_blockcount == cmap->br_blockcount);
	ASSERT(cmap2.br_startblock == cmap->br_startblock);
	ASSERT(cmap2.br_state == cmap->br_state);
}
#else
# define xfs_check_atomic_cow_conversion(...)	((void)0)
#endif

static int
xfs_atomic_write_cow_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	const xfs_fileoff_t	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	const xfs_fileoff_t	end_fsb = XFS_B_TO_FSB(mp, offset + length);
	const xfs_filblks_t	count_fsb = end_fsb - offset_fsb;
	xfs_filblks_t		hole_count_fsb;
	int			nmaps = 1;
	xfs_filblks_t		resaligned;
	struct xfs_bmbt_irec	cmap;
	struct xfs_iext_cursor	icur;
	struct xfs_trans	*tp;
	unsigned int		dblocks = 0, rblocks = 0;
	int			error;
	u64			seq;

	ASSERT(flags & IOMAP_WRITE);
	ASSERT(flags & IOMAP_DIRECT);

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (!xfs_can_sw_atomic_write(mp)) {
		ASSERT(xfs_can_sw_atomic_write(mp));
		return -EINVAL;
	}

	/* blocks are always allocated in this path */
	if (flags & IOMAP_NOWAIT)
		return -EAGAIN;

	trace_xfs_iomap_atomic_write_cow(ip, offset, length);
retry:
	xfs_ilock(ip, XFS_ILOCK_EXCL);

	if (!ip->i_cowfp) {
		ASSERT(!xfs_is_reflink_inode(ip));
		xfs_ifork_init_cow(ip);
	}

	if (!xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &cmap))
		cmap.br_startoff = end_fsb;
	if (cmap.br_startoff <= offset_fsb) {
		if (isnullstartblock(cmap.br_startblock))
			goto convert_delay;

		/*
		 * cmap could extend outside the write range due to previous
		 * speculative preallocations.  We must trim cmap to the write
		 * range because the cow fork treats written mappings to mean
		 * "write in progress".
		 */
		xfs_trim_extent(&cmap, offset_fsb, count_fsb);
		goto found;
	}

	hole_count_fsb = cmap.br_startoff - offset_fsb;

	resaligned = xfs_aligned_fsb_count(offset_fsb, hole_count_fsb,
			xfs_get_cowextsz_hint(ip));
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	if (XFS_IS_REALTIME_INODE(ip)) {
		dblocks = XFS_DIOSTRAT_SPACE_RES(mp, 0);
		rblocks = resaligned;
	} else {
		dblocks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);
		rblocks = 0;
	}

	error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write, dblocks,
			rblocks, false, &tp);
	if (error)
		return error;

	/* extent layout could have changed since the unlock, so check again */
	if (!xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &cmap))
		cmap.br_startoff = end_fsb;
	if (cmap.br_startoff <= offset_fsb) {
		xfs_trans_cancel(tp);
		if (isnullstartblock(cmap.br_startblock))
			goto convert_delay;
		xfs_trim_extent(&cmap, offset_fsb, count_fsb);
		goto found;
	}

	/*
	 * Allocate the entire reservation as unwritten blocks.
	 *
	 * Use XFS_BMAPI_EXTSZALIGN to hint at aligning new extents according to
	 * extszhint, such that there will be a greater chance that future
	 * atomic writes to that same range will be aligned (and don't require
	 * this COW-based method).
	 */
	error = xfs_bmapi_write(tp, ip, offset_fsb, hole_count_fsb,
			XFS_BMAPI_COWFORK | XFS_BMAPI_PREALLOC |
			XFS_BMAPI_EXTSZALIGN, 0, &cmap, &nmaps);
	if (error) {
		xfs_trans_cancel(tp);
		goto out_unlock;
	}

	xfs_inode_set_cowblocks_tag(ip);
	error = xfs_trans_commit(tp);
	if (error)
		goto out_unlock;

	/*
	 * cmap could map more blocks than the range we passed into bmapi_write
	 * because of EXTSZALIGN or adjacent pre-existing unwritten mappings
	 * that were merged.  Trim cmap to the original write range so that we
	 * don't convert more than we were asked to do for this write.
	 */
	xfs_trim_extent(&cmap, offset_fsb, count_fsb);

found:
	if (cmap.br_state != XFS_EXT_NORM) {
		error = xfs_reflink_convert_cow_locked(ip, cmap.br_startoff,
				cmap.br_blockcount);
		if (error)
			goto out_unlock;
		cmap.br_state = XFS_EXT_NORM;
		xfs_check_atomic_cow_conversion(ip, offset_fsb, count_fsb,
				&cmap);
	}

	trace_xfs_iomap_found(ip, offset, length, XFS_COW_FORK, &cmap);
	seq = xfs_iomap_inode_sequence(ip, IOMAP_F_SHARED);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return xfs_bmbt_to_iomap(ip, iomap, &cmap, flags, IOMAP_F_SHARED, seq);

convert_delay:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	error = xfs_bmapi_convert_delalloc(ip, XFS_COW_FORK, offset, iomap,
			NULL);
	if (error)
		return error;

	/*
	 * Try the lookup again, because the delalloc conversion might have
	 * turned the COW mapping into unwritten, but we need it to be in
	 * written state.
	 */
	goto retry;
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

const struct iomap_ops xfs_atomic_write_cow_iomap_ops = {
	.iomap_begin		= xfs_atomic_write_cow_iomap_begin,
};

static int
xfs_dax_write_iomap_end(
	struct inode		*inode,
	loff_t			pos,
	loff_t			length,
	ssize_t			written,
	unsigned		flags,
	struct iomap		*iomap)
{
	struct xfs_inode	*ip = XFS_I(inode);

	if (!xfs_is_cow_inode(ip))
		return 0;

	if (!written)
		return xfs_reflink_cancel_cow_range(ip, pos, length, true);

	return xfs_reflink_end_cow(ip, pos, written);
}

const struct iomap_ops xfs_dax_write_iomap_ops = {
	.iomap_begin	= xfs_direct_write_iomap_begin,
	.iomap_end	= xfs_dax_write_iomap_end,
};

/*
 * Convert a hole to a delayed allocation.
 */
static void
xfs_bmap_add_extent_hole_delay(
	struct xfs_inode	*ip,	/* incore inode pointer */
	int			whichfork,
	struct xfs_iext_cursor	*icur,
	struct xfs_bmbt_irec	*new)	/* new data to add to file extents */
{
	struct xfs_ifork	*ifp;	/* inode fork pointer */
	xfs_bmbt_irec_t		left;	/* left neighbor extent entry */
	xfs_filblks_t		newlen=0;	/* new indirect size */
	xfs_filblks_t		oldlen=0;	/* old indirect size */
	xfs_bmbt_irec_t		right;	/* right neighbor extent entry */
	uint32_t		state = xfs_bmap_fork_to_state(whichfork);
	xfs_filblks_t		temp;	 /* temp for indirect calculations */

	ifp = xfs_ifork_ptr(ip, whichfork);
	ASSERT(isnullstartblock(new->br_startblock));

	/*
	 * Check and set flags if this segment has a left neighbor
	 */
	if (xfs_iext_peek_prev_extent(ifp, icur, &left)) {
		state |= BMAP_LEFT_VALID;
		if (isnullstartblock(left.br_startblock))
			state |= BMAP_LEFT_DELAY;
	}

	/*
	 * Check and set flags if the current (right) segment exists.
	 * If it doesn't exist, we're converting the hole at end-of-file.
	 */
	if (xfs_iext_get_extent(ifp, icur, &right)) {
		state |= BMAP_RIGHT_VALID;
		if (isnullstartblock(right.br_startblock))
			state |= BMAP_RIGHT_DELAY;
	}

	/*
	 * Set contiguity flags on the left and right neighbors.
	 * Don't let extents get too large, even if the pieces are contiguous.
	 */
	if ((state & BMAP_LEFT_VALID) && (state & BMAP_LEFT_DELAY) &&
	    left.br_startoff + left.br_blockcount == new->br_startoff &&
	    left.br_blockcount + new->br_blockcount <= XFS_MAX_BMBT_EXTLEN)
		state |= BMAP_LEFT_CONTIG;

	if ((state & BMAP_RIGHT_VALID) && (state & BMAP_RIGHT_DELAY) &&
	    new->br_startoff + new->br_blockcount == right.br_startoff &&
	    new->br_blockcount + right.br_blockcount <= XFS_MAX_BMBT_EXTLEN &&
	    (!(state & BMAP_LEFT_CONTIG) ||
	     (left.br_blockcount + new->br_blockcount +
	      right.br_blockcount <= XFS_MAX_BMBT_EXTLEN)))
		state |= BMAP_RIGHT_CONTIG;

	/*
	 * Switch out based on the contiguity flags.
	 */
	switch (state & (BMAP_LEFT_CONTIG | BMAP_RIGHT_CONTIG)) {
	case BMAP_LEFT_CONTIG | BMAP_RIGHT_CONTIG:
		/*
		 * New allocation is contiguous with delayed allocations
		 * on the left and on the right.
		 * Merge all three into a single extent record.
		 */
		temp = left.br_blockcount + new->br_blockcount +
			right.br_blockcount;

		oldlen = startblockval(left.br_startblock) +
			startblockval(new->br_startblock) +
			startblockval(right.br_startblock);
		newlen = XFS_FILBLKS_MIN(xfs_bmap_worst_indlen(ip, temp),
					 oldlen);
		left.br_startblock = nullstartblock(newlen);
		left.br_blockcount = temp;

		xfs_iext_remove(ip, icur, state);
		xfs_iext_prev(ifp, icur);
		xfs_iext_update_extent(ip, state, icur, &left);
		break;

	case BMAP_LEFT_CONTIG:
		/*
		 * New allocation is contiguous with a delayed allocation
		 * on the left.
		 * Merge the new allocation with the left neighbor.
		 */
		temp = left.br_blockcount + new->br_blockcount;

		oldlen = startblockval(left.br_startblock) +
			startblockval(new->br_startblock);
		newlen = XFS_FILBLKS_MIN(xfs_bmap_worst_indlen(ip, temp),
					 oldlen);
		left.br_blockcount = temp;
		left.br_startblock = nullstartblock(newlen);

		xfs_iext_prev(ifp, icur);
		xfs_iext_update_extent(ip, state, icur, &left);
		break;

	case BMAP_RIGHT_CONTIG:
		/*
		 * New allocation is contiguous with a delayed allocation
		 * on the right.
		 * Merge the new allocation with the right neighbor.
		 */
		temp = new->br_blockcount + right.br_blockcount;
		oldlen = startblockval(new->br_startblock) +
			startblockval(right.br_startblock);
		newlen = XFS_FILBLKS_MIN(xfs_bmap_worst_indlen(ip, temp),
					 oldlen);
		right.br_startoff = new->br_startoff;
		right.br_startblock = nullstartblock(newlen);
		right.br_blockcount = temp;
		xfs_iext_update_extent(ip, state, icur, &right);
		break;

	case 0:
		/*
		 * New allocation is not contiguous with another
		 * delayed allocation.
		 * Insert a new entry.
		 */
		oldlen = newlen = 0;
		xfs_iext_insert(ip, icur, new, state);
		break;
	}
	if (oldlen != newlen) {
		ASSERT(oldlen > newlen);
		xfs_add_fdblocks(ip->i_mount, oldlen - newlen);

		/*
		 * Nothing to do for disk quota accounting here.
		 */
		xfs_mod_delalloc(ip, 0, (int64_t)newlen - oldlen);
	}
}

/*
 * Add a delayed allocation extent to an inode. Blocks are reserved from the
 * global pool and the extent inserted into the inode in-core extent tree.
 *
 * On entry, got refers to the first extent beyond the offset of the extent to
 * allocate or eof is specified if no such extent exists. On return, got refers
 * to the extent record that was inserted to the inode fork.
 *
 * Note that the allocated extent may have been merged with contiguous extents
 * during insertion into the inode fork. Thus, got does not reflect the current
 * state of the inode fork on return. If necessary, the caller can use lastx to
 * look up the updated record in the inode fork.
 */
static int
xfs_bmapi_reserve_delalloc(
	struct xfs_inode	*ip,
	int			whichfork,
	xfs_fileoff_t		off,
	xfs_filblks_t		len,
	xfs_filblks_t		prealloc,
	struct xfs_bmbt_irec	*got,
	struct xfs_iext_cursor	*icur,
	int			eof)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, whichfork);
	xfs_extlen_t		alen;
	xfs_extlen_t		indlen;
	uint64_t		fdblocks;
	int			error;
	xfs_fileoff_t		aoff;
	bool			use_cowextszhint =
					whichfork == XFS_COW_FORK && !prealloc;

retry:
	/*
	 * Cap the alloc length. Keep track of prealloc so we know whether to
	 * tag the inode before we return.
	 */
	aoff = off;
	alen = XFS_FILBLKS_MIN(len + prealloc, XFS_MAX_BMBT_EXTLEN);
	if (!eof)
		alen = XFS_FILBLKS_MIN(alen, got->br_startoff - aoff);
	if (prealloc && alen >= len)
		prealloc = alen - len;

	/*
	 * If we're targetting the COW fork but aren't creating a speculative
	 * posteof preallocation, try to expand the reservation to align with
	 * the COW extent size hint if there's sufficient free space.
	 *
	 * Unlike the data fork, the CoW cancellation functions will free all
	 * the reservations at inactivation, so we don't require that every
	 * delalloc reservation have a dirty pagecache.
	 */
	if (use_cowextszhint) {
		struct xfs_bmbt_irec	prev;
		xfs_extlen_t		extsz = xfs_get_cowextsz_hint(ip);

		if (!xfs_iext_peek_prev_extent(ifp, icur, &prev))
			prev.br_startoff = NULLFILEOFF;

		error = xfs_bmap_extsize_align(mp, got, &prev, extsz, 0, eof,
					       1, 0, &aoff, &alen);
		ASSERT(!error);
	}

	/*
	 * Make a transaction-less quota reservation for delayed allocation
	 * blocks.  This number gets adjusted later.  We return if we haven't
	 * allocated blocks already inside this loop.
	 */
	error = xfs_quota_reserve_blkres(ip, alen);
	if (error)
		goto out;

	/*
	 * Split changing sb for alen and indlen since they could be coming
	 * from different places.
	 */
	indlen = (xfs_extlen_t)xfs_bmap_worst_indlen(ip, alen);
	ASSERT(indlen > 0);

	fdblocks = indlen;
	if (XFS_IS_REALTIME_INODE(ip)) {
		ASSERT(!xfs_is_zoned_inode(ip));
		error = xfs_dec_frextents(mp, xfs_blen_to_rtbxlen(mp, alen));
		if (error)
			goto out_unreserve_quota;
	} else {
		fdblocks += alen;
	}

	error = xfs_dec_fdblocks(mp, fdblocks, false);
	if (error)
		goto out_unreserve_frextents;

	ip->i_delayed_blks += alen;
	xfs_mod_delalloc(ip, alen, indlen);

	got->br_startoff = aoff;
	got->br_startblock = nullstartblock(indlen);
	got->br_blockcount = alen;
	got->br_state = XFS_EXT_NORM;

	xfs_bmap_add_extent_hole_delay(ip, whichfork, icur, got);

	/*
	 * Tag the inode if blocks were preallocated. Note that COW fork
	 * preallocation can occur at the start or end of the extent, even when
	 * prealloc == 0, so we must also check the aligned offset and length.
	 */
	if (whichfork == XFS_DATA_FORK && prealloc)
		xfs_inode_set_eofblocks_tag(ip);
	if (whichfork == XFS_COW_FORK && (prealloc || aoff < off || alen > len))
		xfs_inode_set_cowblocks_tag(ip);

	return 0;

out_unreserve_frextents:
	if (XFS_IS_REALTIME_INODE(ip))
		xfs_add_frextents(mp, xfs_blen_to_rtbxlen(mp, alen));
out_unreserve_quota:
	if (XFS_IS_QUOTA_ON(mp))
		xfs_quota_unreserve_blkres(ip, alen);
out:
	if (error == -ENOSPC || error == -EDQUOT) {
		trace_xfs_delalloc_enospc(ip, off, len);

		if (prealloc || use_cowextszhint) {
			/* retry without any preallocation */
			use_cowextszhint = false;
			prealloc = 0;
			goto retry;
		}
	}
	return error;
}

static int
xfs_zoned_buffered_write_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			count,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct iomap_iter	*iter =
		container_of(iomap, struct iomap_iter, iomap);
	struct xfs_zone_alloc_ctx *ac = iter->private;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = xfs_iomap_end_fsb(mp, offset, count);
	u16			iomap_flags = IOMAP_F_SHARED;
	unsigned int		lockmode = XFS_ILOCK_EXCL;
	xfs_filblks_t		count_fsb;
	xfs_extlen_t		indlen;
	struct xfs_bmbt_irec	got;
	struct xfs_iext_cursor	icur;
	int			error = 0;

	ASSERT(!xfs_get_extsz_hint(ip));
	ASSERT(!(flags & IOMAP_UNSHARE));
	ASSERT(ac);

	if (xfs_is_shutdown(mp))
		return -EIO;

	error = xfs_qm_dqattach(ip);
	if (error)
		return error;

	error = xfs_ilock_for_iomap(ip, flags, &lockmode);
	if (error)
		return error;

	if (XFS_IS_CORRUPT(mp, !xfs_ifork_has_extents(&ip->i_df)) ||
	    XFS_TEST_ERROR(mp, XFS_ERRTAG_BMAPIFORMAT)) {
		xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
		error = -EFSCORRUPTED;
		goto out_unlock;
	}

	XFS_STATS_INC(mp, xs_blk_mapw);

	error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
	if (error)
		goto out_unlock;

	/*
	 * For zeroing operations check if there is any data to zero first.
	 *
	 * For regular writes we always need to allocate new blocks, but need to
	 * provide the source mapping when the range is unaligned to support
	 * read-modify-write of the whole block in the page cache.
	 *
	 * In either case we need to limit the reported range to the boundaries
	 * of the source map in the data fork.
	 */
	if (!IS_ALIGNED(offset, mp->m_sb.sb_blocksize) ||
	    !IS_ALIGNED(offset + count, mp->m_sb.sb_blocksize) ||
	    (flags & IOMAP_ZERO)) {
		struct xfs_bmbt_irec	smap;
		struct xfs_iext_cursor	scur;

		if (!xfs_iext_lookup_extent(ip, &ip->i_df, offset_fsb, &scur,
				&smap))
			smap.br_startoff = end_fsb; /* fake hole until EOF */
		if (smap.br_startoff > offset_fsb) {
			/*
			 * We never need to allocate blocks for zeroing a hole.
			 */
			if (flags & IOMAP_ZERO) {
				xfs_hole_to_iomap(ip, iomap, offset_fsb,
						smap.br_startoff);
				goto out_unlock;
			}
			end_fsb = min(end_fsb, smap.br_startoff);
		} else {
			end_fsb = min(end_fsb,
				smap.br_startoff + smap.br_blockcount);
			xfs_trim_extent(&smap, offset_fsb,
					end_fsb - offset_fsb);
			error = xfs_bmbt_to_iomap(ip, srcmap, &smap, flags, 0,
					xfs_iomap_inode_sequence(ip, 0));
			if (error)
				goto out_unlock;
		}
	}

	if (!ip->i_cowfp)
		xfs_ifork_init_cow(ip);

	if (!xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &got))
		got.br_startoff = end_fsb;
	if (got.br_startoff <= offset_fsb) {
		trace_xfs_reflink_cow_found(ip, &got);
		goto done;
	}

	/*
	 * Cap the maximum length to keep the chunks of work done here somewhat
	 * symmetric with the work writeback does.
	 */
	end_fsb = min(end_fsb, got.br_startoff);
	count_fsb = min3(end_fsb - offset_fsb, XFS_MAX_BMBT_EXTLEN,
			 XFS_B_TO_FSB(mp, 1024 * PAGE_SIZE));

	/*
	 * The block reservation is supposed to cover all blocks that the
	 * operation could possible write, but there is a nasty corner case
	 * where blocks could be stolen from underneath us:
	 *
	 *  1) while this thread iterates over a larger buffered write,
	 *  2) another thread is causing a write fault that calls into
	 *     ->page_mkwrite in range this thread writes to, using up the
	 *     delalloc reservation created by a previous call to this function.
	 *  3) another thread does direct I/O on the range that the write fault
	 *     happened on, which causes writeback of the dirty data.
	 *  4) this then set the stale flag, which cuts the current iomap
	 *     iteration short, causing the new call to ->iomap_begin that gets
	 *     us here again, but now without a sufficient reservation.
	 *
	 * This is a very unusual I/O pattern, and nothing but generic/095 is
	 * known to hit it. There's not really much we can do here, so turn this
	 * into a short write.
	 */
	if (count_fsb > ac->reserved_blocks) {
		xfs_warn_ratelimited(mp,
"Short write on ino 0x%llx comm %.20s due to three-way race with write fault and direct I/O",
			ip->i_ino, current->comm);
		count_fsb = ac->reserved_blocks;
		if (!count_fsb) {
			error = -EIO;
			goto out_unlock;
		}
	}

	error = xfs_quota_reserve_blkres(ip, count_fsb);
	if (error)
		goto out_unlock;

	indlen = xfs_bmap_worst_indlen(ip, count_fsb);
	error = xfs_dec_fdblocks(mp, indlen, false);
	if (error)
		goto out_unlock;
	ip->i_delayed_blks += count_fsb;
	xfs_mod_delalloc(ip, count_fsb, indlen);

	got.br_startoff = offset_fsb;
	got.br_startblock = nullstartblock(indlen);
	got.br_blockcount = count_fsb;
	got.br_state = XFS_EXT_NORM;
	xfs_bmap_add_extent_hole_delay(ip, XFS_COW_FORK, &icur, &got);
	ac->reserved_blocks -= count_fsb;
	iomap_flags |= IOMAP_F_NEW;

	trace_xfs_iomap_alloc(ip, offset, XFS_FSB_TO_B(mp, count_fsb),
			XFS_COW_FORK, &got);
done:
	error = xfs_bmbt_to_iomap(ip, iomap, &got, flags, iomap_flags,
			xfs_iomap_inode_sequence(ip, IOMAP_F_SHARED));
out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;
}

static int
xfs_buffered_write_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			count,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = xfs_iomap_end_fsb(mp, offset, count);
	struct xfs_bmbt_irec	imap, cmap;
	struct xfs_iext_cursor	icur, ccur;
	xfs_fsblock_t		prealloc_blocks = 0;
	bool			eof = false, cow_eof = false, shared = false;
	int			allocfork = XFS_DATA_FORK;
	int			error = 0;
	unsigned int		lockmode = XFS_ILOCK_EXCL;
	unsigned int		iomap_flags = 0;
	u64			seq;

	if (xfs_is_shutdown(mp))
		return -EIO;

	if (xfs_is_zoned_inode(ip))
		return xfs_zoned_buffered_write_iomap_begin(inode, offset,
				count, flags, iomap, srcmap);

	/* we can't use delayed allocations when using extent size hints */
	if (xfs_get_extsz_hint(ip))
		return xfs_direct_write_iomap_begin(inode, offset, count,
				flags, iomap, srcmap);

	error = xfs_qm_dqattach(ip);
	if (error)
		return error;

	error = xfs_ilock_for_iomap(ip, flags, &lockmode);
	if (error)
		return error;

	if (XFS_IS_CORRUPT(mp, !xfs_ifork_has_extents(&ip->i_df)) ||
	    XFS_TEST_ERROR(mp, XFS_ERRTAG_BMAPIFORMAT)) {
		xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
		error = -EFSCORRUPTED;
		goto out_unlock;
	}

	XFS_STATS_INC(mp, xs_blk_mapw);

	error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
	if (error)
		goto out_unlock;

	/*
	 * Search the data fork first to look up our source mapping.  We
	 * always need the data fork map, as we have to return it to the
	 * iomap code so that the higher level write code can read data in to
	 * perform read-modify-write cycles for unaligned writes.
	 */
	eof = !xfs_iext_lookup_extent(ip, &ip->i_df, offset_fsb, &icur, &imap);
	if (eof)
		imap.br_startoff = end_fsb; /* fake hole until the end */

	/* We never need to allocate blocks for zeroing or unsharing a hole. */
	if ((flags & (IOMAP_UNSHARE | IOMAP_ZERO)) &&
	    imap.br_startoff > offset_fsb) {
		xfs_hole_to_iomap(ip, iomap, offset_fsb, imap.br_startoff);
		goto out_unlock;
	}

	/*
	 * For zeroing, trim a delalloc extent that extends beyond the EOF
	 * block.  If it starts beyond the EOF block, convert it to an
	 * unwritten extent.
	 */
	if ((flags & IOMAP_ZERO) && imap.br_startoff <= offset_fsb &&
	    isnullstartblock(imap.br_startblock)) {
		xfs_fileoff_t eof_fsb = XFS_B_TO_FSB(mp, XFS_ISIZE(ip));

		if (offset_fsb >= eof_fsb)
			goto convert_delay;
		if (end_fsb > eof_fsb) {
			end_fsb = eof_fsb;
			xfs_trim_extent(&imap, offset_fsb,
					end_fsb - offset_fsb);
		}
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
			goto found_cow;
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
			goto found_imap;
		}

		xfs_trim_extent(&imap, offset_fsb, end_fsb - offset_fsb);

		/* Trim the mapping to the nearest shared extent boundary. */
		error = xfs_bmap_trim_cow(ip, &imap, &shared);
		if (error)
			goto out_unlock;

		/* Not shared?  Just report the (potentially capped) extent. */
		if (!shared) {
			trace_xfs_iomap_found(ip, offset, count, XFS_DATA_FORK,
					&imap);
			goto found_imap;
		}

		/*
		 * Fork all the shared blocks from our write offset until the
		 * end of the extent.
		 */
		allocfork = XFS_COW_FORK;
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
		end_fsb = xfs_iomap_end_fsb(mp, offset, count);

		if (xfs_is_always_cow_inode(ip))
			allocfork = XFS_COW_FORK;
	}

	if (eof && offset + count > XFS_ISIZE(ip)) {
		/*
		 * Determine the initial size of the preallocation.
		 * We clean up any extra preallocation when the file is closed.
		 */
		if (xfs_has_allocsize(mp))
			prealloc_blocks = mp->m_allocsize_blocks;
		else if (allocfork == XFS_DATA_FORK)
			prealloc_blocks = xfs_iomap_prealloc_size(ip, allocfork,
						offset, count, &icur);
		else
			prealloc_blocks = xfs_iomap_prealloc_size(ip, allocfork,
						offset, count, &ccur);
		if (prealloc_blocks) {
			xfs_extlen_t	align;
			xfs_off_t	end_offset;
			xfs_fileoff_t	p_end_fsb;

			end_offset = XFS_ALLOC_ALIGN(mp, offset + count - 1);
			p_end_fsb = XFS_B_TO_FSBT(mp, end_offset) +
					prealloc_blocks;

			align = xfs_eof_alignment(ip);
			if (align)
				p_end_fsb = roundup_64(p_end_fsb, align);

			p_end_fsb = min(p_end_fsb,
				XFS_B_TO_FSB(mp, mp->m_super->s_maxbytes));
			ASSERT(p_end_fsb > offset_fsb);
			prealloc_blocks = p_end_fsb - end_fsb;
		}
	}

	/*
	 * Flag newly allocated delalloc blocks with IOMAP_F_NEW so we punch
	 * them out if the write happens to fail.
	 */
	iomap_flags |= IOMAP_F_NEW;
	if (allocfork == XFS_COW_FORK) {
		error = xfs_bmapi_reserve_delalloc(ip, allocfork, offset_fsb,
				end_fsb - offset_fsb, prealloc_blocks, &cmap,
				&ccur, cow_eof);
		if (error)
			goto out_unlock;

		trace_xfs_iomap_alloc(ip, offset, count, allocfork, &cmap);
		goto found_cow;
	}

	error = xfs_bmapi_reserve_delalloc(ip, allocfork, offset_fsb,
			end_fsb - offset_fsb, prealloc_blocks, &imap, &icur,
			eof);
	if (error)
		goto out_unlock;

	trace_xfs_iomap_alloc(ip, offset, count, allocfork, &imap);
found_imap:
	seq = xfs_iomap_inode_sequence(ip, iomap_flags);
	xfs_iunlock(ip, lockmode);
	return xfs_bmbt_to_iomap(ip, iomap, &imap, flags, iomap_flags, seq);

convert_delay:
	xfs_iunlock(ip, lockmode);
	truncate_pagecache(inode, offset);
	error = xfs_bmapi_convert_delalloc(ip, XFS_DATA_FORK, offset,
					   iomap, NULL);
	if (error)
		return error;

	trace_xfs_iomap_alloc(ip, offset, count, XFS_DATA_FORK, &imap);
	return 0;

found_cow:
	if (imap.br_startoff <= offset_fsb) {
		error = xfs_bmbt_to_iomap(ip, srcmap, &imap, flags, 0,
				xfs_iomap_inode_sequence(ip, 0));
		if (error)
			goto out_unlock;
	} else {
		xfs_trim_extent(&cmap, offset_fsb,
				imap.br_startoff - offset_fsb);
	}

	iomap_flags |= IOMAP_F_SHARED;
	seq = xfs_iomap_inode_sequence(ip, iomap_flags);
	xfs_iunlock(ip, lockmode);
	return xfs_bmbt_to_iomap(ip, iomap, &cmap, flags, iomap_flags, seq);

out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;
}

static void
xfs_buffered_write_delalloc_punch(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	struct iomap		*iomap)
{
	struct iomap_iter	*iter =
		container_of(iomap, struct iomap_iter, iomap);

	xfs_bmap_punch_delalloc_range(XFS_I(inode),
			(iomap->flags & IOMAP_F_SHARED) ?
				XFS_COW_FORK : XFS_DATA_FORK,
			offset, offset + length, iter->private);
}

static int
xfs_buffered_write_iomap_end(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	ssize_t			written,
	unsigned		flags,
	struct iomap		*iomap)
{
	loff_t			start_byte, end_byte;

	/* If we didn't reserve the blocks, we're not allowed to punch them. */
	if (iomap->type != IOMAP_DELALLOC || !(iomap->flags & IOMAP_F_NEW))
		return 0;

	/*
	 * iomap_page_mkwrite() will never fail in a way that requires delalloc
	 * extents that it allocated to be revoked.  Hence never try to release
	 * them here.
	 */
	if (flags & IOMAP_FAULT)
		return 0;

	/* Nothing to do if we've written the entire delalloc extent */
	start_byte = iomap_last_written_block(inode, offset, written);
	end_byte = round_up(offset + length, i_blocksize(inode));
	if (start_byte >= end_byte)
		return 0;

	/* For zeroing operations the callers already hold invalidate_lock. */
	if (flags & (IOMAP_UNSHARE | IOMAP_ZERO)) {
		rwsem_assert_held_write(&inode->i_mapping->invalidate_lock);
		iomap_write_delalloc_release(inode, start_byte, end_byte, flags,
				iomap, xfs_buffered_write_delalloc_punch);
	} else {
		filemap_invalidate_lock(inode->i_mapping);
		iomap_write_delalloc_release(inode, start_byte, end_byte, flags,
				iomap, xfs_buffered_write_delalloc_punch);
		filemap_invalidate_unlock(inode->i_mapping);
	}

	return 0;
}

const struct iomap_ops xfs_buffered_write_iomap_ops = {
	.iomap_begin		= xfs_buffered_write_iomap_begin,
	.iomap_end		= xfs_buffered_write_iomap_end,
};

static int
xfs_read_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	imap;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = xfs_iomap_end_fsb(mp, offset, length);
	int			nimaps = 1, error = 0;
	bool			shared = false;
	unsigned int		lockmode = XFS_ILOCK_SHARED;
	u64			seq;

	ASSERT(!(flags & (IOMAP_WRITE | IOMAP_ZERO)));

	if (xfs_is_shutdown(mp))
		return -EIO;

	error = xfs_ilock_for_iomap(ip, flags, &lockmode);
	if (error)
		return error;
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, 0);
	if (!error && ((flags & IOMAP_REPORT) || IS_DAX(inode)))
		error = xfs_reflink_trim_around_shared(ip, &imap, &shared);
	seq = xfs_iomap_inode_sequence(ip, shared ? IOMAP_F_SHARED : 0);
	xfs_iunlock(ip, lockmode);

	if (error)
		return error;
	trace_xfs_iomap_found(ip, offset, length, XFS_DATA_FORK, &imap);
	return xfs_bmbt_to_iomap(ip, iomap, &imap, flags,
				 shared ? IOMAP_F_SHARED : 0, seq);
}

const struct iomap_ops xfs_read_iomap_ops = {
	.iomap_begin		= xfs_read_iomap_begin,
};

static int
xfs_seek_iomap_begin(
	struct inode		*inode,
	loff_t			offset,
	loff_t			length,
	unsigned		flags,
	struct iomap		*iomap,
	struct iomap		*srcmap)
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
	u64			seq;

	if (xfs_is_shutdown(mp))
		return -EIO;

	lockmode = xfs_ilock_data_map_shared(ip);
	error = xfs_iread_extents(NULL, ip, XFS_DATA_FORK);
	if (error)
		goto out_unlock;

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
		data_fsb = xfs_iomap_end_fsb(mp, offset, length);
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
		xfs_trim_extent(&cmap, offset_fsb, end_fsb - offset_fsb);
		seq = xfs_iomap_inode_sequence(ip, IOMAP_F_SHARED);
		error = xfs_bmbt_to_iomap(ip, iomap, &cmap, flags,
				IOMAP_F_SHARED, seq);
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
	seq = xfs_iomap_inode_sequence(ip, 0);
	xfs_trim_extent(&imap, offset_fsb, end_fsb - offset_fsb);
	error = xfs_bmbt_to_iomap(ip, iomap, &imap, flags, 0, seq);
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
	struct iomap		*iomap,
	struct iomap		*srcmap)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + length);
	struct xfs_bmbt_irec	imap;
	int			nimaps = 1, error = 0;
	unsigned		lockmode;
	int			seq;

	if (xfs_is_shutdown(mp))
		return -EIO;

	lockmode = xfs_ilock_attr_map_shared(ip);

	/* if there are no attribute fork or extents, return ENOENT */
	if (!xfs_inode_has_attr_fork(ip) || !ip->i_af.if_nextents) {
		error = -ENOENT;
		goto out_unlock;
	}

	ASSERT(ip->i_af.if_format != XFS_DINODE_FMT_LOCAL);
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb, &imap,
			       &nimaps, XFS_BMAPI_ATTRFORK);
out_unlock:

	seq = xfs_iomap_inode_sequence(ip, IOMAP_F_XATTR);
	xfs_iunlock(ip, lockmode);

	if (error)
		return error;
	ASSERT(nimaps);
	return xfs_bmbt_to_iomap(ip, iomap, &imap, flags, IOMAP_F_XATTR, seq);
}

const struct iomap_ops xfs_xattr_iomap_ops = {
	.iomap_begin		= xfs_xattr_iomap_begin,
};

int
xfs_zero_range(
	struct xfs_inode	*ip,
	loff_t			pos,
	loff_t			len,
	struct xfs_zone_alloc_ctx *ac,
	bool			*did_zero)
{
	struct inode		*inode = VFS_I(ip);

	xfs_assert_ilocked(ip, XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL);

	if (IS_DAX(inode))
		return dax_zero_range(inode, pos, len, did_zero,
				      &xfs_dax_write_iomap_ops);
	return iomap_zero_range(inode, pos, len, did_zero,
			&xfs_buffered_write_iomap_ops, &xfs_iomap_write_ops,
			ac);
}

int
xfs_truncate_page(
	struct xfs_inode	*ip,
	loff_t			pos,
	struct xfs_zone_alloc_ctx *ac,
	bool			*did_zero)
{
	struct inode		*inode = VFS_I(ip);

	if (IS_DAX(inode))
		return dax_truncate_page(inode, pos, did_zero,
					&xfs_dax_write_iomap_ops);
	return iomap_truncate_page(inode, pos, did_zero,
			&xfs_buffered_write_iomap_ops, &xfs_iomap_write_ops,
			ac);
}
