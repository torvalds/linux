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
#include "xfs_btree.h"
#include "xfs_bmap_btree.h"
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
 * When dirty pages are being written out (typically in writepage), the
 * delalloc reservations are converted into real mappings by allocating
 * blocks and replacing the delalloc mapping with real ones.  A delalloc
 * mapping can be replaced by several real ones if the free space is
 * fragmented.
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
 * CoW remapping must be done after the data block write completes,
 * because we don't want to destroy the old data fork map until we're sure
 * the new block has been written.  Since the new mappings are kept in a
 * separate fork, we can simply iterate these mappings to find the ones
 * that cover the file blocks that we just CoW'd.  For each extent, simply
 * unmap the corresponding range in the data fork, map the new range into
 * the data fork, and remove the extent from the CoW fork.
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

	error = xfs_alloc_read_agf(mp, NULL, agno, 0, &agbp);
	if (error)
		return error;

	cur = xfs_refcountbt_init_cursor(mp, NULL, agbp, agno, NULL);

	error = xfs_refcount_find_shared(cur, agbno, aglen, fbno, flen,
			find_end_of_shared);

	xfs_btree_del_cursor(cur, error ? XFS_BTREE_ERROR : XFS_BTREE_NOERROR);

	xfs_buf_relse(agbp);
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
	if (!xfs_is_reflink_inode(ip) ||
	    ISUNWRITTEN(irec) ||
	    irec->br_startblock == HOLESTARTBLOCK ||
	    irec->br_startblock == DELAYSTARTBLOCK ||
	    isnullstartblock(irec->br_startblock)) {
		*shared = false;
		return 0;
	}

	trace_xfs_reflink_trim_around_shared(ip, irec);

	agno = XFS_FSB_TO_AGNO(ip->i_mount, irec->br_startblock);
	agbno = XFS_FSB_TO_AGBNO(ip->i_mount, irec->br_startblock);
	aglen = irec->br_blockcount;

	error = xfs_reflink_find_shared(ip->i_mount, agno, agbno,
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
	struct xfs_bmbt_irec	got, prev;
	xfs_fileoff_t		end_fsb, orig_end_fsb;
	int			eof = 0, error = 0;
	bool			trimmed;
	xfs_extnum_t		idx;
	xfs_extlen_t		align;

	/*
	 * Search the COW fork extent list first.  This serves two purposes:
	 * first this implement the speculative preallocation using cowextisze,
	 * so that we also unshared block adjacent to shared blocks instead
	 * of just the shared blocks themselves.  Second the lookup in the
	 * extent list is generally faster than going out to the shared extent
	 * tree.
	 */
	xfs_bmap_search_extents(ip, imap->br_startoff, XFS_COW_FORK, &eof, &idx,
			&got, &prev);
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

	end_fsb = orig_end_fsb = imap->br_startoff + imap->br_blockcount;

	align = xfs_eof_alignment(ip, xfs_get_cowextsz_hint(ip));
	if (align)
		end_fsb = roundup_64(end_fsb, align);

retry:
	error = xfs_bmapi_reserve_delalloc(ip, XFS_COW_FORK, imap->br_startoff,
			end_fsb - imap->br_startoff, &got, &prev, &idx, eof);
	switch (error) {
	case 0:
		break;
	case -ENOSPC:
	case -EDQUOT:
		/* retry without any preallocation */
		trace_xfs_reflink_cow_enospc(ip, imap);
		if (end_fsb != orig_end_fsb) {
			end_fsb = orig_end_fsb;
			goto retry;
		}
		/*FALLTHRU*/
	default:
		return error;
	}

	if (end_fsb != orig_end_fsb)
		xfs_inode_set_cowblocks_tag(ip);

	trace_xfs_reflink_cow_alloc(ip, &got);
	return 0;
}

/* Allocate all CoW reservations covering a range of blocks in a file. */
static int
__xfs_reflink_allocate_cow(
	struct xfs_inode	*ip,
	xfs_fileoff_t		*offset_fsb,
	xfs_fileoff_t		end_fsb)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_bmbt_irec	imap;
	struct xfs_defer_ops	dfops;
	struct xfs_trans	*tp;
	xfs_fsblock_t		first_block;
	int			nimaps = 1, error;
	bool			shared;

	xfs_defer_init(&dfops, &first_block);

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_write, 0, 0,
			XFS_TRANS_RESERVE, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);

	/* Read extent from the source file. */
	nimaps = 1;
	error = xfs_bmapi_read(ip, *offset_fsb, end_fsb - *offset_fsb,
			&imap, &nimaps, 0);
	if (error)
		goto out_unlock;
	ASSERT(nimaps == 1);

	error = xfs_reflink_reserve_cow(ip, &imap, &shared);
	if (error)
		goto out_trans_cancel;

	if (!shared) {
		*offset_fsb = imap.br_startoff + imap.br_blockcount;
		goto out_trans_cancel;
	}

	xfs_trans_ijoin(tp, ip, 0);
	error = xfs_bmapi_write(tp, ip, imap.br_startoff, imap.br_blockcount,
			XFS_BMAPI_COWFORK, &first_block,
			XFS_EXTENTADD_SPACE_RES(mp, XFS_DATA_FORK),
			&imap, &nimaps, &dfops);
	if (error)
		goto out_trans_cancel;

	error = xfs_defer_finish(&tp, &dfops, NULL);
	if (error)
		goto out_trans_cancel;

	error = xfs_trans_commit(tp);

	*offset_fsb = imap.br_startoff + imap.br_blockcount;
out_unlock:
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return error;
out_trans_cancel:
	xfs_defer_cancel(&dfops);
	xfs_trans_cancel(tp);
	goto out_unlock;
}

/* Allocate all CoW reservations covering a part of a file. */
int
xfs_reflink_allocate_cow_range(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		count)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + count);
	int			error;

	ASSERT(xfs_is_reflink_inode(ip));

	trace_xfs_reflink_allocate_cow_range(ip, offset, count);

	/*
	 * Make sure that the dquots are there.
	 */
	error = xfs_qm_dqattach(ip, 0);
	if (error)
		return error;

	while (offset_fsb < end_fsb) {
		error = __xfs_reflink_allocate_cow(ip, &offset_fsb, end_fsb);
		if (error) {
			trace_xfs_reflink_allocate_cow_range_error(ip, error,
					_RET_IP_);
			break;
		}
	}

	return error;
}

/*
 * Find the CoW reservation (and whether or not it needs block allocation)
 * for a given byte offset of a file.
 */
bool
xfs_reflink_find_cow_mapping(
	struct xfs_inode		*ip,
	xfs_off_t			offset,
	struct xfs_bmbt_irec		*imap,
	bool				*need_alloc)
{
	struct xfs_bmbt_irec		irec;
	struct xfs_ifork		*ifp;
	struct xfs_bmbt_rec_host	*gotp;
	xfs_fileoff_t			bno;
	xfs_extnum_t			idx;

	ASSERT(xfs_isilocked(ip, XFS_ILOCK_EXCL | XFS_ILOCK_SHARED));
	ASSERT(xfs_is_reflink_inode(ip));

	/* Find the extent in the CoW fork. */
	ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	bno = XFS_B_TO_FSBT(ip->i_mount, offset);
	gotp = xfs_iext_bno_to_ext(ifp, bno, &idx);
	if (!gotp)
		return false;

	xfs_bmbt_get_all(gotp, &irec);
	if (bno >= irec.br_startoff + irec.br_blockcount ||
	    bno < irec.br_startoff)
		return false;

	trace_xfs_reflink_find_cow_mapping(ip, offset, 1, XFS_IO_OVERWRITE,
			&irec);

	/* If it's still delalloc, we must allocate later. */
	*imap = irec;
	*need_alloc = !!(isnullstartblock(irec.br_startblock));

	return true;
}

/*
 * Trim an extent to end at the next CoW reservation past offset_fsb.
 */
int
xfs_reflink_trim_irec_to_next_cow(
	struct xfs_inode		*ip,
	xfs_fileoff_t			offset_fsb,
	struct xfs_bmbt_irec		*imap)
{
	struct xfs_bmbt_irec		irec;
	struct xfs_ifork		*ifp;
	struct xfs_bmbt_rec_host	*gotp;
	xfs_extnum_t			idx;

	if (!xfs_is_reflink_inode(ip))
		return 0;

	/* Find the extent in the CoW fork. */
	ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	gotp = xfs_iext_bno_to_ext(ifp, offset_fsb, &idx);
	if (!gotp)
		return 0;
	xfs_bmbt_get_all(gotp, &irec);

	/* This is the extent before; try sliding up one. */
	if (irec.br_startoff < offset_fsb) {
		idx++;
		if (idx >= xfs_iext_count(ifp))
			return 0;
		gotp = xfs_iext_get_ext(ifp, idx);
		xfs_bmbt_get_all(gotp, &irec);
	}

	if (irec.br_startoff >= imap->br_startoff + imap->br_blockcount)
		return 0;

	imap->br_blockcount = irec.br_startoff - imap->br_startoff;
	trace_xfs_reflink_trim_irec(ip, imap);

	return 0;
}

/*
 * Cancel all pending CoW reservations for some block range of an inode.
 */
int
xfs_reflink_cancel_cow_blocks(
	struct xfs_inode		*ip,
	struct xfs_trans		**tpp,
	xfs_fileoff_t			offset_fsb,
	xfs_fileoff_t			end_fsb)
{
	struct xfs_ifork		*ifp = XFS_IFORK_PTR(ip, XFS_COW_FORK);
	struct xfs_bmbt_irec		got, prev, del;
	xfs_extnum_t			idx;
	xfs_fsblock_t			firstfsb;
	struct xfs_defer_ops		dfops;
	int				error = 0, eof = 0;

	if (!xfs_is_reflink_inode(ip))
		return 0;

	xfs_bmap_search_extents(ip, offset_fsb, XFS_COW_FORK, &eof, &idx,
			&got, &prev);
	if (eof)
		return 0;

	while (got.br_startoff < end_fsb) {
		del = got;
		xfs_trim_extent(&del, offset_fsb, end_fsb - offset_fsb);
		trace_xfs_reflink_cancel_cow(ip, &del);

		if (isnullstartblock(del.br_startblock)) {
			error = xfs_bmap_del_extent_delay(ip, XFS_COW_FORK,
					&idx, &got, &del);
			if (error)
				break;
		} else {
			xfs_trans_ijoin(*tpp, ip, 0);
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

			/* Update quota accounting */
			xfs_trans_mod_dquot_byino(*tpp, ip, XFS_TRANS_DQ_BCOUNT,
					-(long)del.br_blockcount);

			/* Roll the transaction */
			error = xfs_defer_finish(tpp, &dfops, ip);
			if (error) {
				xfs_defer_cancel(&dfops);
				break;
			}

			/* Remove the mapping from the CoW fork. */
			xfs_bmap_del_extent_cow(ip, &idx, &got, &del);
		}

		if (++idx >= xfs_iext_count(ifp))
			break;
		xfs_bmbt_get_all(xfs_iext_get_ext(ifp, idx), &got);
	}

	/* clear tag if cow fork is emptied */
	if (!ifp->if_bytes)
		xfs_inode_clear_cowblocks_tag(ip);

	return error;
}

/*
 * Cancel all pending CoW reservations for some byte range of an inode.
 */
int
xfs_reflink_cancel_cow_range(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	xfs_off_t		count)
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
			0, 0, 0, &tp);
	if (error)
		goto out;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	/* Scrape out the old CoW reservations */
	error = xfs_reflink_cancel_cow_blocks(ip, &tp, offset_fsb, end_fsb);
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
	struct xfs_bmbt_irec		got, prev, del;
	struct xfs_trans		*tp;
	xfs_fileoff_t			offset_fsb;
	xfs_fileoff_t			end_fsb;
	xfs_fsblock_t			firstfsb;
	struct xfs_defer_ops		dfops;
	int				error, eof = 0;
	unsigned int			resblks;
	xfs_filblks_t			rlen;
	xfs_extnum_t			idx;

	trace_xfs_reflink_end_cow(ip, offset, count);

	/* No COW extents?  That's easy! */
	if (ifp->if_bytes == 0)
		return 0;

	offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
	end_fsb = XFS_B_TO_FSB(ip->i_mount, offset + count);

	/* Start a rolling transaction to switch the mappings */
	resblks = XFS_EXTENTADD_SPACE_RES(ip->i_mount, XFS_DATA_FORK);
	error = xfs_trans_alloc(ip->i_mount, &M_RES(ip->i_mount)->tr_write,
			resblks, 0, 0, &tp);
	if (error)
		goto out;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	xfs_trans_ijoin(tp, ip, 0);

	xfs_bmap_search_extents(ip, end_fsb - 1, XFS_COW_FORK, &eof, &idx,
			&got, &prev);

	/* If there is a hole at end_fsb - 1 go to the previous extent */
	if (eof || got.br_startoff > end_fsb) {
		ASSERT(idx > 0);
		xfs_bmbt_get_all(xfs_iext_get_ext(ifp, --idx), &got);
	}

	/* Walk backwards until we're out of the I/O range... */
	while (got.br_startoff + got.br_blockcount > offset_fsb) {
		del = got;
		xfs_trim_extent(&del, offset_fsb, end_fsb - offset_fsb);

		/* Extent delete may have bumped idx forward */
		if (!del.br_blockcount) {
			idx--;
			goto next_extent;
		}

		ASSERT(!isnullstartblock(got.br_startblock));

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

		/* Remove the mapping from the CoW fork. */
		xfs_bmap_del_extent_cow(ip, &idx, &got, &del);

		error = xfs_defer_finish(&tp, &dfops, ip);
		if (error)
			goto out_defer;

next_extent:
		if (idx < 0)
			break;
		xfs_bmbt_get_all(xfs_iext_get_ext(ifp, idx), &got);
	}

	error = xfs_trans_commit(tp);
	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	if (error)
		goto out;
	return 0;

out_defer:
	xfs_defer_cancel(&dfops);
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
		xfs_lock_two_inodes(src, dest, XFS_ILOCK_EXCL);

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
	xfs_extlen_t		cowextsize)
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
	if (xfs_ag_resv_critical(pag, XFS_AG_RESV_AGFL) ||
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
	struct xfs_trans	*tp;
	xfs_fsblock_t		firstfsb;
	unsigned int		resblks;
	struct xfs_defer_ops	dfops;
	struct xfs_bmbt_irec	uirec;
	bool			real_extent;
	xfs_filblks_t		rlen;
	xfs_filblks_t		unmap_len;
	xfs_off_t		newlen;
	int			error;

	unmap_len = irec->br_startoff + irec->br_blockcount - destoff;
	trace_xfs_reflink_punch_range(ip, destoff, unmap_len);

	/* Only remap normal extents. */
	real_extent =  (irec->br_startblock != HOLESTARTBLOCK &&
			irec->br_startblock != DELAYSTARTBLOCK &&
			!ISUNWRITTEN(irec));

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
		error = xfs_defer_finish(&tp, &dfops, ip);
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
		trace_xfs_reflink_remap_blocks_loop(src, srcoff, len,
				dest, destoff);
		/* Read extent from the source file */
		nimaps = 1;
		xfs_ilock(src, XFS_ILOCK_EXCL);
		error = xfs_bmapi_read(src, srcoff, len, &imap, &nimaps, 0);
		xfs_iunlock(src, XFS_ILOCK_EXCL);
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
 * Read a page's worth of file data into the page cache.  Return the page
 * locked.
 */
static struct page *
xfs_get_page(
	struct inode	*inode,
	xfs_off_t	offset)
{
	struct address_space	*mapping;
	struct page		*page;
	pgoff_t			n;

	n = offset >> PAGE_SHIFT;
	mapping = inode->i_mapping;
	page = read_mapping_page(mapping, n, NULL);
	if (IS_ERR(page))
		return page;
	if (!PageUptodate(page)) {
		put_page(page);
		return ERR_PTR(-EIO);
	}
	lock_page(page);
	return page;
}

/*
 * Compare extents of two files to see if they are the same.
 */
static int
xfs_compare_extents(
	struct inode	*src,
	xfs_off_t	srcoff,
	struct inode	*dest,
	xfs_off_t	destoff,
	xfs_off_t	len,
	bool		*is_same)
{
	xfs_off_t	src_poff;
	xfs_off_t	dest_poff;
	void		*src_addr;
	void		*dest_addr;
	struct page	*src_page;
	struct page	*dest_page;
	xfs_off_t	cmp_len;
	bool		same;
	int		error;

	error = -EINVAL;
	same = true;
	while (len) {
		src_poff = srcoff & (PAGE_SIZE - 1);
		dest_poff = destoff & (PAGE_SIZE - 1);
		cmp_len = min(PAGE_SIZE - src_poff,
			      PAGE_SIZE - dest_poff);
		cmp_len = min(cmp_len, len);
		ASSERT(cmp_len > 0);

		trace_xfs_reflink_compare_extents(XFS_I(src), srcoff, cmp_len,
				XFS_I(dest), destoff);

		src_page = xfs_get_page(src, srcoff);
		if (IS_ERR(src_page)) {
			error = PTR_ERR(src_page);
			goto out_error;
		}
		dest_page = xfs_get_page(dest, destoff);
		if (IS_ERR(dest_page)) {
			error = PTR_ERR(dest_page);
			unlock_page(src_page);
			put_page(src_page);
			goto out_error;
		}
		src_addr = kmap_atomic(src_page);
		dest_addr = kmap_atomic(dest_page);

		flush_dcache_page(src_page);
		flush_dcache_page(dest_page);

		if (memcmp(src_addr + src_poff, dest_addr + dest_poff, cmp_len))
			same = false;

		kunmap_atomic(dest_addr);
		kunmap_atomic(src_addr);
		unlock_page(dest_page);
		unlock_page(src_page);
		put_page(dest_page);
		put_page(src_page);

		if (!same)
			break;

		srcoff += cmp_len;
		destoff += cmp_len;
		len -= cmp_len;
	}

	*is_same = same;
	return 0;

out_error:
	trace_xfs_reflink_compare_extents_error(XFS_I(dest), error, _RET_IP_);
	return error;
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
	loff_t			bs = inode_out->i_sb->s_blocksize;
	bool			same_inode = (inode_in == inode_out);
	xfs_fileoff_t		sfsbno, dfsbno;
	xfs_filblks_t		fsblen;
	xfs_extlen_t		cowextsize;
	loff_t			isize;
	ssize_t			ret;
	loff_t			blen;

	if (!xfs_sb_version_hasreflink(&mp->m_sb))
		return -EOPNOTSUPP;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	/* Lock both files against IO */
	if (same_inode) {
		xfs_ilock(src, XFS_IOLOCK_EXCL);
		xfs_ilock(src, XFS_MMAPLOCK_EXCL);
	} else {
		xfs_lock_two_inodes(src, dest, XFS_IOLOCK_EXCL);
		xfs_lock_two_inodes(src, dest, XFS_MMAPLOCK_EXCL);
	}

	/* Don't touch certain kinds of inodes */
	ret = -EPERM;
	if (IS_IMMUTABLE(inode_out))
		goto out_unlock;

	ret = -ETXTBSY;
	if (IS_SWAPFILE(inode_in) || IS_SWAPFILE(inode_out))
		goto out_unlock;


	/* Don't reflink dirs, pipes, sockets... */
	ret = -EISDIR;
	if (S_ISDIR(inode_in->i_mode) || S_ISDIR(inode_out->i_mode))
		goto out_unlock;
	ret = -EINVAL;
	if (S_ISFIFO(inode_in->i_mode) || S_ISFIFO(inode_out->i_mode))
		goto out_unlock;
	if (!S_ISREG(inode_in->i_mode) || !S_ISREG(inode_out->i_mode))
		goto out_unlock;

	/* Don't reflink realtime inodes */
	if (XFS_IS_REALTIME_INODE(src) || XFS_IS_REALTIME_INODE(dest))
		goto out_unlock;

	/* Don't share DAX file data for now. */
	if (IS_DAX(inode_in) || IS_DAX(inode_out))
		goto out_unlock;

	/* Are we going all the way to the end? */
	isize = i_size_read(inode_in);
	if (isize == 0) {
		ret = 0;
		goto out_unlock;
	}

	if (len == 0)
		len = isize - pos_in;

	/* Ensure offsets don't wrap and the input is inside i_size */
	if (pos_in + len < pos_in || pos_out + len < pos_out ||
	    pos_in + len > isize)
		goto out_unlock;

	/* Don't allow dedupe past EOF in the dest file */
	if (is_dedupe) {
		loff_t	disize;

		disize = i_size_read(inode_out);
		if (pos_out >= disize || pos_out + len > disize)
			goto out_unlock;
	}

	/* If we're linking to EOF, continue to the block boundary. */
	if (pos_in + len == isize)
		blen = ALIGN(isize, bs) - pos_in;
	else
		blen = len;

	/* Only reflink if we're aligned to block boundaries */
	if (!IS_ALIGNED(pos_in, bs) || !IS_ALIGNED(pos_in + blen, bs) ||
	    !IS_ALIGNED(pos_out, bs) || !IS_ALIGNED(pos_out + blen, bs))
		goto out_unlock;

	/* Don't allow overlapped reflink within the same file */
	if (same_inode) {
		if (pos_out + blen > pos_in && pos_out < pos_in + blen)
			goto out_unlock;
	}

	/* Wait for the completion of any pending IOs on both files */
	inode_dio_wait(inode_in);
	if (!same_inode)
		inode_dio_wait(inode_out);

	ret = filemap_write_and_wait_range(inode_in->i_mapping,
			pos_in, pos_in + len - 1);
	if (ret)
		goto out_unlock;

	ret = filemap_write_and_wait_range(inode_out->i_mapping,
			pos_out, pos_out + len - 1);
	if (ret)
		goto out_unlock;

	trace_xfs_reflink_remap_range(src, pos_in, len, dest, pos_out);

	/*
	 * Check that the extents are the same.
	 */
	if (is_dedupe) {
		bool		is_same = false;

		ret = xfs_compare_extents(inode_in, pos_in, inode_out, pos_out,
				len, &is_same);
		if (ret)
			goto out_unlock;
		if (!is_same) {
			ret = -EBADE;
			goto out_unlock;
		}
	}

	ret = xfs_reflink_set_inode_flag(src, dest);
	if (ret)
		goto out_unlock;

	/*
	 * Invalidate the page cache so that we can clear any CoW mappings
	 * in the destination file.
	 */
	truncate_inode_pages_range(&inode_out->i_data, pos_out,
				   PAGE_ALIGN(pos_out + len) - 1);

	dfsbno = XFS_B_TO_FSBT(mp, pos_out);
	sfsbno = XFS_B_TO_FSBT(mp, pos_in);
	fsblen = XFS_B_TO_FSB(mp, len);
	ret = xfs_reflink_remap_blocks(src, sfsbno, dest, dfsbno, fsblen,
			pos_out + len);
	if (ret)
		goto out_unlock;

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

	ret = xfs_reflink_update_dest(dest, pos_out + len, cowextsize);

out_unlock:
	xfs_iunlock(src, XFS_MMAPLOCK_EXCL);
	xfs_iunlock(src, XFS_IOLOCK_EXCL);
	if (src->i_ino != dest->i_ino) {
		xfs_iunlock(dest, XFS_MMAPLOCK_EXCL);
		xfs_iunlock(dest, XFS_IOLOCK_EXCL);
	}
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
		if (map[0].br_startblock == HOLESTARTBLOCK ||
		    map[0].br_startblock == DELAYSTARTBLOCK ||
		    ISUNWRITTEN(&map[0]))
			goto next;

		map[1] = map[0];
		while (map[1].br_blockcount) {
			agno = XFS_FSB_TO_AGNO(mp, map[1].br_startblock);
			agbno = XFS_FSB_TO_AGBNO(mp, map[1].br_startblock);
			aglen = map[1].br_blockcount;

			error = xfs_reflink_find_shared(mp, agno, agbno, aglen,
					&rbno, &rlen, true);
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

/* Clear the inode reflink flag if there are no shared extents. */
int
xfs_reflink_clear_inode_flag(
	struct xfs_inode	*ip,
	struct xfs_trans	**tpp)
{
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		fbno;
	xfs_filblks_t		end;
	xfs_agnumber_t		agno;
	xfs_agblock_t		agbno;
	xfs_extlen_t		aglen;
	xfs_agblock_t		rbno;
	xfs_extlen_t		rlen;
	struct xfs_bmbt_irec	map;
	int			nmaps;
	int			error = 0;

	ASSERT(xfs_is_reflink_inode(ip));

	fbno = 0;
	end = XFS_B_TO_FSB(mp, i_size_read(VFS_I(ip)));
	while (end - fbno > 0) {
		nmaps = 1;
		/*
		 * Look for extents in the file.  Skip holes, delalloc, or
		 * unwritten extents; they can't be reflinked.
		 */
		error = xfs_bmapi_read(ip, fbno, end - fbno, &map, &nmaps, 0);
		if (error)
			return error;
		if (nmaps == 0)
			break;
		if (map.br_startblock == HOLESTARTBLOCK ||
		    map.br_startblock == DELAYSTARTBLOCK ||
		    ISUNWRITTEN(&map))
			goto next;

		agno = XFS_FSB_TO_AGNO(mp, map.br_startblock);
		agbno = XFS_FSB_TO_AGBNO(mp, map.br_startblock);
		aglen = map.br_blockcount;

		error = xfs_reflink_find_shared(mp, agno, agbno, aglen,
				&rbno, &rlen, false);
		if (error)
			return error;
		/* Is there still a shared block here? */
		if (rbno != NULLAGBLOCK)
			return 0;
next:
		fbno = map.br_startoff + map.br_blockcount;
	}

	/*
	 * We didn't find any shared blocks so turn off the reflink flag.
	 * First, get rid of any leftover CoW mappings.
	 */
	error = xfs_reflink_cancel_cow_blocks(ip, tpp, 0, NULLFILEOFF);
	if (error)
		return error;

	/* Clear the inode flag. */
	trace_xfs_reflink_unset_inode_flag(ip);
	ip->i_d.di_flags2 &= ~XFS_DIFLAG2_REFLINK;
	xfs_inode_clear_cowblocks_tag(ip);
	xfs_trans_ijoin(*tpp, ip, 0);
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
