/*
 * Copyright (C) 2016 Oracle.  All Rights Reserved.
 *
 * Author: Darrick J. Wong <darrick.wong@oracle.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include "xfs.h"
#include "xfs_fs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_defer.h"
#include "xfs_da_format.h"
#include "xfs_da_btree.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_inode_item.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_error.h"
#include "xfs_dir2.h"
#include "xfs_dir2_priv.h"
#include "xfs_ioctl.h"
#include "xfs_trace.h"
#include "xfs_log.h"
#include "xfs_icache.h"
#include "xfs_pnfs.h"
#include "xfs_btree.h"
#include "xfs_refcount_btree.h"
#include "xfs_refcount.h"
#include "xfs_bmap_btree.h"
#include "xfs_trans_space.h"
#include "xfs_bit.h"
#include "xfs_alloc.h"
#include "xfs_quota_defs.h"
#include "xfs_quota.h"
#include "xfs_reflink.h"
#include "xfs_iomap.h"
#include "xfs_rmap_btree.h"
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
	if (!agbp)
		return -ENOMEM;

	cur = xfs_refcountbt_init_cursor(mp, tp, agbp, agno, NULL);

	error = xfs_refcount_find_shared(cur, agbno, aglen, fbno, flen,
			find_end_of_shared);

	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);

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
	bool			*shared,
	bool			*trimmed)
{
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	xfs_extlen_t		aglen;
	xfs_agblock_t		fbno;
	xfs_extlen_t		flen;
	int			error = 0;

	/* Holes, unwritten, and delalloc extents cannot be shared */
	if (!xfs_is_reflink_inode(ip) || !xfs_bmap_is_real_extent(irec)) {
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

	*shared = *trimmed = false;
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
		if (flen != aglen)
			*trimmed = true;
		return 0;
	} else {
		/*
		 * There's a shared extent midway through this extent.
		 * Truncate the mapping at the start of the shared
		 * extent so that a subsequent iteration starts at the
		 * start of the shared region.
		 */
		irec->br_blockcount = fbno - agbno;
		*trimmed = true;
		return 0;
	}
}

/*
 * Trim the passed in imap to the next shared/unshared extent boundary, and
 * if imap->br_startoff points to a shared extent reserve space for it in the
 * COW fork.  In this case *shared is set to true, else to false.
 *
 * Note that imap will always contain the block numbers for the existing blocks
 * in the data fork, as the upper layers need them for read-modify-write
 * operations.
 */
int
xfs_reflink_reserve_cow(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	bool			*shared)
{
	struct xfs_ifork	*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	struct xfs_bmbt_irec	got;
	int			error = 0;
	bool			eof = false, trimmed;
	struct xfs_iext_cursor	icur;

	/*
	 * Search the COW fork extent list first.  This serves two purposes:
	 * first this implement the speculative preallocation using cowextisze,
	 * so that we also unshared block adjacent to shared blocks instead
	 * of just the shared blocks themselves.  Second the lookup in the
	 * extent list is generally faster than going out to the shared extent
	 * tree.
	 */

	if (!xfs_iext_lookup_extent(ip, ifp, imap->br_startoff, &icur, &got))
		eof = true;
	if (!eof && got.br_startoff <= imap->br_startoff) {
		trace_xfs_reflink_cow_found(ip, imap);
		xfs_trim_extent(imap, got.br_startoff, got.br_blockcount);

		*shared = true;
		return 0;
	}

	/* Trim the mapping to the nearest shared extent boundary. */
	error = xfs_reflink_trim_around_shared(ip, imap, shared, &trimmed);
	if (error)
		return error;

	/* Not shared?  Just report the (potentially capped) extent. */
	if (!*shared)
		return 0;

	/*
	 * Fork all the shared blocks from our write offset until the end of
	 * the extent.
	 */
	error = xfs_qm_dqattach_locked(ip, 0);
	if (error)
		return error;

	error = xfs_bmapi_reserve_delalloc(ip, XFS_COW_FORK, imap->br_startoff,
			imap->br_blockcount, 0, &got, &icur, eof);
	if (error == -ENOSPC || error == -EDQUOT)
		trace_xfs_reflink_cow_enospc(ip, imap);
	if (error)
		return error;

	trace_xfs_reflink_cow_alloc(ip, &got);
	return 0;
}

/* Convert part of an unwritten CoW extent to a real one. */
STATIC int
xfs_reflink_convert_cow_extent(
	struct xfs_inode		*ip,
	struct xfs_bmbt_irec		*imap,
	xfs_fileoff_t			offset_fsb,
	xfs_filblks_t			count_fsb,
	struct xfs_defer_ops		*dfops)
{
	xfs_fsblock_t			first_block = NULLFSBLOCK;
	int				nimaps = 1;

	if (imap->br_state == XFS_EXT_NORM)
		return 0;

	xfs_trim_extent(imap, offset_fsb, count_fsb);
	trace_xfs_reflink_convert_cow(ip, imap);
	if (imap->br_blockcount == 0)
		return 0;
	return xfs_bmapi_write(NULL, ip, imap->br_startoff, imap->br_blockcount,
			XFS_BMAPI_COWFORK | XFS_BMAPI_CONVERT, &first_block,
			0, imap, &nimaps, dfops);
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
	struct xfs_bmbt_irec	imap;
	struct xfs_defer_ops	dfops;
	xfs_fsblock_t		first_block = NULLFSBLOCK;
	int			nimaps = 1, error = 0;

	ASSERT(count != 0);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	error = xfs_bmapi_write(NULL, ip, offset_fsb, count_fsb,
			XFS_BMAPI_COWFORK | XFS_BMAPI_CONVERT |
			XFS_BMAPI_CONVERT_ONLY, &first_block, 0, &imap, &nimaps,
			&dfops);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
}

/* Allocate all CoW reservations covering a range of blocks in a file. */
int
xfs_reflink_allocate_cow(
	struct xfs_inode	*ip,
	struct xfs_bmbt_irec	*imap,
	bool			*shared,
	uint			*lockmode)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = imap->br_startoff;
	xfs_filblks_t		count_fsb = imap->br_blockcount;
	struct xfs_bmbt_irec	got;
	struct xfs_defer_ops	dfops;
	struct xfs_trans	*tp = NULL;
	xfs_fsblock_t		first_block;
	int			nimaps, error = 0;
	bool			trimmed;
	xfs_filblks_t		resaligned;
	xfs_extlen_t		resblks = 0;
	struct xfs_iext_cursor	icur;

retry:
	ASSERT(xfs_is_reflink_inode(ip));
	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL));

	/*
	 * Even if the extent is not shared we might have a preallocation for
	 * it in the COW fork.  If so use it.
	 */
	if (xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &got) &&
	    got.br_startoff <= offset_fsb) {
		*shared = true;

		/* If we have a real allocation in the COW fork we're done. */
		if (!isnullstartblock(got.br_startblock)) {
			xfs_trim_extent(&got, offset_fsb, count_fsb);
			*imap = got;
			goto convert;
		}

		xfs_trim_extent(imap, got.br_startoff, got.br_blockcount);
	} else {
		error = xfs_reflink_trim_around_shared(ip, imap, shared, &trimmed);
		if (error || !*shared)
			goto out;
	}

	if (!tp) {
		resaligned = xfs_aligned_fsb_count(imap->br_startoff,
			imap->br_blockcount, xfs_get_cowextsz_hint(ip));
		resblks = XFS_DIOSTRAT_SPACE_RES(mp, resaligned);

		xfs_iunlock(ip, *lockmode);
		error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, resblks, 0, 0, &tp);
		*lockmode = XFS_ILOCK_EXCL;
		xfs_ilock(ip, *lockmode);

		if (error)
			return error;

		error = xfs_qm_dqattach_locked(ip, 0);
		if (error)
			goto out;
		goto retry;
	}

	error = xfs_trans_reserve_quota_nblks(tp, ip, resblks, 0,
			XFS_QMOPT_RES_REGBLKS);
	if (error)
		goto out;

	xfs_trans_ijoin(tp, ip, 0);

	xfs_defer_init(&dfops, &first_block);
	nimaps = 1;

	/* Allocate the entire reservation as unwritten blocks. */
	error = xfs_bmapi_write(tp, ip, imap->br_startoff, imap->br_blockcount,
			XFS_BMAPI_COWFORK | XFS_BMAPI_PREALLOC, &first_block,
			resblks, imap, &nimaps, &dfops);
	if (error)
		goto out_bmap_cancel;

	xfs_inode_set_cowblocks_tag(ip);

	/* Finish up. */
	error = xfs_defer_finish(&tp, &dfops);
	if (error)
		goto out_bmap_cancel;

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
	return xfs_reflink_convert_cow_extent(ip, imap, offset_fsb, count_fsb,
			&dfops);
out_bmap_cancel:
	xfs_defer_cancel(&dfops);
	xfs_trans_unreserve_quota_nblks(tp, ip, (long)resblks, 0,
			XFS_QMOPT_RES_REGBLKS);
out:
	if (tp)
		xfs_trans_cancel(tp);
	return error;
}

/*
 * Find the CoW reservation for a given byte offset of a file.
 */
bool
xfs_reflink_find_cow_mapping(
	struct xfs_inode		*ip,
	xfs_off_t			offset,
	struct xfs_bmbt_irec		*imap)
{
	struct xfs_ifork		*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	xfs_fileoff_t			offset_fsb;
	struct xfs_bmbt_irec		got;
	struct xfs_iext_cursor		icur;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL | XFS_ILOCK_SHARED));

	if (!xfs_is_reflink_inode(ip))
		return false;
	offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
	if (!xfs_iext_lookup_extent(ip, ifp, offset_fsb, &icur, &got))
		return false;
	if (got.br_startoff > offset_fsb)
		return false;

	trace_xfs_reflink_find_cow_mapping(ip, offset, 1, XFS_IO_OVERWRITE,
			&got);
	*imap = got;
	return true;
}

/*
 * Trim an extent to end at the next CoW reservation past offset_fsb.
 */
void
xfs_reflink_trim_irec_to_next_cow(
	struct xfs_inode		*ip,
	xfs_fileoff_t			offset_fsb,
	struct xfs_bmbt_irec		*imap)
{
	struct xfs_ifork		*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	struct xfs_bmbt_irec		got;
	struct xfs_iext_cursor		icur;

	if (!xfs_is_reflink_inode(ip))
		return;

	/* Find the extent in the CoW fork. */
	if (!xfs_iext_lookup_extent(ip, ifp, offset_fsb, &icur, &got))
		return;

	/* This is the extent before; try sliding up one. */
	if (got.br_startoff < offset_fsb) {
		if (!xfs_iext_next_extent(ifp, &icur, &got))
			return;
	}

	if (got.br_startoff >= imap->br_startoff + imap->br_blockcount)
		return;

	imap->br_blockcount = got.br_startoff - imap->br_startoff;
	trace_xfs_reflink_trim_irec(ip, imap);
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
	xfs_fsblock_t			firstfsb;
	struct xfs_defer_ops		dfops;
	int				error = 0;

	if (!xfs_is_reflink_inode(ip))
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
			xfs_defer_init(&dfops, &firstfsb);

			/* Free the CoW orphan record. */
			error = xfs_refcount_free_cow_extent(ip->i_mount,
					&dfops, del.br_startblock,
					del.br_blockcount);
			if (error)
				break;

			xfs_bmap_add_free(ip->i_mount, &dfops,
					del.br_startblock, del.br_blockcount,
					NULL);

			/* Roll the transaction */
			xfs_defer_ijoin(&dfops, ip);
			error = xfs_defer_finish(tpp, &dfops);
			if (error) {
				xfs_defer_cancel(&dfops);
				break;
			}

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
	ASSERT(xfs_is_reflink_inode(ip));

	offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
	if (count == NULLFILEOFF)
		end_fsb = NULLFILEOFF;
	else
		end_fsb = XFS_B_TO_FSB(ip->i_mount, offset + count);

	/* Start a rolling transaction to remove the mappings */
	error = xfs_trans_alloc(ip->i_mount, &M_RES(ip->i_mount)->tr_write,
			0, 0, XFS_TRANS_NOFS, &tp);
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
 * Remap parts of a file's data fork after a successful CoW.
 */
int
xfs_reflink_end_cow(
	struct xfs_inode		*ip,
	xfs_off_t			offset,
	xfs_off_t			count)
{
	struct xfs_ifork		*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	struct xfs_bmbt_irec		got, del;
	struct xfs_trans		*tp;
	xfs_fileoff_t			offset_fsb;
	xfs_fileoff_t			end_fsb;
	xfs_fsblock_t			firstfsb;
	struct xfs_defer_ops		dfops;
	int				error;
	unsigned int			resblks;
	xfs_filblks_t			rlen;
	struct xfs_iext_cursor		icur;

	trace_xfs_reflink_end_cow(ip, offset, count);

	/* No COW extents?  That's easy! */
	if (ifp->if_bytes == 0)
		return 0;

	offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
	end_fsb = XFS_B_TO_FSB(ip->i_mount, offset + count);

	/*
	 * Start a rolling transaction to switch the mappings.  We're
	 * unlikely ever to have to remap 16T worth of single-block
	 * extents, so just cap the worst case extent count to 2^32-1.
	 * Stick a warning in just in case, and avoid 64-bit division.
	 */
	BUILD_BUG_ON(MAX_RW_COUNT > UINT_MAX);
	if (end_fsb - offset_fsb > UINT_MAX) {
		error = -EFSCORRUPTED;
		xfs_force_shutdown(ip->i_mount, SHUTDOWN_CORRUPT_INCORE);
		ASSERT(0);
		goto out;
	}
	resblks = XFS_NEXTENTADD_SPACE_RES(ip->i_mount,
			(unsigned int)(end_fsb - offset_fsb),
			XFS_DATA_FORK);
	error = xfs_trans_alloc(ip->i_mount, &M_RES(ip->i_mount)->tr_write,
			resblks, 0, XFS_TRANS_RESERVE | XFS_TRANS_NOFS, &tp);
	if (error)
		goto out;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/*
	 * In case of racing, overlapping AIO writes no COW extents might be
	 * left by the time I/O completes for the loser of the race.  In that
	 * case we are done.
	 */
	if (!xfs_iext_lookup_extent_before(ip, ifp, &end_fsb, &icur, &got))
		goto out_cancel;

	/* Walk backwards until we're out of the I/O range... */
	while (got.br_startoff + got.br_blockcount > offset_fsb) {
		del = got;
		xfs_trim_extent(&del, offset_fsb, end_fsb - offset_fsb);

		/* Extent delete may have bumped ext forward */
		if (!del.br_blockcount)
			goto prev_extent;

		ASSERT(!isnullstartblock(got.br_startblock));

		/*
		 * Don't remap unwritten extents; these are
		 * speculatively preallocated CoW extents that have been
		 * allocated but have not yet been involved in a write.
		 */
		if (got.br_state == XFS_EXT_UNWRITTEN)
			goto prev_extent;

		/* Unmap the old blocks in the data fork. */
		xfs_defer_init(&dfops, &firstfsb);
		rlen = del.br_blockcount;
		error = __xfs_bunmapi(tp, ip, del.br_startoff, &rlen, 0, 1,
				&firstfsb, &dfops);
		if (error)
			goto out_defer;

		/* Trim the extent to whatever got unmapped. */
		if (rlen) {
			xfs_trim_extent(&del, del.br_startoff + rlen,
				del.br_blockcount - rlen);
		}
		trace_xfs_reflink_cow_remap(ip, &del);

		/* Free the CoW orphan record. */
		error = xfs_refcount_free_cow_extent(tp->t_mountp, &dfops,
				del.br_startblock, del.br_blockcount);
		if (error)
			goto out_defer;

		/* Map the new blocks into the data fork. */
		error = xfs_bmap_map_extent(tp->t_mountp, &dfops, ip, &del);
		if (error)
			goto out_defer;

		/* Charge this new data fork mapping to the on-disk quota. */
		xfs_trans_mod_dquot_byino(tp, ip, XFS_TRANS_DQ_DELBCOUNT,
				(long)del.br_blockcount);

		/* Remove the mapping from the CoW fork. */
		xfs_bmap_del_extent_cow(ip, &icur, &got, &del);

		xfs_defer_ijoin(&dfops, ip);
		error = xfs_defer_finish(&tp, &dfops);
		if (error)
			goto out_defer;
		if (!xfs_iext_get_extent(ifp, &icur, &got))
			break;
		continue;
prev_extent:
		if (!xfs_iext_prev_extent(ifp, &icur, &got))
			break;
	}

	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		goto out;
	return 0;

out_defer:
	xfs_defer_cancel(&dfops);
out_cancel:
	xfs_trans_cancel(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out:
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
STATIC int
xfs_reflink_update_dest(
	struct xfs_inode	*dest,
	xfs_off_t		newlen,
	xfs_extlen_t		cowextsize,
	bool			is_dedupe)
{
	struct xfs_mount	*mp = dest->i_mount;
	struct xfs_trans	*tp;
	int			error;

	if (is_dedupe && newlen <= i_size_read(VFS_I(dest)) && cowextsize == 0)
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

	if (!is_dedupe) {
		xfs_trans_ichgtime(tp, dest,
				   XFS_ICHGTIME_MOD | XFS_ICHGTIME_CHG);
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
	xfs_fsblock_t		firstfsb;
	unsigned int		resblks;
	struct xfs_defer_ops	dfops;
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
		xfs_defer_init(&dfops, &firstfsb);
		error = __xfs_bunmapi(tp, ip, destoff, &rlen, 0, 1,
				&firstfsb, &dfops);
		if (error)
			goto out_defer;

		/*
		 * Trim the extent to whatever got unmapped.
		 * Remember, bunmapi works backwards.
		 */
		uirec.br_startblock = irec->br_startblock + rlen;
		uirec.br_startoff = irec->br_startoff + rlen;
		uirec.br_blockcount = unmap_len - rlen;
		unmap_len = rlen;

		/* If this isn't a real mapping, we're done. */
		if (!real_extent || uirec.br_blockcount == 0)
			goto next_extent;

		trace_xfs_reflink_remap(ip, uirec.br_startoff,
				uirec.br_blockcount, uirec.br_startblock);

		/* Update the refcount tree */
		error = xfs_refcount_increase_extent(mp, &dfops, &uirec);
		if (error)
			goto out_defer;

		/* Map the new blocks into the data fork. */
		error = xfs_bmap_map_extent(mp, &dfops, ip, &uirec);
		if (error)
			goto out_defer;

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
		xfs_defer_ijoin(&dfops, ip);
		error = xfs_defer_finish(&tp, &dfops);
		if (error)
			goto out_defer;
	}

	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		goto out;
	return 0;

out_defer:
	xfs_defer_cancel(&dfops);
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
STATIC int
xfs_reflink_remap_blocks(
	struct xfs_inode	*src,
	xfs_fileoff_t		srcoff,
	struct xfs_inode	*dest,
	xfs_fileoff_t		destoff,
	xfs_filblks_t		len,
	xfs_off_t		new_isize)
{
	struct xfs_bmbt_irec	imap;
	int			nimaps;
	int			error = 0;
	xfs_filblks_t		range_len;

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
			goto err;
		ASSERT(nimaps == 1);

		trace_xfs_reflink_remap_imap(src, srcoff, len, XFS_IO_OVERWRITE,
				&imap);

		/* Translate imap into the destination file. */
		range_len = imap.br_startoff + imap.br_blockcount - srcoff;
		imap.br_startoff += destoff - srcoff;

		/* Clear dest from destoff to the end of imap and map it in. */
		error = xfs_reflink_remap_extent(dest, &imap, destoff,
				new_isize);
		if (error)
			goto err;

		if (fatal_signal_pending(current)) {
			error = -EINTR;
			goto err;
		}

		/* Advance drange/srange */
		srcoff += range_len;
		destoff += range_len;
		len -= range_len;
	}

	return 0;

err:
	trace_xfs_reflink_remap_blocks_error(dest, error, _RET_IP_);
	return error;
}

/*
 * Grab the exclusive iolock for a data copy from src to dest, making
 * sure to abide vfs locking order (lowest pointer value goes first) and
 * breaking the pnfs layout leases on dest before proceeding.  The loop
 * is needed because we cannot call the blocking break_layout() with the
 * src iolock held, and therefore have to back out both locks.
 */
static int
xfs_iolock_two_inodes_and_break_layout(
	struct inode		*src,
	struct inode		*dest)
{
	int			error;

retry:
	if (src < dest) {
		inode_lock_shared(src);
		inode_lock_nested(dest, I_MUTEX_NONDIR2);
	} else {
		/* src >= dest */
		inode_lock(dest);
	}

	error = break_layout(dest, false);
	if (error == -EWOULDBLOCK) {
		inode_unlock(dest);
		if (src < dest)
			inode_unlock_shared(src);
		error = break_layout(dest, true);
		if (error)
			return error;
		goto retry;
	}
	if (error) {
		inode_unlock(dest);
		if (src < dest)
			inode_unlock_shared(src);
		return error;
	}
	if (src > dest)
		inode_lock_shared_nested(src, I_MUTEX_NONDIR2);
	return 0;
}

/*
 * Link a range of blocks from one file to another.
 */
int
xfs_reflink_remap_range(
	struct file		*file_in,
	loff_t			pos_in,
	struct file		*file_out,
	loff_t			pos_out,
	u64			len,
	bool			is_dedupe)
{
	struct inode		*inode_in = file_inode(file_in);
	struct xfs_inode	*src = XFS_I(inode_in);
	struct inode		*inode_out = file_inode(file_out);
	struct xfs_inode	*dest = XFS_I(inode_out);
	struct xfs_mount	*mp = src->i_mount;
	bool			same_inode = (inode_in == inode_out);
	xfs_fileoff_t		sfsbno, dfsbno;
	xfs_filblks_t		fsblen;
	xfs_extlen_t		cowextsize;
	ssize_t			ret;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return -EOPNOTSUPP;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	/* Lock both files against IO */
	ret = xfs_iolock_two_inodes_and_break_layout(inode_in, inode_out);
	if (ret)
		return ret;
	if (same_inode)
		xfs_ilock(src, XFS_MMAPLOCK_EXCL);
	else
		xfs_lock_two_inodes(src, XFS_MMAPLOCK_SHARED, dest,
				XFS_MMAPLOCK_EXCL);

	/* Check file eligibility and prepare for block sharing. */
	ret = -EINVAL;
	/* Don't reflink realtime inodes */
	if (XFS_IS_REALTIME_INODE(src) || XFS_IS_REALTIME_INODE(dest))
		goto out_unlock;

	/* Don't share DAX file data for now. */
	if (IS_DAX(inode_in) || IS_DAX(inode_out))
		goto out_unlock;

	ret = vfs_clone_file_prep_inodes(inode_in, pos_in, inode_out, pos_out,
			&len, is_dedupe);
	if (ret <= 0)
		goto out_unlock;

	/* Attach dquots to dest inode before changing block map */
	ret = xfs_qm_dqattach(dest, 0);
	if (ret)
		goto out_unlock;

	trace_xfs_reflink_remap_range(src, pos_in, len, dest, pos_out);

	/*
	 * Clear out post-eof preallocations because we don't have page cache
	 * backing the delayed allocations and they'll never get freed on
	 * their own.
	 */
	if (xfs_can_free_eofblocks(dest, true)) {
		ret = xfs_free_eofblocks(dest);
		if (ret)
			goto out_unlock;
	}

	/* Set flags and remap blocks. */
	ret = xfs_reflink_set_inode_flag(src, dest);
	if (ret)
		goto out_unlock;

	dfsbno = XFS_B_TO_FSBT(mp, pos_out);
	sfsbno = XFS_B_TO_FSBT(mp, pos_in);
	fsblen = XFS_B_TO_FSB(mp, len);
	ret = xfs_reflink_remap_blocks(src, sfsbno, dest, dfsbno, fsblen,
			pos_out + len);
	if (ret)
		goto out_unlock;

	/* Zap any page cache for the destination file's range. */
	truncate_inode_pages_range(&inode_out->i_data, pos_out,
				   PAGE_ALIGN(pos_out + len) - 1);

	/*
	 * Carry the cowextsize hint from src to dest if we're sharing the
	 * entire source file to the entire destination file, the source file
	 * has a cowextsize hint, and the destination file does not.
	 */
	cowextsize = 0;
	if (pos_in == 0 && len == i_size_read(inode_in) &&
	    (src->i_d.di_flags2 & XFS_DIFLAG2_COWEXTSIZE) &&
	    pos_out == 0 && len >= i_size_read(inode_out) &&
	    !(dest->i_d.di_flags2 & XFS_DIFLAG2_COWEXTSIZE))
		cowextsize = src->i_d.di_cowextsize;

	ret = xfs_reflink_update_dest(dest, pos_out + len, cowextsize,
			is_dedupe);

out_unlock:
	xfs_iunlock(dest, XFS_MMAPLOCK_EXCL);
	if (!same_inode)
		xfs_iunlock(src, XFS_MMAPLOCK_SHARED);
	inode_unlock(inode_out);
	if (!same_inode)
		inode_unlock_shared(inode_in);
	if (ret)
		trace_xfs_reflink_remap_range_error(dest, ret, _RET_IP_);
	return ret;
}

/*
 * The user wants to preemptively CoW all shared blocks in this file,
 * which enables us to turn off the reflink flag.  Iterate all
 * extents which are not prealloc/delalloc to see which ranges are
 * mentioned in the refcount tree, then read those blocks into the
 * pagecache, dirty them, fsync them back out, and then we can update
 * the inode flag.  What happens if we run out of memory? :)
 */
STATIC int
xfs_reflink_dirty_extents(
	struct xfs_inode	*ip,
	xfs_fileoff_t		fbno,
	xfs_filblks_t		end,
	xfs_off_t		isize)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	xfs_extlen_t		aglen;
	xfs_agblock_t		rbno;
	xfs_extlen_t		rlen;
	xfs_off_t		fpos;
	xfs_off_t		flen;
	struct xfs_bmbt_irec	map[2];
	int			nmaps;
	int			error = 0;

	while (end - fbno > 0) {
		nmaps = 1;
		/*
		 * Look for extents in the file.  Skip holes, delalloc, or
		 * unwritten extents; they can't be reflinked.
		 */
		error = xfs_bmapi_read(ip, fbno, end - fbno, map, &nmaps, 0);
		if (error)
			goto out;
		if (nmaps == 0)
			break;
		if (!xfs_bmap_is_real_extent(&map[0]))
			goto next;

		map[1] = map[0];
		while (map[1].br_blockcount) {
			agno = XFS_FSB_TO_AGNO(mp, map[1].br_startblock);
			agbno = XFS_FSB_TO_AGBNO(mp, map[1].br_startblock);
			aglen = map[1].br_blockcount;

			error = xfs_reflink_find_shared(mp, NULL, agno, agbno,
					aglen, &rbno, &rlen, true);
			if (error)
				goto out;
			if (rbno == NULLAGBLOCK)
				break;

			/* Dirty the pages */
			xfs_iunlock(ip, XFS_ILOCK_EXCL);
			fpos = XFS_FSB_TO_B(mp, map[1].br_startoff +
					(rbno - agbno));
			flen = XFS_FSB_TO_B(mp, rlen);
			if (fpos + flen > isize)
				flen = isize - fpos;
			error = iomap_file_dirty(VFS_I(ip), fpos, flen,
					&xfs_iomap_ops);
			xfs_ilock(ip, XFS_ILOCK_EXCL);
			if (error)
				goto out;

			map[1].br_blockcount -= (rbno - agbno + rlen);
			map[1].br_startoff += (rbno - agbno + rlen);
			map[1].br_startblock += (rbno - agbno + rlen);
		}

next:
		fbno = map[0].br_startoff + map[0].br_blockcount;
	}
out:
	return error;
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

/* Clear the inode reflink flag if there are no shared extents. */
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
	xfs_trans_ijoin(*tpp, ip, 0);
	error = xfs_reflink_cancel_cow_blocks(ip, tpp, 0, NULLFILEOFF, true);
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
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		fbno;
	xfs_filblks_t		end;
	xfs_off_t		isize;
	int			error;

	if (!xfs_is_reflink_inode(ip))
		return 0;

	trace_xfs_reflink_unshare(ip, offset, len);

	inode_dio_wait(VFS_I(ip));

	/* Try to CoW the selected ranges */
	xfs_ilock(ip, XFS_ILOCK_EXCL);
	fbno = XFS_B_TO_FSBT(mp, offset);
	isize = i_size_read(VFS_I(ip));
	end = XFS_B_TO_FSB(mp, offset + len);
	error = xfs_reflink_dirty_extents(ip, fbno, end, isize);
	if (error)
		goto out_unlock;
	xfs_iunlock(ip, XFS_ILOCK_EXCL);

	/* Wait for the IO to finish */
	error = filemap_write_and_wait(VFS_I(ip)->i_mapping);
	if (error)
		goto out;

	/* Turn off the reflink flag if possible. */
	error = xfs_reflink_try_clear_inode_flag(ip);
	if (error)
		goto out;

	return 0;

out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out:
	trace_xfs_reflink_unshare_error(ip, error, _RET_IP_);
	return error;
}
