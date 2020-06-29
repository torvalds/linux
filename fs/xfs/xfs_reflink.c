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
#include "xfs_sb.h"
#include "xfs_ag_resv.h"

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
int
xfs_reflink_find_shared(
	struct xfs_mount	*mp,
	struct xfs_trans	*tp,
	xfs_agnumber_t		agno,
	xfs_agblock_t		agbno,
	xfs_extlen_t		aglen,
	xfs_agblock_t		*fbno,
	xfs_extlen_t		*flen,
	bool			find_end_of_shared)
{
	struct xfs_buf		*agbp;
	struct xfs_btree_cur	*cur;
	int			error;

	error = xfs_alloc_read_agf(mp, tp, agno, 0, &agbp);
	if (error)
		return error;

	cur = xfs_refcountbt_init_cursor(mp, tp, agbp, agno);

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
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	xfs_extlen_t		aglen;
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	int			error = 0;

	/* Holes, unwritten, and delalloc extents cannot be shared */
	if (!xfs_is_cow_inode(ip) || !xfs_bmap_is_real_extent(irec)) {
		*shared = false;
		return 0;
	}

	trace_xfs_reflink_trim_around_shared(ip, irec);

	agno = XFS_FSB_TO_AGNO(ip->i_mount, irec->br_startblock);
	agbno = XFS_FSB_TO_AGBNO(ip->i_mount, irec->br_startblock);
	aglen = irec->br_blockcount;

	error = xfs_reflink_find_shared(ip->i_mount, NULL, agno, agbno,
			aglen, &fbno, &flen, true);
	if (error)
		return error;

	*shared = false;
	if (fbno == NULLAGBLOCK) {
		/* No shared blocks at all. */
		return 0;
	} else if (fbno == agbno) {
		/*
		 * The start of this extent is shared.  Truncate the
		 * mapping at the end of the shared region so that a
		 * subsequent iteration starts at the start of the
		 * unshared region.
		 */
		irec->br_blockcount = flen;
		*shared = true;
		return 0;
	} else {
		/*
		 * There's a shared extent midway through this extent.
		 * Truncate the mapping at the start of the shared
		 * extent so that a subsequent iteration starts at the
		 * start of the shared region.
		 */
		irec->br_blockcount = fbno - agbno;
		return 0;
	}
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
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = imap->br_startoff;
	xfs_filblks_t		count_fsb = imap->br_blockcount;
	struct xfs_trans	*tp;
	int			nimaps, error = 0;
	bool			found;
	xfs_filblks_t		resaligned;
	xfs_extlen_t		resblks = 0;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));
	if (!ip->i_cowfp) {
		ASSERT(!xfs_is_reflink_inode(ip));
		xfs_ifork_init_cow(ip);
	}

	error = xfs_find_trim_cow_extent(ip, imap, cmap, shared, &found);
	if (error || !*shared)
		return error;
	if (found)
		goto convert;

	resaligned = xfs_aligned_fsb_count(imap->br_startoff,
		imap->br_blockcount, xfs_get_cowextsz_hint(ip));
	resblks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);

	xfs_iunlock(ip, *lockmode);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0, 0, &tp);
	*lockmode = XFS_ILOCK_EXCL;
	xfs_ilock(ip, *lockmode);

	if (error)
		return error;

	error = xfs_qm_dqattach_locked(ip, false);
	if (error)
		goto out_trans_cancel;

	/*
	 * Check for an overlapping extent again now that we dropped the ilock.
	 */
	error = xfs_find_trim_cow_extent(ip, imap, cmap, shared, &found);
	if (error || !*shared)
		goto out_trans_cancel;
	if (found) {
		xfs_trans_cancel(tp);
		goto convert;
	}

	error = xfs_trans_reserve_quota_nblks(tp, ip, resblks, 0,
			XFS_QMOPT_RES_REGBLKS);
	if (error)
		goto out_trans_cancel;

	xfs_trans_ijoin(tp, ip, 0);

	/* Allocate the entire reservation as unwritten blocks. */
	nimaps = 1;
	error = xfs_bmapi_write(tp, ip, imap->br_startoff, imap->br_blockcount,
			XFS_BMAPI_COWFORK | XFS_BMAPI_PREALLOC, 0, cmap,
			&nimaps);
	if (error)
		goto out_unreserve;

	xfs_inode_set_cowblocks_tag(ip);
	error = xfs_trans_commit(tp);
	if (error)
		return error;

	/*
	 * Allocation succeeded but the requested range was not even partially
	 * satisfied?  Bail out!
	 */
	if (nimaps == 0)
		return -ENOSPC;
convert:
	xfs_trim_extent(cmap, offset_fsb, count_fsb);
	/*
	 * COW fork extents are supposed to remain unwritten until we're ready
	 * to initiate a disk write.  For direct I/O we are going to write the
	 * data and need the conversion, but for buffered writes we're done.
	 */
	if (!convert_now || cmap->br_state == XFS_EXT_NORM)
		return 0;
	trace_xfs_reflink_convert_cow(ip, cmap);
	return xfs_reflink_convert_cow_locked(ip, offset_fsb, count_fsb);

out_unreserve:
	xfs_trans_unreserve_quota_nblks(tp, ip, (long)resblks, 0,
			XFS_QMOPT_RES_REGBLKS);
out_trans_cancel:
	xfs_trans_cancel(tp);
	return error;
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
	struct xfs_ifork		*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
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
			error = xfs_bmap_del_extent_delay(ip, XFS_COW_FORK,
					&icur, &got, &del);
			if (error)
				break;
		} else if (del.br_state == XFS_EXT_UNWRITTEN || cancel_real) {
			ASSERT((*tpp)->t_firstblock == NULLFSBLOCK);

			/* Free the CoW orphan record. */
			xfs_refcount_free_cow_extent(*tpp, del.br_startblock,
					del.br_blockcount);

			xfs_bmap_add_free(*tpp, del.br_startblock,
					  del.br_blockcount, NULL);

			/* Roll the transaction */
			error = xfs_defer_finish(tpp);
			if (error)
				break;

			/* Remove the mapping from the CoW fork. */
			xfs_bmap_del_extent_cow(ip, &icur, &got, &del);

			/* Remove the quota reservation */
			error = xfs_trans_reserve_quota_nblks(NULL, ip,
					-(long)del.br_blockcount, 0,
					XFS_QMOPT_RES_REGBLKS);
			if (error)
				break;
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
	xfs_fileoff_t		offset_fsb,
	xfs_fileoff_t		*end_fsb)
{
	struct xfs_bmbt_irec	got, del;
	struct xfs_iext_cursor	icur;
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	xfs_filblks_t		rlen;
	unsigned int		resblks;
	int			error;

	/* No COW extents?  That's easy! */
	if (ifp->if_bytes == 0) {
		*end_fsb = offset_fsb;
		return 0;
	}

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
	if (!xfs_iext_lookup_extent_before(ip, ifp, end_fsb, &icur, &got) ||
	    got.br_startoff + got.br_blockcount <= offset_fsb) {
		*end_fsb = offset_fsb;
		goto out_cancel;
	}

	/*
	 * Structure copy @got into @del, then trim @del to the range that we
	 * were asked to remap.  We preserve @got for the eventual CoW fork
	 * deletion; from now on @del represents the mapping that we're
	 * actually remapping.
	 */
	del = got;
	xfs_trim_extent(&del, offset_fsb, *end_fsb - offset_fsb);

	ASSERT(del.br_blockcount > 0);

	/*
	 * Only remap real extents that contain data.  With AIO, speculative
	 * preallocations can leak into the range we are called upon, and we
	 * need to skip them.
	 */
	if (!xfs_bmap_is_real_extent(&got)) {
		*end_fsb = del.br_startoff;
		goto out_cancel;
	}

	/* Unmap the old blocks in the data fork. */
	rlen = del.br_blockcount;
	error = __xfs_bunmapi(tp, ip, del.br_startoff, &rlen, 0, 1);
	if (error)
		goto out_cancel;

	/* Trim the extent to whatever got unmapped. */
	xfs_trim_extent(&del, del.br_startoff + rlen, del.br_blockcount - rlen);
	trace_xfs_reflink_cow_remap(ip, &del);

	/* Free the CoW orphan record. */
	xfs_refcount_free_cow_extent(tp, del.br_startblock, del.br_blockcount);

	/* Map the new blocks into the data fork. */
	xfs_bmap_map_extent(tp, ip, &del);

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
	*end_fsb = del.br_startoff;
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
	 * Walk backwards until we're out of the I/O range.  The loop function
	 * repeatedly cycles the ILOCK to allocate one transaction per remapped
	 * extent.
	 *
	 * If we're being called by writeback then the the pages will still
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
		error = xfs_reflink_end_cow_extent(ip, offset_fsb, &end_fsb);

	if (error)
		trace_xfs_reflink_end_cow_error(ip, error, _RET_IP_);
	return error;
}

/*
 * Free leftover CoW reservations that didn't get cleaned out.
 */
int
xfs_reflink_recover_cow(
	struct xfs_mount	*mp)
{
	xfs_agnumber_t		agno;
	int			error = 0;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return 0;

	for (agno = 0; agno < mp->m_sb.sb_agcount; agno++) {
		error = xfs_refcount_recover_cow_leftovers(mp, agno);
		if (error)
			break;
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
		src->i_d.di_flags2 |= XFS_DIFLAG2_REFLINK;
		xfs_trans_log_inode(tp, src, XFS_ILOG_CORE);
		xfs_ifork_init_cow(src);
	} else
		xfs_iunlock(src, XFS_ILOCK_EXCL);

	if (src->i_ino == dest->i_ino)
		goto commit_flags;

	if (!xfs_is_reflink_inode(dest)) {
		trace_xfs_reflink_set_inode_flag(dest);
		xfs_trans_ijoin(tp, dest, XFS_ILOCK_EXCL);
		dest->i_d.di_flags2 |= XFS_DIFLAG2_REFLINK;
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
		dest->i_d.di_size = newlen;
	}

	if (cowextsize) {
		dest->i_d.di_cowextsize = cowextsize;
		dest->i_d.di_flags2 |= XFS_DIFLAG2_COWEXTSIZE;
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

	if (!xfs_sb_version_hasrmapbt(&mp->m_sb))
		return 0;

	pag = xfs_perag_get(mp, agno);
	if (xfs_ag_resv_critical(pag, XFS_AG_RESV_RMAPBT) ||
	    xfs_ag_resv_critical(pag, XFS_AG_RESV_METADATA))
		error = -ENOSPC;
	xfs_perag_put(pag);
	return error;
}

/*
 * Unmap a range of blocks from a file, then map other blocks into the hole.
 * The range to unmap is (destoff : destoff + srcioff + irec->br_blockcount).
 * The extent irec is mapped into dest at irec->br_startoff.
 */
STATIC int
xfs_reflink_remap_extent(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*irec,
	xfs_fileoff_t		destoff,
	xfs_off_t		new_isize)
{
	struct xfs_mount	*mp = ip->i_mount;
	bool			real_extent = xfs_bmap_is_real_extent(irec);
	struct xfs_trans	*tp;
	unsigned int		resblks;
	struct xfs_bmbt_irec	uirec;
	xfs_filblks_t		rlen;
	xfs_filblks_t		unmap_len;
	xfs_off_t		newlen;
	int			error;

	unmap_len = irec->br_startoff + irec->br_blockcount - destoff;
	trace_xfs_reflink_punch_range(ip, destoff, unmap_len);

	/* No reflinking if we're low on space */
	if (real_extent) {
		error = xfs_reflink_ag_has_free_space(mp,
				XFS_FSB_TO_AGNO(mp, irec->br_startblock));
		if (error)
			goto out;
	}

	/* Start a rolling transaction to switch the mappings */
	resblks = XFS_EXTENTADD_SPACE_RES(ip->i_mount, XFS_DATA_FORK);
	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0, 0, &tp);
	if (error)
		goto out;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/* If we're not just clearing space, then do we have enough quota? */
	if (real_extent) {
		error = xfs_trans_reserve_quota_nblks(tp, ip,
				irec->br_blockcount, 0, XFS_QMOPT_RES_REGBLKS);
		if (error)
			goto out_cancel;
	}

	trace_xfs_reflink_remap(ip, irec->br_startoff,
				irec->br_blockcount, irec->br_startblock);

	/* Unmap the old blocks in the data fork. */
	rlen = unmap_len;
	while (rlen) {
		ASSERT(tp->t_firstblock == NULLFSBLOCK);
		error = __xfs_bunmapi(tp, ip, destoff, &rlen, 0, 1);
		if (error)
			goto out_cancel;

		/*
		 * Trim the extent to whatever got unmapped.
		 * Remember, bunmapi works backwards.
		 */
		uirec.br_startblock = irec->br_startblock + rlen;
		uirec.br_startoff = irec->br_startoff + rlen;
		uirec.br_blockcount = unmap_len - rlen;
		uirec.br_state = irec->br_state;
		unmap_len = rlen;

		/* If this isn't a real mapping, we're done. */
		if (!real_extent || uirec.br_blockcount == 0)
			goto next_extent;

		trace_xfs_reflink_remap(ip, uirec.br_startoff,
				uirec.br_blockcount, uirec.br_startblock);

		/* Update the refcount tree */
		xfs_refcount_increase_extent(tp, &uirec);

		/* Map the new blocks into the data fork. */
		xfs_bmap_map_extent(tp, ip, &uirec);

		/* Update quota accounting. */
		xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_BCOUNT,
				uirec.br_blockcount);

		/* Update dest isize if needed. */
		newlen = XFS_FSB_TO_B(mp,
				uirec.br_startoff + uirec.br_blockcount);
		newlen = min_t(xfs_off_t, newlen, new_isize);
		if (newlen > i_size_read(VFS_I(ip))) {
			trace_xfs_reflink_update_inode_size(ip, newlen);
			i_size_write(VFS_I(ip), newlen);
			ip->i_d.di_size = newlen;
			xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);
		}

next_extent:
		/* Process all the deferred stuff. */
		error = xfs_defer_finish(&tp);
		if (error)
			goto out_cancel;
	}

	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		goto out;
	return 0;

out_cancel:
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out:
	trace_xfs_reflink_remap_extent_error(ip, error, _RET_IP_);
	return error;
}

/*
 * Iteratively remap one file's extents (and holes) to another's.
 */
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
	xfs_fileoff_t		srcoff;
	xfs_fileoff_t		destoff;
	xfs_filblks_t		len;
	xfs_filblks_t		range_len;
	xfs_filblks_t		remapped_len = 0;
	xfs_off_t		new_isize = pos_out + remap_len;
	int			nimaps;
	int			error = 0;

	destoff = XFS_B_TO_FSBT(src->i_mount, pos_out);
	srcoff = XFS_B_TO_FSBT(src->i_mount, pos_in);
	len = XFS_B_TO_FSB(src->i_mount, remap_len);

	/* drange = (destoff, destoff + len); srange = (srcoff, srcoff + len) */
	while (len) {
		uint		lock_mode;

		trace_xfs_reflink_remap_blocks_loop(src, srcoff, len,
				dest, destoff);

		/* Read extent from the source file */
		nimaps = 1;
		lock_mode = xfs_ilock_data_map_shared(src);
		error = xfs_bmapi_read(src, srcoff, len, &imap, &nimaps, 0);
		xfs_iunlock(src, lock_mode);
		if (error)
			break;
		ASSERT(nimaps == 1);

		trace_xfs_reflink_remap_imap(src, srcoff, len, XFS_DATA_FORK,
				&imap);

		/* Translate imap into the destination file. */
		range_len = imap.br_startoff + imap.br_blockcount - srcoff;
		imap.br_startoff += destoff - srcoff;

		/* Clear dest from destoff to the end of imap and map it in. */
		error = xfs_reflink_remap_extent(dest, &imap, destoff,
				new_isize);
		if (error)
			break;

		if (fatal_signal_pending(current)) {
			error = -EINTR;
			break;
		}

		/* Advance drange/srange */
		srcoff += range_len;
		destoff += range_len;
		len -= range_len;
		remapped_len += range_len;
	}

	if (error)
		trace_xfs_reflink_remap_blocks_error(dest, error, _RET_IP_);
	*remapped = min_t(loff_t, remap_len,
			  XFS_FSB_TO_B(src->i_mount, remapped_len));
	return error;
}

/*
 * Grab the exclusive iolock for a data copy from src to dest, making sure to
 * abide vfs locking order (lowest pointer value goes first) and breaking the
 * layout leases before proceeding.  The loop is needed because we cannot call
 * the blocking break_layout() with the iolocks held, and therefore have to
 * back out both locks.
 */
static int
xfs_iolock_two_inodes_and_break_layout(
	struct inode		*src,
	struct inode		*dest)
{
	int			error;

	if (src > dest)
		swap(src, dest);

retry:
	/* Wait to break both inodes' layouts before we start locking. */
	error = break_layout(src, true);
	if (error)
		return error;
	if (src != dest) {
		error = break_layout(dest, true);
		if (error)
			return error;
	}

	/* Lock one inode and make sure nobody got in and leased it. */
	inode_lock(src);
	error = break_layout(src, false);
	if (error) {
		inode_unlock(src);
		if (error == -EWOULDBLOCK)
			goto retry;
		return error;
	}

	if (src == dest)
		return 0;

	/* Lock the other inode and make sure nobody got in and leased it. */
	inode_lock_nested(dest, I_MUTEX_NONDIR2);
	error = break_layout(dest, false);
	if (error) {
		inode_unlock(src);
		inode_unlock(dest);
		if (error == -EWOULDBLOCK)
			goto retry;
		return error;
	}

	return 0;
}

/* Unlock both inodes after they've been prepped for a range clone. */
void
xfs_reflink_remap_unlock(
	struct file		*file_in,
	struct file		*file_out)
{
	struct inode		*inode_in = file_inode(file_in);
	struct xfs_inode	*src = XFS_I(inode_in);
	struct inode		*inode_out = file_inode(file_out);
	struct xfs_inode	*dest = XFS_I(inode_out);
	bool			same_inode = (inode_in == inode_out);

	xfs_iunlock(dest, XFS_MMAPLOCK_EXCL);
	if (!same_inode)
		xfs_iunlock(src, XFS_MMAPLOCK_EXCL);
	inode_unlock(inode_out);
	if (!same_inode)
		inode_unlock(inode_in);
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
	return iomap_zero_range(VFS_I(ip), isize, pos - isize, NULL,
			&xfs_buffered_write_iomap_ops);
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
	bool			same_inode = (inode_in == inode_out);
	ssize_t			ret;

	/* Lock both files against IO */
	ret = xfs_iolock_two_inodes_and_break_layout(inode_in, inode_out);
	if (ret)
		return ret;
	if (same_inode)
		xfs_ilock(src, XFS_MMAPLOCK_EXCL);
	else
		xfs_lock_two_inodes(src, XFS_MMAPLOCK_EXCL, dest,
				XFS_MMAPLOCK_EXCL);

	/* Check file eligibility and prepare for block sharing. */
	ret = -EINVAL;
	/* Don't reflink realtime inodes */
	if (XFS_IS_REALTIME_INODE(src) || XFS_IS_REALTIME_INODE(dest))
		goto out_unlock;

	/* Don't share DAX file data for now. */
	if (IS_DAX(inode_in) || IS_DAX(inode_out))
		goto out_unlock;

	ret = generic_remap_file_range_prep(file_in, pos_in, file_out, pos_out,
			len, remap_flags);
	if (ret < 0 || *len == 0)
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

	return 1;
out_unlock:
	xfs_reflink_remap_unlock(file_in, file_out);
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
	xfs_agnumber_t			agno;
	xfs_agblock_t			agbno;
	xfs_extlen_t			aglen;
	xfs_agblock_t			rbno;
	xfs_extlen_t			rlen;
	struct xfs_iext_cursor		icur;
	bool				found;
	int				error;

	ifp = XFS_IFORK_PTR(ip, XFS_DATA_FORK);
	if (!(ifp->if_flags & XFS_IFEXTENTS)) {
		error = xfs_iread_extents(tp, ip, XFS_DATA_FORK);
		if (error)
			return error;
	}

	*has_shared = false;
	found = xfs_iext_lookup_extent(ip, ifp, 0, &icur, &got);
	while (found) {
		if (isnullstartblock(got.br_startblock) ||
		    got.br_state != XFS_EXT_NORM)
			goto next;
		agno = XFS_FSB_TO_AGNO(mp, got.br_startblock);
		agbno = XFS_FSB_TO_AGBNO(mp, got.br_startblock);
		aglen = got.br_blockcount;

		error = xfs_reflink_find_shared(mp, tp, agno, agbno, aglen,
				&rbno, &rlen, false);
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
	ip->i_d.di_flags2 &= ~XFS_DIFLAG2_REFLINK;
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

	error = iomap_file_unshare(inode, offset, len,
			&xfs_buffered_write_iomap_ops);
	if (error)
		goto out;
	error = filemap_write_and_wait(inode->i_mapping);
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
