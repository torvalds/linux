// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * Copyright (c) 2016-2018 Christoph Hellwig.
 * All Rights Reserved.
 */
#include "xfs.h"
#include "xfs_shared.h"
#include "xfs_format.h"
#include "xfs_log_format.h"
#include "xfs_trans_resv.h"
#include "xfs_mount.h"
#include "xfs_inode.h"
#include "xfs_trans.h"
#include "xfs_iomap.h"
#include "xfs_trace.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_reflink.h"
#include "xfs_errortag.h"
#include "xfs_error.h"
#include "xfs_icache.h"

struct xfs_writepage_ctx {
	struct iomap_writepage_ctx ctx;
	unsigned int		data_seq;
	unsigned int		cow_seq;
};

static inline struct xfs_writepage_ctx *
XFS_WPC(struct iomap_writepage_ctx *ctx)
{
	return container_of(ctx, struct xfs_writepage_ctx, ctx);
}

/*
 * Fast and loose check if this write could update the on-disk inode size.
 */
static inline bool xfs_ioend_is_append(struct iomap_ioend *ioend)
{
	return ioend->io_offset + ioend->io_size >
		XFS_I(ioend->io_inode)->i_disk_size;
}

/*
 * Update on-disk file size now that data has been written to disk.
 */
int
xfs_setfilesize(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	size_t			size)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	xfs_fsize_t		isize;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_fsyncts, 0, 0, 0, &tp);
	if (error)
		return error;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	isize = xfs_new_eof(ip, offset + size);
	if (!isize) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_trans_cancel(tp);
		return 0;
	}

	trace_xfs_setfilesize(ip, offset, size);

	ip->i_disk_size = isize;
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	return xfs_trans_commit(tp);
}

/*
 * IO write completion.
 */
STATIC void
xfs_end_ioend(
	struct iomap_ioend	*ioend)
{
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_off_t		offset = ioend->io_offset;
	size_t			size = ioend->io_size;
	unsigned int		nofs_flag;
	int			error;

	/*
	 * We can allocate memory here while doing writeback on behalf of
	 * memory reclaim.  To avoid memory allocation deadlocks set the
	 * task-wide nofs context for the following operations.
	 */
	nofs_flag = memalloc_nofs_save();

	/*
	 * Just clean up the in-memory structures if the fs has been shut down.
	 */
	if (xfs_is_shutdown(mp)) {
		error = -EIO;
		goto done;
	}

	/*
	 * Clean up all COW blocks and underlying data fork delalloc blocks on
	 * I/O error. The delalloc punch is required because this ioend was
	 * mapped to blocks in the COW fork and the associated pages are no
	 * longer dirty. If we don't remove delalloc blocks here, they become
	 * stale and can corrupt free space accounting on unmount.
	 */
	error = blk_status_to_errno(ioend->io_bio.bi_status);
	if (unlikely(error)) {
		if (ioend->io_flags & IOMAP_IOEND_SHARED) {
			xfs_reflink_cancel_cow_range(ip, offset, size, true);
			xfs_bmap_punch_delalloc_range(ip, XFS_DATA_FORK, offset,
					offset + size);
		}
		goto done;
	}

	/*
	 * Success: commit the COW or unwritten blocks if needed.
	 */
	if (ioend->io_flags & IOMAP_IOEND_SHARED)
		error = xfs_reflink_end_cow(ip, offset, size);
	else if (ioend->io_flags & IOMAP_IOEND_UNWRITTEN)
		error = xfs_iomap_write_unwritten(ip, offset, size, false);

	if (!error && xfs_ioend_is_append(ioend))
		error = xfs_setfilesize(ip, offset, size);
done:
	iomap_finish_ioends(ioend, error);
	memalloc_nofs_restore(nofs_flag);
}

/*
 * Finish all pending IO completions that require transactional modifications.
 *
 * We try to merge physical and logically contiguous ioends before completion to
 * minimise the number of transactions we need to perform during IO completion.
 * Both unwritten extent conversion and COW remapping need to iterate and modify
 * one physical extent at a time, so we gain nothing by merging physically
 * discontiguous extents here.
 *
 * The ioend chain length that we can be processing here is largely unbound in
 * length and we may have to perform significant amounts of work on each ioend
 * to complete it. Hence we have to be careful about holding the CPU for too
 * long in this loop.
 */
void
xfs_end_io(
	struct work_struct	*work)
{
	struct xfs_inode	*ip =
		container_of(work, struct xfs_inode, i_ioend_work);
	struct iomap_ioend	*ioend;
	struct list_head	tmp;
	unsigned long		flags;

	spin_lock_irqsave(&ip->i_ioend_lock, flags);
	list_replace_init(&ip->i_ioend_list, &tmp);
	spin_unlock_irqrestore(&ip->i_ioend_lock, flags);

	iomap_sort_ioends(&tmp);
	while ((ioend = list_first_entry_or_null(&tmp, struct iomap_ioend,
			io_list))) {
		list_del_init(&ioend->io_list);
		iomap_ioend_try_merge(ioend, &tmp);
		xfs_end_ioend(ioend);
		cond_resched();
	}
}

STATIC void
xfs_end_bio(
	struct bio		*bio)
{
	struct iomap_ioend	*ioend = iomap_ioend_from_bio(bio);
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	unsigned long		flags;

	spin_lock_irqsave(&ip->i_ioend_lock, flags);
	if (list_empty(&ip->i_ioend_list))
		WARN_ON_ONCE(!queue_work(ip->i_mount->m_unwritten_workqueue,
					 &ip->i_ioend_work));
	list_add_tail(&ioend->io_list, &ip->i_ioend_list);
	spin_unlock_irqrestore(&ip->i_ioend_lock, flags);
}

/*
 * Fast revalidation of the cached writeback mapping. Return true if the current
 * mapping is valid, false otherwise.
 */
static bool
xfs_imap_valid(
	struct iomap_writepage_ctx	*wpc,
	struct xfs_inode		*ip,
	loff_t				offset)
{
	if (offset < wpc->iomap.offset ||
	    offset >= wpc->iomap.offset + wpc->iomap.length)
		return false;
	/*
	 * If this is a COW mapping, it is sufficient to check that the mapping
	 * covers the offset. Be careful to check this first because the caller
	 * can revalidate a COW mapping without updating the data seqno.
	 */
	if (wpc->iomap.flags & IOMAP_F_SHARED)
		return true;

	/*
	 * This is not a COW mapping. Check the sequence number of the data fork
	 * because concurrent changes could have invalidated the extent. Check
	 * the COW fork because concurrent changes since the last time we
	 * checked (and found nothing at this offset) could have added
	 * overlapping blocks.
	 */
	if (XFS_WPC(wpc)->data_seq != READ_ONCE(ip->i_df.if_seq)) {
		trace_xfs_wb_data_iomap_invalid(ip, &wpc->iomap,
				XFS_WPC(wpc)->data_seq, XFS_DATA_FORK);
		return false;
	}
	if (xfs_inode_has_cow_data(ip) &&
	    XFS_WPC(wpc)->cow_seq != READ_ONCE(ip->i_cowfp->if_seq)) {
		trace_xfs_wb_cow_iomap_invalid(ip, &wpc->iomap,
				XFS_WPC(wpc)->cow_seq, XFS_COW_FORK);
		return false;
	}
	return true;
}

static int
xfs_map_blocks(
	struct iomap_writepage_ctx *wpc,
	struct inode		*inode,
	loff_t			offset,
	unsigned int		len)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			count = i_blocksize(inode);
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset);
	xfs_fileoff_t		end_fsb = XFS_B_TO_FSB(mp, offset + count);
	xfs_fileoff_t		cow_fsb;
	int			whichfork;
	struct xfs_bmbt_irec	imap;
	struct xfs_iext_cursor	icur;
	int			retries = 0;
	int			error = 0;
	unsigned int		*seq;

	if (xfs_is_shutdown(mp))
		return -EIO;

	XFS_ERRORTAG_DELAY(mp, XFS_ERRTAG_WB_DELAY_MS);

	/*
	 * COW fork blocks can overlap data fork blocks even if the blocks
	 * aren't shared.  COW I/O always takes precedent, so we must always
	 * check for overlap on reflink inodes unless the mapping is already a
	 * COW one, or the COW fork hasn't changed from the last time we looked
	 * at it.
	 *
	 * It's safe to check the COW fork if_seq here without the ILOCK because
	 * we've indirectly protected against concurrent updates: writeback has
	 * the page locked, which prevents concurrent invalidations by reflink
	 * and directio and prevents concurrent buffered writes to the same
	 * page.  Changes to if_seq always happen under i_lock, which protects
	 * against concurrent updates and provides a memory barrier on the way
	 * out that ensures that we always see the current value.
	 */
	if (xfs_imap_valid(wpc, ip, offset))
		return 0;

	/*
	 * If we don't have a valid map, now it's time to get a new one for this
	 * offset.  This will convert delayed allocations (including COW ones)
	 * into real extents.  If we return without a valid map, it means we
	 * landed in a hole and we skip the block.
	 */
retry:
	cow_fsb = NULLFILEOFF;
	whichfork = XFS_DATA_FORK;
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	ASSERT(!xfs_need_iread_extents(&ip->i_df));

	/*
	 * Check if this is offset is covered by a COW extents, and if yes use
	 * it directly instead of looking up anything in the data fork.
	 */
	if (xfs_inode_has_cow_data(ip) &&
	    xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &imap))
		cow_fsb = imap.br_startoff;
	if (cow_fsb != NULLFILEOFF && cow_fsb <= offset_fsb) {
		XFS_WPC(wpc)->cow_seq = READ_ONCE(ip->i_cowfp->if_seq);
		xfs_iunlock(ip, XFS_ILOCK_SHARED);

		whichfork = XFS_COW_FORK;
		goto allocate_blocks;
	}

	/*
	 * No COW extent overlap. Revalidate now that we may have updated
	 * ->cow_seq. If the data mapping is still valid, we're done.
	 */
	if (xfs_imap_valid(wpc, ip, offset)) {
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		return 0;
	}

	/*
	 * If we don't have a valid map, now it's time to get a new one for this
	 * offset.  This will convert delayed allocations (including COW ones)
	 * into real extents.
	 */
	if (!xfs_iext_lookup_extent(ip, &ip->i_df, offset_fsb, &icur, &imap))
		imap.br_startoff = end_fsb;	/* fake a hole past EOF */
	XFS_WPC(wpc)->data_seq = READ_ONCE(ip->i_df.if_seq);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	/* landed in a hole or beyond EOF? */
	if (imap.br_startoff > offset_fsb) {
		imap.br_blockcount = imap.br_startoff - offset_fsb;
		imap.br_startoff = offset_fsb;
		imap.br_startblock = HOLESTARTBLOCK;
		imap.br_state = XFS_EXT_NORM;
	}

	/*
	 * Truncate to the next COW extent if there is one.  This is the only
	 * opportunity to do this because we can skip COW fork lookups for the
	 * subsequent blocks in the mapping; however, the requirement to treat
	 * the COW range separately remains.
	 */
	if (cow_fsb != NULLFILEOFF &&
	    cow_fsb < imap.br_startoff + imap.br_blockcount)
		imap.br_blockcount = cow_fsb - imap.br_startoff;

	/* got a delalloc extent? */
	if (imap.br_startblock != HOLESTARTBLOCK &&
	    isnullstartblock(imap.br_startblock))
		goto allocate_blocks;

	xfs_bmbt_to_iomap(ip, &wpc->iomap, &imap, 0, 0, XFS_WPC(wpc)->data_seq);
	trace_xfs_map_blocks_found(ip, offset, count, whichfork, &imap);
	return 0;
allocate_blocks:
	/*
	 * Convert a dellalloc extent to a real one. The current page is held
	 * locked so nothing could have removed the block backing offset_fsb,
	 * although it could have moved from the COW to the data fork by another
	 * thread.
	 */
	if (whichfork == XFS_COW_FORK)
		seq = &XFS_WPC(wpc)->cow_seq;
	else
		seq = &XFS_WPC(wpc)->data_seq;

	error = xfs_bmapi_convert_delalloc(ip, whichfork, offset,
				&wpc->iomap, seq);
	if (error) {
		/*
		 * If we failed to find the extent in the COW fork we might have
		 * raced with a COW to data fork conversion or truncate.
		 * Restart the lookup to catch the extent in the data fork for
		 * the former case, but prevent additional retries to avoid
		 * looping forever for the latter case.
		 */
		if (error == -EAGAIN && whichfork == XFS_COW_FORK && !retries++)
			goto retry;
		ASSERT(error != -EAGAIN);
		return error;
	}

	/*
	 * Due to merging the return real extent might be larger than the
	 * original delalloc one.  Trim the return extent to the next COW
	 * boundary again to force a re-lookup.
	 */
	if (whichfork != XFS_COW_FORK && cow_fsb != NULLFILEOFF) {
		loff_t		cow_offset = XFS_FSB_TO_B(mp, cow_fsb);

		if (cow_offset < wpc->iomap.offset + wpc->iomap.length)
			wpc->iomap.length = cow_offset - wpc->iomap.offset;
	}

	ASSERT(wpc->iomap.offset <= offset);
	ASSERT(wpc->iomap.offset + wpc->iomap.length > offset);
	trace_xfs_map_blocks_alloc(ip, offset, count, whichfork, &imap);
	return 0;
}

static int
xfs_submit_ioend(
	struct iomap_writepage_ctx *wpc,
	int			status)
{
	struct iomap_ioend	*ioend = wpc->ioend;
	unsigned int		nofs_flag;

	/*
	 * We can allocate memory here while doing writeback on behalf of
	 * memory reclaim.  To avoid memory allocation deadlocks set the
	 * task-wide nofs context for the following operations.
	 */
	nofs_flag = memalloc_nofs_save();

	/* Convert CoW extents to regular */
	if (!status && (ioend->io_flags & IOMAP_IOEND_SHARED)) {
		status = xfs_reflink_convert_cow(XFS_I(ioend->io_inode),
				ioend->io_offset, ioend->io_size);
	}

	memalloc_nofs_restore(nofs_flag);

	/* send ioends that might require a transaction to the completion wq */
	if (xfs_ioend_is_append(ioend) ||
	    (ioend->io_flags & (IOMAP_IOEND_UNWRITTEN | IOMAP_IOEND_SHARED)))
		ioend->io_bio.bi_end_io = xfs_end_bio;

	if (status)
		return status;
	submit_bio(&ioend->io_bio);
	return 0;
}

/*
 * If the folio has delalloc blocks on it, the caller is asking us to punch them
 * out. If we don't, we can leave a stale delalloc mapping covered by a clean
 * page that needs to be dirtied again before the delalloc mapping can be
 * converted. This stale delalloc mapping can trip up a later direct I/O read
 * operation on the same region.
 *
 * We prevent this by truncating away the delalloc regions on the folio. Because
 * they are delalloc, we can do this without needing a transaction. Indeed - if
 * we get ENOSPC errors, we have to be able to do this truncation without a
 * transaction as there is no space left for block reservation (typically why
 * we see a ENOSPC in writeback).
 */
static void
xfs_discard_folio(
	struct folio		*folio,
	loff_t			pos)
{
	struct xfs_inode	*ip = XFS_I(folio->mapping->host);
	struct xfs_mount	*mp = ip->i_mount;

	if (xfs_is_shutdown(mp))
		return;

	xfs_alert_ratelimited(mp,
		"page discard on page "PTR_FMT", inode 0x%llx, pos %llu.",
			folio, ip->i_ino, pos);

	/*
	 * The end of the punch range is always the offset of the first
	 * byte of the next folio. Hence the end offset is only dependent on the
	 * folio itself and not the start offset that is passed in.
	 */
	xfs_bmap_punch_delalloc_range(ip, XFS_DATA_FORK, pos,
				folio_pos(folio) + folio_size(folio));
}

static const struct iomap_writeback_ops xfs_writeback_ops = {
	.map_blocks		= xfs_map_blocks,
	.submit_ioend		= xfs_submit_ioend,
	.discard_folio		= xfs_discard_folio,
};

STATIC int
xfs_vm_writepages(
	struct address_space	*mapping,
	struct writeback_control *wbc)
{
	struct xfs_writepage_ctx wpc = { };

	xfs_iflags_clear(XFS_I(mapping->host), XFS_ITRUNCATED);
	return iomap_writepages(mapping, wbc, &wpc.ctx, &xfs_writeback_ops);
}

STATIC int
xfs_dax_writepages(
	struct address_space	*mapping,
	struct writeback_control *wbc)
{
	struct xfs_inode	*ip = XFS_I(mapping->host);

	xfs_iflags_clear(ip, XFS_ITRUNCATED);
	return dax_writeback_mapping_range(mapping,
			xfs_inode_buftarg(ip)->bt_daxdev, wbc);
}

STATIC sector_t
xfs_vm_bmap(
	struct address_space	*mapping,
	sector_t		block)
{
	struct xfs_inode	*ip = XFS_I(mapping->host);

	trace_xfs_vm_bmap(ip);

	/*
	 * The swap code (ab-)uses ->bmap to get a block mapping and then
	 * bypasses the file system for actual I/O.  We really can't allow
	 * that on reflinks inodes, so we have to skip out here.  And yes,
	 * 0 is the magic code for a bmap error.
	 *
	 * Since we don't pass back blockdev info, we can't return bmap
	 * information for rt files either.
	 */
	if (xfs_is_cow_inode(ip) || XFS_IS_REALTIME_INODE(ip))
		return 0;
	return iomap_bmap(mapping, block, &xfs_read_iomap_ops);
}

STATIC int
xfs_vm_read_folio(
	struct file		*unused,
	struct folio		*folio)
{
	return iomap_read_folio(folio, &xfs_read_iomap_ops);
}

STATIC void
xfs_vm_readahead(
	struct readahead_control	*rac)
{
	iomap_readahead(rac, &xfs_read_iomap_ops);
}

static int
xfs_vm_swap_activate(
	struct swap_info_struct		*sis,
	struct file			*swap_file,
	sector_t			*span)
{
	struct xfs_inode		*ip = XFS_I(file_inode(swap_file));

	/*
	 * Swap file activation can race against concurrent shared extent
	 * removal in files that have been cloned.  If this happens,
	 * iomap_swapfile_iter() can fail because it encountered a shared
	 * extent even though an operation is in progress to remove those
	 * shared extents.
	 *
	 * This race becomes problematic when we defer extent removal
	 * operations beyond the end of a syscall (i.e. use async background
	 * processing algorithms).  Users think the extents are no longer
	 * shared, but iomap_swapfile_iter() still sees them as shared
	 * because the refcountbt entries for the extents being removed have
	 * not yet been updated.  Hence the swapon call fails unexpectedly.
	 *
	 * The race condition is currently most obvious from the unlink()
	 * operation as extent removal is deferred until after the last
	 * reference to the inode goes away.  We then process the extent
	 * removal asynchronously, hence triggers the "syscall completed but
	 * work not done" condition mentioned above.  To close this race
	 * window, we need to flush any pending inodegc operations to ensure
	 * they have updated the refcountbt records before we try to map the
	 * swapfile.
	 */
	xfs_inodegc_flush(ip->i_mount);

	/*
	 * Direct the swap code to the correct block device when this file
	 * sits on the RT device.
	 */
	sis->bdev = xfs_inode_buftarg(ip)->bt_bdev;

	return iomap_swapfile_activate(sis, swap_file, span,
			&xfs_read_iomap_ops);
}

const struct address_space_operations xfs_address_space_operations = {
	.read_folio		= xfs_vm_read_folio,
	.readahead		= xfs_vm_readahead,
	.writepages		= xfs_vm_writepages,
	.dirty_folio		= iomap_dirty_folio,
	.release_folio		= iomap_release_folio,
	.invalidate_folio	= iomap_invalidate_folio,
	.bmap			= xfs_vm_bmap,
	.migrate_folio		= filemap_migrate_folio,
	.is_partially_uptodate  = iomap_is_partially_uptodate,
	.error_remove_folio	= generic_error_remove_folio,
	.swap_activate		= xfs_vm_swap_activate,
};

const struct address_space_operations xfs_dax_aops = {
	.writepages		= xfs_dax_writepages,
	.dirty_folio		= noop_dirty_folio,
	.swap_activate		= xfs_vm_swap_activate,
};
