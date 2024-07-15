// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_trace.h"
#include "xfs_icache.h"
#include "xfs_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_refcount.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_bit.h"
#include "xfs_alloc.h"
#include "xfs_quota.h"
#include "xfs_reflink.h"
#include "xfs_iomap.h"
#include "xfs_ag.h"
#include "xfs_ag_resv.h"
#include "xfs_health.h"

/*
 * Copy on Write of Shared Blocks
 *
 * XFS must preserve "the usual" file semantics even when two files share
 * the same physical blocks.  This means that a write to one file must not
 * alter the blocks in a different file; the way that we'll do that is
 * through the use of a copy-on-write mechanism.  At a high level, that
 * means that when we want to write to a shared block, we allocate a new
 * block, write the data to the new block, and if that succeeds we map the
 * new block into the file.
 *
 * XFS provides a "delayed allocation" mechanism that defers the allocation
 * of disk blocks to dirty-but-not-yet-mapped file blocks as long as
 * possible.  This reduces fragmentation by enabling the filesystem to ask
 * for bigger chunks less often, which is exactly what we want for CoW.
 *
 * The delalloc mechanism begins when the kernel wants to make a block
 * writable (write_begin or page_mkwrite).  If the offset is not mapped, we
 * create a delalloc mapping, which is a regular in-core extent, but without
 * a real startblock.  (For delalloc mappings, the startblock encodes both
 * a flag that this is a delalloc mapping, and a worst-case estimate of how
 * many blocks might be required to put the mapping into the BMBT.)  delalloc
 * mappings are a reservation against the free space in the filesystem;
 * adjacent mappings can also be combined into fewer larger mappings.
 *
 * As an optimization, the CoW extent size hint (cowextsz) creates
 * outsized aligned delalloc reservations in the hope of landing out of
 * order nearby CoW writes in a single extent on disk, thereby reducing
 * fragmentation and improving future performance.
 *
 * D: --RRRRRRSSSRRRRRRRR--- (data fork)
 * C: ------DDDDDDD--------- (CoW fork)
 *
 * When dirty pages are being written out (typically in writepage), the
 * delalloc reservations are converted into unwritten mappings by
 * allocating blocks and replacing the delalloc mapping with real ones.
 * A delalloc mapping can be replaced by several unwritten ones if the
 * free space is fragmented.
 *
 * D: --RRRRRRSSSRRRRRRRR---
 * C: ------UUUUUUU---------
 *
 * We want to adapt the delalloc mechanism for copy-on-write, since the
 * write paths are similar.  The first two steps (creating the reservation
 * and allocating the blocks) are exactly the same as delalloc except that
 * the mappings must be stored in a separate CoW fork because we do not want
 * to disturb the mapping in the data fork until we're sure that the write
 * succeeded.  IO completion in this case is the process of removing the old
 * mapping from the data fork and moving the new mapping from the CoW fork to
 * the data fork.  This will be discussed shortly.
 *
 * For now, unaligned directio writes will be bounced back to the page cache.
 * Block-aligned directio writes will use the same mechanism as buffered
 * writes.
 *
 * Just prior to submitting the actual disk write requests, we convert
 * the extents representing the range of the file actually being written
 * (as opposed to extra pieces created for the cowextsize hint) to real
 * extents.  This will become important in the next step:
 *
 * D: --RRRRRRSSSRRRRRRRR---
 * C: ------UUrrUUU---------
 *
 * CoW remapping must be done after the data block write completes,
 * because we don't want to destroy the old data fork map until we're sure
 * the new block has been written.  Since the new mappings are kept in a
 * separate fork, we can simply iterate these mappings to find the ones
 * that cover the file blocks that we just CoW'd.  For each extent, simply
 * unmap the corresponding range in the data fork, map the new range into
 * the data fork, and remove the extent from the CoW fork.  Because of
 * the presence of the cowextsize hint, however, we must be careful
 * only to remap the blocks that we've actually written out --  we must
 * never remap delalloc reservations nor CoW staging blocks that have
 * yet to be written.  This corresponds exactly to the real extents in
 * the CoW fork:
 *
 * D: --RRRRRRrrSRRRRRRRR---
 * C: ------UU--UUU---------
 *
 * Since the remapping operation can be applied to an arbitrary file
 * range, we record the need for the remap step as a flag in the ioend
 * instead of declaring a new IO type.  This is required for direct io
 * because we only have ioend for the whole dio, and we have to be able to
 * remember the presence of unwritten blocks and CoW blocks with a single
 * ioend structure.  Better yet, the more ground we can cover with one
 * ioend, the better.
 */

/*
 * Given an AG extent, find the lowest-numbered run of shared blocks
 * within that range and return the range in fbno/flen.  If
 * find_end_of_shared is true, return the longest contiguous extent of
 * shared blocks.  If there are no shared extents, fbno and flen will
 * be set to NULLAGBLOCK and 0, respectively.
 */
static int
xfs_reflink_find_shared(
	struct xfs_perag	*pag,
	struct xfs_trans	*tp,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	xfs_agblock_t		*fbno,
	xfs_extlen_t		*flen,
	bool			find_end_of_shared)
{
	struct xfs_buf		*agbp;
	struct xfs_btree_cur	*cur;
	int			error;

	error = xfs_alloc_read_agf(pag, tp, 0, &agbp);
	if (error)
		return error;

	cur = xfs_refcountbt_init_cursor(pag->pag_mount, tp, agbp, pag);

	error = xfs_refcount_find_shared(cur, agbno, aglen, fbno, flen,
			find_end_of_shared);

	xfs_btree_del_cursor(cur, error);

	xfs_trans_brelse(tp, agbp);
	return error;
}

/*
 * Trim the mapping to the next block where there's a change in the
 * shared/unshared status.  More specifically, this means that we
 * find the lowest-numbered extent of shared blocks that coincides with
 * the given block mapping.  If the shared extent overlaps the start of
 * the mapping, trim the mapping to the end of the shared extent.  If
 * the shared region intersects the mapping, trim the mapping to the
 * start of the shared extent.  If there are no shared regions that
 * overlap, just return the original extent.
 */
int
xfs_reflink_trim_around_shared(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*irec,
	bool			*shared)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_perag	*pag;
	xfs_agblock_t		agbno;
	xfs_extlen_t		aglen;
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	int			error = 0;

	/* Holes, unwritten, and delalloc extents cannot be shared */
	if (!xfs_is_cow_inode(ip) || !xfs_bmap_is_written_extent(irec)) {
		*shared = false;
		return 0;
	}

	trace_xfs_reflink_trim_around_shared(ip, irec);

	pag = xfs_perag_get(mp, XFS_FSB_TO_AGNO(mp, irec->br_startblock));
	agbno = XFS_FSB_TO_AGBNO(mp, irec->br_startblock);
	aglen = irec->br_blockcount;

	error = xfs_reflink_find_shared(pag, NULL, agbno, aglen, &fbno, &flen,
			true);
	xfs_perag_put(pag);
	if (error)
		return error;

	*shared = false;
	if (fbno == NULLAGBLOCK) {
		/* No shared blocks at all. */
		return 0;
	}

	if (fbno == agbno) {
		/*
		 * The start of this extent is shared.  Truncate the
		 * mapping at the end of the shared region so that a
		 * subsequent iteration starts at the start of the
		 * unshared region.
		 */
		irec->br_blockcount = flen;
		*shared = true;
		return 0;
	}

	/*
	 * There's a shared extent midway through this extent.
	 * Truncate the mapping at the start of the shared
	 * extent so that a subsequent iteration starts at the
	 * start of the shared region.
	 */
	irec->br_blockcount = fbno - agbno;
	return 0;
}

int
xfs_bmap_trim_cow(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	bool			*shared)
{
	/* We can't update any real extents in always COW mode. */
	if (xfs_is_always_cow_inode(ip) &&
	    !isnullstartblock(imap->br_startblock)) {
		*shared = true;
		return 0;
	}

	/* Trim the mapping to the nearest shared extent boundary. */
	return xfs_reflink_trim_around_shared(ip, imap, shared);
}

static int
xfs_reflink_convert_cow_locked(
	struct xfs_inode	*ip,
	xfs_fileoff_t		offset_fsb,
	xfs_filblks_t		count_fsb)
{
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	got;
	struct xfs_btree_cur	*dummy_cur = NULL;
	int			dummy_logflags;
	int			error = 0;

	if (!xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &got))
		return 0;

	do {
		if (got.br_startoff >= offset_fsb + count_fsb)
			break;
		if (got.br_state == XFS_EXT_NORM)
			continue;
		if (WARN_ON_ONCE(isnullstartblock(got.br_startblock)))
			return -EIO;

		xfs_trim_extent(&got, offset_fsb, count_fsb);
		if (!got.br_blockcount)
			continue;

		got.br_state = XFS_EXT_NORM;
		error = xfs_bmap_add_extent_unwritten_real(NULL, ip,
				XFS_COW_FORK, &icur, &dummy_cur, &got,
				&dummy_logflags);
		if (error)
			return error;
	} while (xfs_iext_next_extent(ip->i_cowfp, &icur, &got));

	return error;
}

/* Convert all of the unwritten CoW extents in a file's range to real ones. */
int
xfs_reflink_convert_cow(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		count)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + count);
	xfs_filblks_t		count_fsb = end_fsb - offset_fsb;
	int			error;

	ASSERT(count != 0);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	error = xfs_reflink_convert_cow_locked(ip, offset_fsb, count_fsb);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Find the extent that maps the given range in the COW fork. Even if the extent
 * is not shared we might have a preallocation for it in the COW fork. If so we
 * use it that rather than trigger a new allocation.
 */
static int
xfs_find_trim_cow_extent(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	struct xfs_bmbt_irec	*cmap,
	bool			*shared,
	bool			*found)
{
	xfs_fileoff_t		offset_fsb = imap->br_startoff;
	xfs_filblks_t		count_fsb = imap->br_blockcount;
	struct xfs_iext_cursor	icur;

	*found = false;

	/*
	 * If we don't find an overlapping extent, trim the range we need to
	 * allocate to fit the hole we found.
	 */
	if (!xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, cmap))
		cmap->br_startoff = offset_fsb + count_fsb;
	if (cmap->br_startoff > offset_fsb) {
		xfs_trim_extent(imap, imap->br_startoff,
				cmap->br_startoff - imap->br_startoff);
		return xfs_bmap_trim_cow(ip, imap, shared);
	}

	*shared = true;
	if (isnullstartblock(cmap->br_startblock)) {
		xfs_trim_extent(imap, cmap->br_startoff, cmap->br_blockcount);
		return 0;
	}

	/* real extent found - no need to allocate */
	xfs_trim_extent(cmap, offset_fsb, count_fsb);
	*found = true;
	return 0;
}

static int
xfs_reflink_convert_unwritten(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	struct xfs_bmbt_irec	*cmap,
	bool			convert_now)
{
	xfs_fileoff_t		offset_fsb = imap->br_startoff;
	xfs_filblks_t		count_fsb = imap->br_blockcount;
	int			error;

	/*
	 * cmap might larger than imap due to cowextsize hint.
	 */
	xfs_trim_extent(cmap, offset_fsb, count_fsb);

	/*
	 * COW fork extents are supposed to remain unwritten until we're ready
	 * to initiate a disk write.  For direct I/O we are going to write the
	 * data and need the conversion, but for buffered writes we're done.
	 */
	if (!convert_now || cmap->br_state == XFS_EXT_NORM)
		return 0;

	trace_xfs_reflink_convert_cow(ip, cmap);

	error = xfs_reflink_convert_cow_locked(ip, offset_fsb, count_fsb);
	if (!error)
		cmap->br_state = XFS_EXT_NORM;

	return error;
}

static int
xfs_reflink_fill_cow_hole(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	struct xfs_bmbt_irec	*cmap,
	bool			*shared,
	uint			*lockmode,
	bool			convert_now)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	xfs_filblks_t		resaligned;
	xfs_extlen_t		resblks;
	int			nimaps;
	int			error;
	bool			found;

	resaligned = xfs_aligned_fsb_count(imap->br_startoff,
		imap->br_blockcount, xfs_get_cowextsz_hint(ip));
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);

	xfs_iunlock(ip, *lockmode);
	*lockmode = 0;

	error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write, resblks, 0,
			false, &tp);
	if (error)
		return error;

	*lockmode = XFS_ILOCK_EXCL;

	error = xfs_find_trim_cow_extent(ip, imap, cmap, shared, &found);
	if (error || !*shared)
		goto out_trans_cancel;

	if (found) {
		xfs_trans_cancel(tp);
		goto convert;
	}

	/* Allocate the entire reservation as unwritten blocks. */
	nimaps = 1;
	error = xfs_bmapi_write(tp, ip, imap->br_startoff, imap->br_blockcount,
			XFS_BMAPI_COWFORK | XFS_BMAPI_PREALLOC, 0, cmap,
			&nimaps);
	if (error)
		goto out_trans_cancel;

	xfs_inode_set_cowblocks_tag(ip);
	error = xfs_trans_commit(tp);
	if (error)
		return error;

convert:
	return xfs_reflink_convert_unwritten(ip, imap, cmap, convert_now);

out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
}

static int
xfs_reflink_fill_delalloc(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	struct xfs_bmbt_irec	*cmap,
	bool			*shared,
	uint			*lockmode,
	bool			convert_now)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			nimaps;
	int			error;
	bool			found;

	do {
		xfs_iunlock(ip, *lockmode);
		*lockmode = 0;

		error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write, 0, 0,
				false, &tp);
		if (error)
			return error;

		*lockmode = XFS_ILOCK_EXCL;

		error = xfs_find_trim_cow_extent(ip, imap, cmap, shared,
				&found);
		if (error || !*shared)
			goto out_trans_cancel;

		if (found) {
			xfs_trans_cancel(tp);
			break;
		}

		ASSERT(isnullstartblock(cmap->br_startblock) ||
		       cmap->br_startblock == DELAYSTARTBLOCK);

		/*
		 * Replace delalloc reservation with an unwritten extent.
		 */
		nimaps = 1;
		error = xfs_bmapi_write(tp, ip, cmap->br_startoff,
				cmap->br_blockcount,
				XFS_BMAPI_COWFORK | XFS_BMAPI_PREALLOC, 0,
				cmap, &nimaps);
		if (error)
			goto out_trans_cancel;

		xfs_inode_set_cowblocks_tag(ip);
		error = xfs_trans_commit(tp);
		if (error)
			return error;
	} while (cmap->br_startoff + cmap->br_blockcount <= imap->br_startoff);

	return xfs_reflink_convert_unwritten(ip, imap, cmap, convert_now);

out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
}

/* Allocate all CoW reservations covering a range of blocks in a file. */
int
xfs_reflink_allocate_cow(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	struct xfs_bmbt_irec	*cmap,
	bool			*shared,
	uint			*lockmode,
	bool			convert_now)
{
	int			error;
	bool			found;

	xfs_assert_ilocked(ip, XFS_ILOCK_EXCL);
	if (!ip->i_cowfp) {
		ASSERT(!xfs_is_reflink_inode(ip));
		xfs_ifork_init_cow(ip);
	}

	error = xfs_find_trim_cow_extent(ip, imap, cmap, shared, &found);
	if (error || !*shared)
		return error;

	/* CoW fork has a real extent */
	if (found)
		return xfs_reflink_convert_unwritten(ip, imap, cmap,
				convert_now);

	/*
	 * CoW fork does not have an extent and data extent is shared.
	 * Allocate a real extent in the CoW fork.
	 */
	if (cmap->br_startoff > imap->br_startoff)
		return xfs_reflink_fill_cow_hole(ip, imap, cmap, shared,
				lockmode, convert_now);

	/*
	 * CoW fork has a delalloc reservation. Replace it with a real extent.
	 * There may or may not be a data fork mapping.
	 */
	if (isnullstartblock(cmap->br_startblock) ||
	    cmap->br_startblock == DELAYSTARTBLOCK)
		return xfs_reflink_fill_delalloc(ip, imap, cmap, shared,
				lockmode, convert_now);

	/* Shouldn't get here. */
	ASSERT(0);
	return -EFSCORRUPTED;
}

/*
 * Cancel CoW reservations for some block range of an inode.
 *
 * If cancel_real is true this function cancels all COW fork extents for the
 * inode; if cancel_real is false, real extents are not cleared.
 *
 * Caller must have already joined the inode to the current transaction. The
 * inode will be joined to the transaction returned to the caller.
 */
int
xfs_reflink_cancel_cow_blocks(
	struct xfs_inode		*ip,
	struct xfs_trans		**tpp,
	xfs_fileoff_t			offset_fsb,
	xfs_fileoff_t			end_fsb,
	bool				cancel_real)
{
	struct xfs_ifork		*ifp = xfs_ifork_ptr(ip, XFS_COW_FORK);
	struct xfs_bmbt_irec		got, del;
	struct xfs_iext_cursor		icur;
	int				error = 0;

	if (!xfs_inode_has_cow_data(ip))
		return 0;
	if (!xfs_iext_lookup_extent_before(ip, ifp, &end_fsb, &icur, &got))
		return 0;

	/* Walk backwards until we're out of the I/O range... */
	while (got.br_startoff + got.br_blockcount > offset_fsb) {
		del = got;
		xfs_trim_extent(&del, offset_fsb, end_fsb - offset_fsb);

		/* Extent delete may have bumped ext forward */
		if (!del.br_blockcount) {
			xfs_iext_prev(ifp, &icur);
			goto next_extent;
		}

		trace_xfs_reflink_cancel_cow(ip, &del);

		if (isnullstartblock(del.br_startblock)) {
			xfs_bmap_del_extent_delay(ip, XFS_COW_FORK, &icur, &got,
					&del);
		} else if (del.br_state == XFS_EXT_UNWRITTEN || cancel_real) {
			ASSERT((*tpp)->t_highest_agno == NULLAGNUMBER);

			/* Free the CoW orphan record. */
			xfs_refcount_free_cow_extent(*tpp, del.br_startblock,
					del.br_blockcount);

			error = xfs_free_extent_later(*tpp, del.br_startblock,
					del.br_blockcount, NULL,
					XFS_AG_RESV_NONE, false);
			if (error)
				break;

			/* Roll the transaction */
			error = xfs_defer_finish(tpp);
			if (error)
				break;

			/* Remove the mapping from the CoW fork. */
			xfs_bmap_del_extent_cow(ip, &icur, &got, &del);

			/* Remove the quota reservation */
			xfs_quota_unreserve_blkres(ip, del.br_blockcount);
		} else {
			/* Didn't do anything, push cursor back. */
			xfs_iext_prev(ifp, &icur);
		}
next_extent:
		if (!xfs_iext_get_extent(ifp, &icur, &got))
			break;
	}

	/* clear tag if cow fork is emptied */
	if (!ifp->if_bytes)
		xfs_inode_clear_cowblocks_tag(ip);
	return error;
}

/*
 * Cancel CoW reservations for some byte range of an inode.
 *
 * If cancel_real is true this function cancels all COW fork extents for the
 * inode; if cancel_real is false, real extents are not cleared.
 */
int
xfs_reflink_cancel_cow_range(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		count,
	bool			cancel_real)
{
	struct xfs_trans	*tp;
	xfs_fileoff_t		offset_fsb;
	xfs_fileoff_t		end_fsb;
	int			error;

	trace_xfs_reflink_cancel_cow_range(ip, offset, count);
	ASSERT(ip->i_cowfp);

	offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
	if (count == NULLFILEOFF)
		end_fsb = NULLFILEOFF;
	else
		end_fsb = XFS_B_TO_FSB(ip->i_mount, offset + count);

	/* Start a rolling transaction to remove the mappings */
	error = xfs_trans_alloc(ip->i_mount, &M_RES(ip->i_mount)->tr_write,
			0, 0, 0, &tp);
	if (error)
		goto out;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/* Scrape out the old CoW reservations */
	error = xfs_reflink_cancel_cow_blocks(ip, &tp, offset_fsb, end_fsb,
			cancel_real);
	if (error)
		goto out_cancel;

	error = xfs_trans_commit(tp);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;

out_cancel:
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out:
	trace_xfs_reflink_cancel_cow_range_error(ip, error, _RET_IP_);
	return error;
}

/*
 * Remap part of the CoW fork into the data fork.
 *
 * We aim to remap the range starting at @offset_fsb and ending at @end_fsb
 * into the data fork; this function will remap what it can (at the end of the
 * range) and update @end_fsb appropriately.  Each remap gets its own
 * transaction because we can end up merging and splitting bmbt blocks for
 * every remap operation and we'd like to keep the block reservation
 * requirements as low as possible.
 */
STATIC int
xfs_reflink_end_cow_extent(
	struct xfs_inode	*ip,
	xfs_fileoff_t		*offset_fsb,
	xfs_fileoff_t		end_fsb)
{
	struct xfs_iext_cursor	icur;
	struct xfs_bmbt_irec	got, del, data;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct xfs_ifork	*ifp = xfs_ifork_ptr(ip, XFS_COW_FORK);
	unsigned int		resblks;
	int			nmaps;
	int			error;

	resblks = XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0,
			XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	/*
	 * Lock the inode.  We have to ijoin without automatic unlock because
	 * the lead transaction is the refcountbt record deletion; the data
	 * fork update follows as a deferred log item.
	 */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * In case of racing, overlapping AIO writes no COW extents might be
	 * left by the time I/O completes for the loser of the race.  In that
	 * case we are done.
	 */
	if (!xfs_iext_lookup_extent(ip, ifp, *offset_fsb, &icur, &got) ||
	    got.br_startoff >= end_fsb) {
		*offset_fsb = end_fsb;
		goto out_cancel;
	}

	/*
	 * Only remap real extents that contain data.  With AIO, speculative
	 * preallocations can leak into the range we are called upon, and we
	 * need to skip them.  Preserve @got for the eventual CoW fork
	 * deletion; from now on @del represents the mapping that we're
	 * actually remapping.
	 */
	while (!xfs_bmap_is_written_extent(&got)) {
		if (!xfs_iext_next_extent(ifp, &icur, &got) ||
		    got.br_startoff >= end_fsb) {
			*offset_fsb = end_fsb;
			goto out_cancel;
		}
	}
	del = got;
	xfs_trim_extent(&del, *offset_fsb, end_fsb - *offset_fsb);

	error = xfs_iext_count_extend(tp, ip, XFS_DATA_FORK,
			XFS_IEXT_REFLINK_END_COW_CNT);
	if (error)
		goto out_cancel;

	/* Grab the corresponding mapping in the data fork. */
	nmaps = 1;
	error = xfs_bmapi_read(ip, del.br_startoff, del.br_blockcount, &data,
			&nmaps, 0);
	if (error)
		goto out_cancel;

	/* We can only remap the smaller of the two extent sizes. */
	data.br_blockcount = min(data.br_blockcount, del.br_blockcount);
	del.br_blockcount = data.br_blockcount;

	trace_xfs_reflink_cow_remap_from(ip, &del);
	trace_xfs_reflink_cow_remap_to(ip, &data);

	if (xfs_bmap_is_real_extent(&data)) {
		/*
		 * If the extent we're remapping is backed by storage (written
		 * or not), unmap the extent and drop its refcount.
		 */
		xfs_bmap_unmap_extent(tp, ip, XFS_DATA_FORK, &data);
		xfs_refcount_decrease_extent(tp, &data);
		xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_BCOUNT,
				-data.br_blockcount);
	} else if (data.br_startblock == DELAYSTARTBLOCK) {
		int		done;

		/*
		 * If the extent we're remapping is a delalloc reservation,
		 * we can use the regular bunmapi function to release the
		 * incore state.  Dropping the delalloc reservation takes care
		 * of the quota reservation for us.
		 */
		error = xfs_bunmapi(NULL, ip, data.br_startoff,
				data.br_blockcount, 0, 1, &done);
		if (error)
			goto out_cancel;
		ASSERT(done);
	}

	/* Free the CoW orphan record. */
	xfs_refcount_free_cow_extent(tp, del.br_startblock, del.br_blockcount);

	/* Map the new blocks into the data fork. */
	xfs_bmap_map_extent(tp, ip, XFS_DATA_FORK, &del);

	/* Charge this new data fork mapping to the on-disk quota. */
	xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_DELBCOUNT,
			(long)del.br_blockcount);

	/* Remove the mapping from the CoW fork. */
	xfs_bmap_del_extent_cow(ip, &icur, &got, &del);

	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		return error;

	/* Update the caller about how much progress we made. */
	*offset_fsb = del.br_startoff + del.br_blockcount;
	return 0;

out_cancel:
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Remap parts of a file's data fork after a successful CoW.
 */
int
xfs_reflink_end_cow(
	struct xfs_inode		*ip,
	xfs_off_t			offset,
	xfs_off_t			count)
{
	xfs_fileoff_t			offset_fsb;
	xfs_fileoff_t			end_fsb;
	int				error = 0;

	trace_xfs_reflink_end_cow(ip, offset, count);

	offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
	end_fsb = XFS_B_TO_FSB(ip->i_mount, offset + count);

	/*
	 * Walk forwards until we've remapped the I/O range.  The loop function
	 * repeatedly cycles the ILOCK to allocate one transaction per remapped
	 * extent.
	 *
	 * If we're being called by writeback then the pages will still
	 * have PageWriteback set, which prevents races with reflink remapping
	 * and truncate.  Reflink remapping prevents races with writeback by
	 * taking the iolock and mmaplock before flushing the pages and
	 * remapping, which means there won't be any further writeback or page
	 * cache dirtying until the reflink completes.
	 *
	 * We should never have two threads issuing writeback for the same file
	 * region.  There are also have post-eof checks in the writeback
	 * preparation code so that we don't bother writing out pages that are
	 * about to be truncated.
	 *
	 * If we're being called as part of directio write completion, the dio
	 * count is still elevated, which reflink and truncate will wait for.
	 * Reflink remapping takes the iolock and mmaplock and waits for
	 * pending dio to finish, which should prevent any directio until the
	 * remap completes.  Multiple concurrent directio writes to the same
	 * region are handled by end_cow processing only occurring for the
	 * threads which succeed; the outcome of multiple overlapping direct
	 * writes is not well defined anyway.
	 *
	 * It's possible that a buffered write and a direct write could collide
	 * here (the buffered write stumbles in after the dio flushes and
	 * invalidates the page cache and immediately queues writeback), but we
	 * have never supported this 100%.  If either disk write succeeds the
	 * blocks will be remapped.
	 */
	while (end_fsb > offset_fsb && !error)
		error = xfs_reflink_end_cow_extent(ip, &offset_fsb, end_fsb);

	if (error)
		trace_xfs_reflink_end_cow_error(ip, error, _RET_IP_);
	return error;
}

/*
 * Free all CoW staging blocks that are still referenced by the ondisk refcount
 * metadata.  The ondisk metadata does not track which inode created the
 * staging extent, so callers must ensure that there are no cached inodes with
 * live CoW staging extents.
 */
int
xfs_reflink_recover_cow(
	struct xfs_mount	*mp)
{
	struct xfs_perag	*pag;
	xfs_agnumber_t		agno;
	int			error = 0;

	if (!xfs_has_reflink(mp))
		return 0;

	for_each_perag(mp, agno, pag) {
		error = xfs_refcount_recover_cow_leftovers(mp, pag);
		if (error) {
			xfs_perag_rele(pag);
			break;
		}
	}

	return error;
}

/*
 * Reflinking (Block) Ranges of Two Files Together
 *
 * First, ensure that the reflink flag is set on both inodes.  The flag is an
 * optimization to avoid unnecessary refcount btree lookups in the write path.
 *
 * Now we can iteratively remap the range of extents (and holes) in src to the
 * corresponding ranges in dest.  Let drange and srange denote the ranges of
 * logical blocks in dest and src touched by the reflink operation.
 *
 * While the length of drange is greater than zero,
 *    - Read src's bmbt at the start of srange ("imap")
 *    - If imap doesn't exist, make imap appear to start at the end of srange
 *      with zero length.
 *    - If imap starts before srange, advance imap to start at srange.
 *    - If imap goes beyond srange, truncate imap to end at the end of srange.
 *    - Punch (imap start - srange start + imap len) blocks from dest at
 *      offset (drange start).
 *    - If imap points to a real range of pblks,
 *         > Increase the refcount of the imap's pblks
 *         > Map imap's pblks into dest at the offset
 *           (drange start + imap start - srange start)
 *    - Advance drange and srange by (imap start - srange start + imap len)
 *
 * Finally, if the reflink made dest longer, update both the in-core and
 * on-disk file sizes.
 *
 * ASCII Art Demonstration:
 *
 * Let's say we want to reflink this source file:
 *
 * ----SSSSSSS-SSSSS----SSSSSS (src file)
 *   <-------------------->
 *
 * into this destination file:
 *
 * --DDDDDDDDDDDDDDDDDDD--DDD (dest file)
 *        <-------------------->
 * '-' means a hole, and 'S' and 'D' are written blocks in the src and dest.
 * Observe that the range has different logical offsets in either file.
 *
 * Consider that the first extent in the source file doesn't line up with our
 * reflink range.  Unmapping  and remapping are separate operations, so we can
 * unmap more blocks from the destination file than we remap.
 *
 * ----SSSSSSS-SSSSS----SSSSSS
 *   <------->
 * --DDDDD---------DDDDD--DDD
 *        <------->
 *
 * Now remap the source extent into the destination file:
 *
 * ----SSSSSSS-SSSSS----SSSSSS
 *   <------->
 * --DDDDD--SSSSSSSDDDDD--DDD
 *        <------->
 *
 * Do likewise with the second hole and extent in our range.  Holes in the
 * unmap range don't affect our operation.
 *
 * ----SSSSSSS-SSSSS----SSSSSS
 *            <---->
 * --DDDDD--SSSSSSS-SSSSS-DDD
 *                 <---->
 *
 * Finally, unmap and remap part of the third extent.  This will increase the
 * size of the destination file.
 *
 * ----SSSSSSS-SSSSS----SSSSSS
 *                  <----->
 * --DDDDD--SSSSSSS-SSSSS----SSS
 *                       <----->
 *
 * Once we update the destination file's i_size, we're done.
 */

/*
 * Ensure the reflink bit is set in both inodes.
 */
STATIC int
xfs_reflink_set_inode_flag(
	struct xfs_inode	*src,
	struct xfs_inode	*dest)
{
	struct xfs_mount	*mp = src->i_mount;
	int			error;
	struct xfs_trans	*tp;

	if (xfs_is_reflink_inode(src) && xfs_is_reflink_inode(dest))
		return 0;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		goto out_error;

	/* Lock both files against IO */
	if (src->i_ino == dest->i_ino)
		xfs_ilock(src, XFS_ILOCK_EXCL);
	else
		xfs_lock_two_inodes(src, XFS_ILOCK_EXCL, dest, XFS_ILOCK_EXCL);

	if (!xfs_is_reflink_inode(src)) {
		trace_xfs_reflink_set_inode_flag(src);
		xfs_trans_ijoin(tp, src, XFS_ILOCK_EXCL);
		src->i_diflags2 |= XFS_DIFLAG2_REFLINK;
		xfs_trans_log_inode(tp, src, XFS_ILOG_CORE);
		xfs_ifork_init_cow(src);
	} else
		xfs_iunlock(src, XFS_ILOCK_EXCL);

	if (src->i_ino == dest->i_ino)
		goto commit_flags;

	if (!xfs_is_reflink_inode(dest)) {
		trace_xfs_reflink_set_inode_flag(dest);
		xfs_trans_ijoin(tp, dest, XFS_ILOCK_EXCL);
		dest->i_diflags2 |= XFS_DIFLAG2_REFLINK;
		xfs_trans_log_inode(tp, dest, XFS_ILOG_CORE);
		xfs_ifork_init_cow(dest);
	} else
		xfs_iunlock(dest, XFS_ILOCK_EXCL);

commit_flags:
	error = xfs_trans_commit(tp);
	if (error)
		goto out_error;
	return error;

out_error:
	trace_xfs_reflink_set_inode_flag_error(dest, error, _RET_IP_);
	return error;
}

/*
 * Update destination inode size & cowextsize hint, if necessary.
 */
int
xfs_reflink_update_dest(
	struct xfs_inode	*dest,
	xfs_off_t		newlen,
	xfs_extlen_t		cowextsize,
	unsigned int		remap_flags)
{
	struct xfs_mount	*mp = dest->i_mount;
	struct xfs_trans	*tp;
	int			error;

	if (newlen <= i_size_read(VFS_I(dest)) && cowextsize == 0)
		return 0;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_ichange, 0, 0, 0, &tp);
	if (error)
		goto out_error;

	xfs_ilock(dest, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, dest, XFS_ILOCK_EXCL);

	if (newlen > i_size_read(VFS_I(dest))) {
		trace_xfs_reflink_update_inode_size(dest, newlen);
		i_size_write(VFS_I(dest), newlen);
		dest->i_disk_size = newlen;
	}

	if (cowextsize) {
		dest->i_cowextsize = cowextsize;
		dest->i_diflags2 |= XFS_DIFLAG2_COWEXTSIZE;
	}

	xfs_trans_log_inode(tp, dest, XFS_ILOG_CORE);

	error = xfs_trans_commit(tp);
	if (error)
		goto out_error;
	return error;

out_error:
	trace_xfs_reflink_update_inode_size_error(dest, error, _RET_IP_);
	return error;
}

/*
 * Do we have enough reserve in this AG to handle a reflink?  The refcount
 * btree already reserved all the space it needs, but the rmap btree can grow
 * infinitely, so we won't allow more reflinks when the AG is down to the
 * btree reserves.
 */
static int
xfs_reflink_ag_has_free_space(
	struct xfs_mount	*mp,
	xfs_agnumber_t		agno)
{
	struct xfs_perag	*pag;
	int			error = 0;

	if (!xfs_has_rmapbt(mp))
		return 0;

	pag = xfs_perag_get(mp, agno);
	if (xfs_ag_resv_critical(pag, XFS_AG_RESV_RMAPBT) ||
	    xfs_ag_resv_critical(pag, XFS_AG_RESV_METADATA))
		error = -ENOSPC;
	xfs_perag_put(pag);
	return error;
}

/*
 * Remap the given extent into the file.  The dmap blockcount will be set to
 * the number of blocks that were actually remapped.
 */
STATIC int
xfs_reflink_remap_extent(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*dmap,
	xfs_off_t		new_isize)
{
	struct xfs_bmbt_irec	smap;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	xfs_off_t		newlen;
	int64_t			qdelta = 0;
	unsigned int		resblks;
	bool			quota_reserved = true;
	bool			smap_real;
	bool			dmap_written = xfs_bmap_is_written_extent(dmap);
	int			iext_delta = 0;
	int			nimaps;
	int			error;

	/*
	 * Start a rolling transaction to switch the mappings.
	 *
	 * Adding a written extent to the extent map can cause a bmbt split,
	 * and removing a mapped extent from the extent can cause a bmbt split.
	 * The two operations cannot both cause a split since they operate on
	 * the same index in the bmap btree, so we only need a reservation for
	 * one bmbt split if either thing is happening.  However, we haven't
	 * locked the inode yet, so we reserve assuming this is the case.
	 *
	 * The first allocation call tries to reserve enough space to handle
	 * mapping dmap into a sparse part of the file plus the bmbt split.  We
	 * haven't locked the inode or read the existing mapping yet, so we do
	 * not know for sure that we need the space.  This should succeed most
	 * of the time.
	 *
	 * If the first attempt fails, try again but reserving only enough
	 * space to handle a bmbt split.  This is the hard minimum requirement,
	 * and we revisit quota reservations later when we know more about what
	 * we're remapping.
	 */
	resblks = XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK);
	error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write,
			resblks + dmap->br_blockcount, 0, false, &tp);
	if (error == -EDQUOT || error == -ENOSPC) {
		quota_reserved = false;
		error = xfs_trans_alloc_inode(ip, &M_RES(mp)->tr_write,
				resblks, 0, false, &tp);
	}
	if (error)
		goto out;

	/*
	 * Read what's currently mapped in the destination file into smap.
	 * If smap isn't a hole, we will have to remove it before we can add
	 * dmap to the destination file.
	 */
	nimaps = 1;
	error = xfs_bmapi_read(ip, dmap->br_startoff, dmap->br_blockcount,
			&smap, &nimaps, 0);
	if (error)
		goto out_cancel;
	ASSERT(nimaps == 1 && smap.br_startoff == dmap->br_startoff);
	smap_real = xfs_bmap_is_real_extent(&smap);

	/*
	 * We can only remap as many blocks as the smaller of the two extent
	 * maps, because we can only remap one extent at a time.
	 */
	dmap->br_blockcount = min(dmap->br_blockcount, smap.br_blockcount);
	ASSERT(dmap->br_blockcount == smap.br_blockcount);

	trace_xfs_reflink_remap_extent_dest(ip, &smap);

	/*
	 * Two extents mapped to the same physical block must not have
	 * different states; that's filesystem corruption.  Move on to the next
	 * extent if they're both holes or both the same physical extent.
	 */
	if (dmap->br_startblock == smap.br_startblock) {
		if (dmap->br_state != smap.br_state) {
			xfs_bmap_mark_sick(ip, XFS_DATA_FORK);
			error = -EFSCORRUPTED;
		}
		goto out_cancel;
	}

	/* If both extents are unwritten, leave them alone. */
	if (dmap->br_state == XFS_EXT_UNWRITTEN &&
	    smap.br_state == XFS_EXT_UNWRITTEN)
		goto out_cancel;

	/* No reflinking if the AG of the dest mapping is low on space. */
	if (dmap_written) {
		error = xfs_reflink_ag_has_free_space(mp,
				XFS_FSB_TO_AGNO(mp, dmap->br_startblock));
		if (error)
			goto out_cancel;
	}

	/*
	 * Increase quota reservation if we think the quota block counter for
	 * this file could increase.
	 *
	 * If we are mapping a written extent into the file, we need to have
	 * enough quota block count reservation to handle the blocks in that
	 * extent.  We log only the delta to the quota block counts, so if the
	 * extent we're unmapping also has blocks allocated to it, we don't
	 * need a quota reservation for the extent itself.
	 *
	 * Note that if we're replacing a delalloc reservation with a written
	 * extent, we have to take the full quota reservation because removing
	 * the delalloc reservation gives the block count back to the quota
	 * count.  This is suboptimal, but the VFS flushed the dest range
	 * before we started.  That should have removed all the delalloc
	 * reservations, but we code defensively.
	 *
	 * xfs_trans_alloc_inode above already tried to grab an even larger
	 * quota reservation, and kicked off a blockgc scan if it couldn't.
	 * If we can't get a potentially smaller quota reservation now, we're
	 * done.
	 */
	if (!quota_reserved && !smap_real && dmap_written) {
		error = xfs_trans_reserve_quota_nblks(tp, ip,
				dmap->br_blockcount, 0, false);
		if (error)
			goto out_cancel;
	}

	if (smap_real)
		++iext_delta;

	if (dmap_written)
		++iext_delta;

	error = xfs_iext_count_extend(tp, ip, XFS_DATA_FORK, iext_delta);
	if (error)
		goto out_cancel;

	if (smap_real) {
		/*
		 * If the extent we're unmapping is backed by storage (written
		 * or not), unmap the extent and drop its refcount.
		 */
		xfs_bmap_unmap_extent(tp, ip, XFS_DATA_FORK, &smap);
		xfs_refcount_decrease_extent(tp, &smap);
		qdelta -= smap.br_blockcount;
	} else if (smap.br_startblock == DELAYSTARTBLOCK) {
		int		done;

		/*
		 * If the extent we're unmapping is a delalloc reservation,
		 * we can use the regular bunmapi function to release the
		 * incore state.  Dropping the delalloc reservation takes care
		 * of the quota reservation for us.
		 */
		error = xfs_bunmapi(NULL, ip, smap.br_startoff,
				smap.br_blockcount, 0, 1, &done);
		if (error)
			goto out_cancel;
		ASSERT(done);
	}

	/*
	 * If the extent we're sharing is backed by written storage, increase
	 * its refcount and map it into the file.
	 */
	if (dmap_written) {
		xfs_refcount_increase_extent(tp, dmap);
		xfs_bmap_map_extent(tp, ip, XFS_DATA_FORK, dmap);
		qdelta += dmap->br_blockcount;
	}

	xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_BCOUNT, qdelta);

	/* Update dest isize if needed. */
	newlen = XFS_FSB_TO_B(mp, dmap->br_startoff + dmap->br_blockcount);
	newlen = min_t(xfs_off_t, newlen, new_isize);
	if (newlen > i_size_read(VFS_I(ip))) {
		trace_xfs_reflink_update_inode_size(ip, newlen);
		i_size_write(VFS_I(ip), newlen);
		ip->i_disk_size = newlen;
		xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
	}

	/* Commit everything and unlock. */
	error = xfs_trans_commit(tp);
	goto out_unlock;

out_cancel:
	xfs_trans_cancel(tp);
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out:
	if (error)
		trace_xfs_reflink_remap_extent_error(ip, error, _RET_IP_);
	return error;
}

/* Remap a range of one file to the other. */
int
xfs_reflink_remap_blocks(
	struct xfs_inode	*src,
	loff_t			pos_in,
	struct xfs_inode	*dest,
	loff_t			pos_out,
	loff_t			remap_len,
	loff_t			*remapped)
{
	struct xfs_bmbt_irec	imap;
	struct xfs_mount	*mp = src->i_mount;
	xfs_fileoff_t		srcoff = XFS_B_TO_FSBT(mp, pos_in);
	xfs_fileoff_t		destoff = XFS_B_TO_FSBT(mp, pos_out);
	xfs_filblks_t		len;
	xfs_filblks_t		remapped_len = 0;
	xfs_off_t		new_isize = pos_out + remap_len;
	int			nimaps;
	int			error = 0;

	len = min_t(xfs_filblks_t, XFS_B_TO_FSB(mp, remap_len),
			XFS_MAX_FILEOFF);

	trace_xfs_reflink_remap_blocks(src, srcoff, len, dest, destoff);

	while (len > 0) {
		unsigned int	lock_mode;

		/* Read extent from the source file */
		nimaps = 1;
		lock_mode = xfs_ilock_data_map_shared(src);
		error = xfs_bmapi_read(src, srcoff, len, &imap, &nimaps, 0);
		xfs_iunlock(src, lock_mode);
		if (error)
			break;
		/*
		 * The caller supposedly flushed all dirty pages in the source
		 * file range, which means that writeback should have allocated
		 * or deleted all delalloc reservations in that range.  If we
		 * find one, that's a good sign that something is seriously
		 * wrong here.
		 */
		ASSERT(nimaps == 1 && imap.br_startoff == srcoff);
		if (imap.br_startblock == DELAYSTARTBLOCK) {
			ASSERT(imap.br_startblock != DELAYSTARTBLOCK);
			xfs_bmap_mark_sick(src, XFS_DATA_FORK);
			error = -EFSCORRUPTED;
			break;
		}

		trace_xfs_reflink_remap_extent_src(src, &imap);

		/* Remap into the destination file at the given offset. */
		imap.br_startoff = destoff;
		error = xfs_reflink_remap_extent(dest, &imap, new_isize);
		if (error)
			break;

		if (fatal_signal_pending(current)) {
			error = -EINTR;
			break;
		}

		/* Advance drange/srange */
		srcoff += imap.br_blockcount;
		destoff += imap.br_blockcount;
		len -= imap.br_blockcount;
		remapped_len += imap.br_blockcount;
		cond_resched();
	}

	if (error)
		trace_xfs_reflink_remap_blocks_error(dest, error, _RET_IP_);
	*remapped = min_t(loff_t, remap_len,
			  XFS_FSB_TO_B(src->i_mount, remapped_len));
	return error;
}

/*
 * If we're reflinking to a point past the destination file's EOF, we must
 * zero any speculative post-EOF preallocations that sit between the old EOF
 * and the destination file offset.
 */
static int
xfs_reflink_zero_posteof(
	struct xfs_inode	*ip,
	loff_t			pos)
{
	loff_t			isize = i_size_read(VFS_I(ip));

	if (pos <= isize)
		return 0;

	trace_xfs_zero_eof(ip, isize, pos - isize);
	return xfs_zero_range(ip, isize, pos - isize, NULL);
}

/*
 * Prepare two files for range cloning.  Upon a successful return both inodes
 * will have the iolock and mmaplock held, the page cache of the out file will
 * be truncated, and any leases on the out file will have been broken.  This
 * function borrows heavily from xfs_file_aio_write_checks.
 *
 * The VFS allows partial EOF blocks to "match" for dedupe even though it hasn't
 * checked that the bytes beyond EOF physically match. Hence we cannot use the
 * EOF block in the source dedupe range because it's not a complete block match,
 * hence can introduce a corruption into the file that has it's block replaced.
 *
 * In similar fashion, the VFS file cloning also allows partial EOF blocks to be
 * "block aligned" for the purposes of cloning entire files.  However, if the
 * source file range includes the EOF block and it lands within the existing EOF
 * of the destination file, then we can expose stale data from beyond the source
 * file EOF in the destination file.
 *
 * XFS doesn't support partial block sharing, so in both cases we have check
 * these cases ourselves. For dedupe, we can simply round the length to dedupe
 * down to the previous whole block and ignore the partial EOF block. While this
 * means we can't dedupe the last block of a file, this is an acceptible
 * tradeoff for simplicity on implementation.
 *
 * For cloning, we want to share the partial EOF block if it is also the new EOF
 * block of the destination file. If the partial EOF block lies inside the
 * existing destination EOF, then we have to abort the clone to avoid exposing
 * stale data in the destination file. Hence we reject these clone attempts with
 * -EINVAL in this case.
 */
int
xfs_reflink_remap_prep(
	struct file		*file_in,
	loff_t			pos_in,
	struct file		*file_out,
	loff_t			pos_out,
	loff_t			*len,
	unsigned int		remap_flags)
{
	struct inode		*inode_in = file_inode(file_in);
	struct xfs_inode	*src = XFS_I(inode_in);
	struct inode		*inode_out = file_inode(file_out);
	struct xfs_inode	*dest = XFS_I(inode_out);
	int			ret;

	/* Lock both files against IO */
	ret = xfs_ilock2_io_mmap(src, dest);
	if (ret)
		return ret;

	/* Check file eligibility and prepare for block sharing. */
	ret = -EINVAL;
	/* Don't reflink realtime inodes */
	if (XFS_IS_REALTIME_INODE(src) || XFS_IS_REALTIME_INODE(dest))
		goto out_unlock;

	/* Don't share DAX file data with non-DAX file. */
	if (IS_DAX(inode_in) != IS_DAX(inode_out))
		goto out_unlock;

	if (!IS_DAX(inode_in))
		ret = generic_remap_file_range_prep(file_in, pos_in, file_out,
				pos_out, len, remap_flags);
	else
		ret = dax_remap_file_range_prep(file_in, pos_in, file_out,
				pos_out, len, remap_flags, &xfs_read_iomap_ops);
	if (ret || *len == 0)
		goto out_unlock;

	/* Attach dquots to dest inode before changing block map */
	ret = xfs_qm_dqattach(dest);
	if (ret)
		goto out_unlock;

	/*
	 * Zero existing post-eof speculative preallocations in the destination
	 * file.
	 */
	ret = xfs_reflink_zero_posteof(dest, pos_out);
	if (ret)
		goto out_unlock;

	/* Set flags and remap blocks. */
	ret = xfs_reflink_set_inode_flag(src, dest);
	if (ret)
		goto out_unlock;

	/*
	 * If pos_out > EOF, we may have dirtied blocks between EOF and
	 * pos_out. In that case, we need to extend the flush and unmap to cover
	 * from EOF to the end of the copy length.
	 */
	if (pos_out > XFS_ISIZE(dest)) {
		loff_t	flen = *len + (pos_out - XFS_ISIZE(dest));
		ret = xfs_flush_unmap_range(dest, XFS_ISIZE(dest), flen);
	} else {
		ret = xfs_flush_unmap_range(dest, pos_out, *len);
	}
	if (ret)
		goto out_unlock;

	xfs_iflags_set(src, XFS_IREMAPPING);
	if (inode_in != inode_out)
		xfs_ilock_demote(src, XFS_IOLOCK_EXCL | XFS_MMAPLOCK_EXCL);

	return 0;
out_unlock:
	xfs_iunlock2_io_mmap(src, dest);
	return ret;
}

/* Does this inode need the reflink flag? */
int
xfs_reflink_inode_has_shared_extents(
	struct xfs_trans		*tp,
	struct xfs_inode		*ip,
	bool				*has_shared)
{
	struct xfs_bmbt_irec		got;
	struct xfs_mount		*mp = ip->i_mount;
	struct xfs_ifork		*ifp;
	struct xfs_iext_cursor		icur;
	bool				found;
	int				error;

	ifp = xfs_ifork_ptr(ip, XFS_DATA_FORK);
	error = xfs_iread_extents(tp, ip, XFS_DATA_FORK);
	if (error)
		return error;

	*has_shared = false;
	found = xfs_iext_lookup_extent(ip, ifp, 0, &icur, &got);
	while (found) {
		struct xfs_perag	*pag;
		xfs_agblock_t		agbno;
		xfs_extlen_t		aglen;
		xfs_agblock_t		rbno;
		xfs_extlen_t		rlen;

		if (isnullstartblock(got.br_startblock) ||
		    got.br_state != XFS_EXT_NORM)
			goto next;

		pag = xfs_perag_get(mp, XFS_FSB_TO_AGNO(mp, got.br_startblock));
		agbno = XFS_FSB_TO_AGBNO(mp, got.br_startblock);
		aglen = got.br_blockcount;
		error = xfs_reflink_find_shared(pag, tp, agbno, aglen,
				&rbno, &rlen, false);
		xfs_perag_put(pag);
		if (error)
			return error;

		/* Is there still a shared block here? */
		if (rbno != NULLAGBLOCK) {
			*has_shared = true;
			return 0;
		}
next:
		found = xfs_iext_next_extent(ifp, &icur, &got);
	}

	return 0;
}

/*
 * Clear the inode reflink flag if there are no shared extents.
 *
 * The caller is responsible for joining the inode to the transaction passed in.
 * The inode will be joined to the transaction that is returned to the caller.
 */
int
xfs_reflink_clear_inode_flag(
	struct xfs_inode	*ip,
	struct xfs_trans	**tpp)
{
	bool			needs_flag;
	int			error = 0;

	ASSERT(xfs_is_reflink_inode(ip));

	error = xfs_reflink_inode_has_shared_extents(*tpp, ip, &needs_flag);
	if (error || needs_flag)
		return error;

	/*
	 * We didn't find any shared blocks so turn off the reflink flag.
	 * First, get rid of any leftover CoW mappings.
	 */
	error = xfs_reflink_cancel_cow_blocks(ip, tpp, 0, XFS_MAX_FILEOFF,
			true);
	if (error)
		return error;

	/* Clear the inode flag. */
	trace_xfs_reflink_unset_inode_flag(ip);
	ip->i_diflags2 &= ~XFS_DIFLAG2_REFLINK;
	xfs_inode_clear_cowblocks_tag(ip);
	xfs_trans_log_inode(*tpp, ip, XFS_ILOG_CORE);

	return error;
}

/*
 * Clear the inode reflink flag if there are no shared extents and the size
 * hasn't changed.
 */
STATIC int
xfs_reflink_try_clear_inode_flag(
	struct xfs_inode	*ip)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error = 0;

	/* Start a rolling transaction to remove the mappings */
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, 0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	error = xfs_reflink_clear_inode_flag(ip, &tp);
	if (error)
		goto cancel;

	error = xfs_trans_commit(tp);
	if (error)
		goto out;

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
cancel:
	xfs_trans_cancel(tp);
out:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/*
 * Pre-COW all shared blocks within a given byte range of a file and turn off
 * the reflink flag if we unshare all of the file's blocks.
 */
int
xfs_reflink_unshare(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		len)
{
	struct inode		*inode = VFS_I(ip);
	int			error;

	if (!xfs_is_reflink_inode(ip))
		return 0;

	trace_xfs_reflink_unshare(ip, offset, len);

	inode_dio_wait(inode);

	if (IS_DAX(inode))
		error = dax_file_unshare(inode, offset, len,
				&xfs_dax_write_iomap_ops);
	else
		error = iomap_file_unshare(inode, offset, len,
				&xfs_buffered_write_iomap_ops);
	if (error)
		goto out;

	error = filemap_write_and_wait_range(inode->i_mapping, offset,
			offset + len - 1);
	if (error)
		goto out;

	/* Turn off the reflink flag if possible. */
	error = xfs_reflink_try_clear_inode_flag(ip);
	if (error)
		goto out;
	return 0;

out:
	trace_xfs_reflink_unshare_error(ip, error, _RET_IP_);
	return error;
}
