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
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_iomap.h"
#include "xfs_trace.h"
#include "xfs_bmap.h"
#include "xfs_bmap_util.h"
#include "xfs_bmap_btree.h"
#include "xfs_reflink.h"
#include <linux/writeback.h>

/*
 * structure owned by writepages passed to individual writepage calls
 */
struct xfs_writepage_ctx {
	struct xfs_bmbt_irec    imap;
	unsigned int		io_type;
	unsigned int		cow_seq;
	struct xfs_ioend	*ioend;
};

struct block_device *
xfs_find_bdev_for_inode(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;

	if (XFS_IS_REALTIME_INODE(ip))
		return mp->m_rtdev_targp->bt_bdev;
	else
		return mp->m_ddev_targp->bt_bdev;
}

struct dax_device *
xfs_find_daxdev_for_inode(
	struct inode		*inode)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;

	if (XFS_IS_REALTIME_INODE(ip))
		return mp->m_rtdev_targp->bt_daxdev;
	else
		return mp->m_ddev_targp->bt_daxdev;
}

static void
xfs_finish_page_writeback(
	struct inode		*inode,
	struct bio_vec		*bvec,
	int			error)
{
	struct iomap_page	*iop = to_iomap_page(bvec->bv_page);

	if (error) {
		SetPageError(bvec->bv_page);
		mapping_set_error(inode->i_mapping, -EIO);
	}

	ASSERT(iop || i_blocksize(inode) == PAGE_SIZE);
	ASSERT(!iop || atomic_read(&iop->write_count) > 0);

	if (!iop || atomic_dec_and_test(&iop->write_count))
		end_page_writeback(bvec->bv_page);
}

/*
 * We're now finished for good with this ioend structure.  Update the page
 * state, release holds on bios, and finally free up memory.  Do not use the
 * ioend after this.
 */
STATIC void
xfs_destroy_ioend(
	struct xfs_ioend	*ioend,
	int			error)
{
	struct inode		*inode = ioend->io_inode;
	struct bio		*bio = &ioend->io_inline_bio;
	struct bio		*last = ioend->io_bio, *next;
	u64			start = bio->bi_iter.bi_sector;
	bool			quiet = bio_flagged(bio, BIO_QUIET);

	for (bio = &ioend->io_inline_bio; bio; bio = next) {
		struct bio_vec	*bvec;
		int		i;

		/*
		 * For the last bio, bi_private points to the ioend, so we
		 * need to explicitly end the iteration here.
		 */
		if (bio == last)
			next = NULL;
		else
			next = bio->bi_private;

		/* walk each page on bio, ending page IO on them */
		bio_for_each_segment_all(bvec, bio, i)
			xfs_finish_page_writeback(inode, bvec, error);
		bio_put(bio);
	}

	if (unlikely(error && !quiet)) {
		xfs_err_ratelimited(XFS_I(inode)->i_mount,
			"writeback error on sector %llu", start);
	}
}

/*
 * Fast and loose check if this write could update the on-disk inode size.
 */
static inline bool xfs_ioend_is_append(struct xfs_ioend *ioend)
{
	return ioend->io_offset + ioend->io_size >
		XFS_I(ioend->io_inode)->i_d.di_size;
}

STATIC int
xfs_setfilesize_trans_alloc(
	struct xfs_ioend	*ioend)
{
	struct xfs_mount	*mp = XFS_I(ioend->io_inode)->i_mount;
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_fsyncts, 0, 0,
				XFS_TRANS_NOFS, &tp);
	if (error)
		return error;

	ioend->io_append_trans = tp;

	/*
	 * We may pass freeze protection with a transaction.  So tell lockdep
	 * we released it.
	 */
	__sb_writers_release(ioend->io_inode->i_sb, SB_FREEZE_FS);
	/*
	 * We hand off the transaction to the completion thread now, so
	 * clear the flag here.
	 */
	current_restore_flags_nested(&tp->t_pflags, PF_MEMALLOC_NOFS);
	return 0;
}

/*
 * Update on-disk file size now that data has been written to disk.
 */
STATIC int
__xfs_setfilesize(
	struct xfs_inode	*ip,
	struct xfs_trans	*tp,
	xfs_off_t		offset,
	size_t			size)
{
	xfs_fsize_t		isize;

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	isize = xfs_new_eof(ip, offset + size);
	if (!isize) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_trans_cancel(tp);
		return 0;
	}

	trace_xfs_setfilesize(ip, offset, size);

	ip->i_d.di_size = isize;
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	return xfs_trans_commit(tp);
}

int
xfs_setfilesize(
	struct xfs_inode	*ip,
	xfs_off_t		offset,
	size_t			size)
{
	struct xfs_mount	*mp = ip->i_mount;
	struct xfs_trans	*tp;
	int			error;

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_fsyncts, 0, 0, 0, &tp);
	if (error)
		return error;

	return __xfs_setfilesize(ip, tp, offset, size);
}

STATIC int
xfs_setfilesize_ioend(
	struct xfs_ioend	*ioend,
	int			error)
{
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_trans	*tp = ioend->io_append_trans;

	/*
	 * The transaction may have been allocated in the I/O submission thread,
	 * thus we need to mark ourselves as being in a transaction manually.
	 * Similarly for freeze protection.
	 */
	current_set_flags_nested(&tp->t_pflags, PF_MEMALLOC_NOFS);
	__sb_writers_acquired(VFS_I(ip)->i_sb, SB_FREEZE_FS);

	/* we abort the update if there was an IO error */
	if (error) {
		xfs_trans_cancel(tp);
		return error;
	}

	return __xfs_setfilesize(ip, tp, ioend->io_offset, ioend->io_size);
}

/*
 * IO write completion.
 */
STATIC void
xfs_end_io(
	struct work_struct *work)
{
	struct xfs_ioend	*ioend =
		container_of(work, struct xfs_ioend, io_work);
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	xfs_off_t		offset = ioend->io_offset;
	size_t			size = ioend->io_size;
	int			error;

	/*
	 * Just clean up the in-memory strutures if the fs has been shut down.
	 */
	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		error = -EIO;
		goto done;
	}

	/*
	 * Clean up any COW blocks on an I/O error.
	 */
	error = blk_status_to_errno(ioend->io_bio->bi_status);
	if (unlikely(error)) {
		switch (ioend->io_type) {
		case XFS_IO_COW:
			xfs_reflink_cancel_cow_range(ip, offset, size, true);
			break;
		}

		goto done;
	}

	/*
	 * Success:  commit the COW or unwritten blocks if needed.
	 */
	switch (ioend->io_type) {
	case XFS_IO_COW:
		error = xfs_reflink_end_cow(ip, offset, size);
		break;
	case XFS_IO_UNWRITTEN:
		/* writeback should never update isize */
		error = xfs_iomap_write_unwritten(ip, offset, size, false);
		break;
	default:
		ASSERT(!xfs_ioend_is_append(ioend) || ioend->io_append_trans);
		break;
	}

done:
	if (ioend->io_append_trans)
		error = xfs_setfilesize_ioend(ioend, error);
	xfs_destroy_ioend(ioend, error);
}

STATIC void
xfs_end_bio(
	struct bio		*bio)
{
	struct xfs_ioend	*ioend = bio->bi_private;
	struct xfs_mount	*mp = XFS_I(ioend->io_inode)->i_mount;

	if (ioend->io_type == XFS_IO_UNWRITTEN || ioend->io_type == XFS_IO_COW)
		queue_work(mp->m_unwritten_workqueue, &ioend->io_work);
	else if (ioend->io_append_trans)
		queue_work(mp->m_data_workqueue, &ioend->io_work);
	else
		xfs_destroy_ioend(ioend, blk_status_to_errno(bio->bi_status));
}

STATIC int
xfs_map_blocks(
	struct xfs_writepage_ctx *wpc,
	struct inode		*inode,
	loff_t			offset)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			count = i_blocksize(inode);
	xfs_fileoff_t		offset_fsb = XFS_B_TO_FSBT(mp, offset), end_fsb;
	xfs_fileoff_t		cow_fsb = NULLFILEOFF;
	struct xfs_bmbt_irec	imap;
	int			whichfork = XFS_DATA_FORK;
	struct xfs_iext_cursor	icur;
	bool			imap_valid;
	int			error = 0;

	/*
	 * We have to make sure the cached mapping is within EOF to protect
	 * against eofblocks trimming on file release leaving us with a stale
	 * mapping. Otherwise, a page for a subsequent file extending buffered
	 * write could get picked up by this writeback cycle and written to the
	 * wrong blocks.
	 *
	 * Note that what we really want here is a generic mapping invalidation
	 * mechanism to protect us from arbitrary extent modifying contexts, not
	 * just eofblocks.
	 */
	xfs_trim_extent_eof(&wpc->imap, ip);

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
	imap_valid = offset_fsb >= wpc->imap.br_startoff &&
		     offset_fsb < wpc->imap.br_startoff + wpc->imap.br_blockcount;
	if (imap_valid &&
	    (!xfs_inode_has_cow_data(ip) ||
	     wpc->io_type == XFS_IO_COW ||
	     wpc->cow_seq == READ_ONCE(ip->i_cowfp->if_seq)))
		return 0;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	/*
	 * If we don't have a valid map, now it's time to get a new one for this
	 * offset.  This will convert delayed allocations (including COW ones)
	 * into real extents.  If we return without a valid map, it means we
	 * landed in a hole and we skip the block.
	 */
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       (ip->i_df.if_flags & XFS_IFEXTENTS));
	ASSERT(offset <= mp->m_super->s_maxbytes);

	if (offset > mp->m_super->s_maxbytes - count)
		count = mp->m_super->s_maxbytes - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);

	/*
	 * Check if this is offset is covered by a COW extents, and if yes use
	 * it directly instead of looking up anything in the data fork.
	 */
	if (xfs_inode_has_cow_data(ip) &&
	    xfs_iext_lookup_extent(ip, ip->i_cowfp, offset_fsb, &icur, &imap))
		cow_fsb = imap.br_startoff;
	if (cow_fsb != NULLFILEOFF && cow_fsb <= offset_fsb) {
		wpc->cow_seq = READ_ONCE(ip->i_cowfp->if_seq);
		xfs_iunlock(ip, XFS_ILOCK_SHARED);
		/*
		 * Truncate can race with writeback since writeback doesn't
		 * take the iolock and truncate decreases the file size before
		 * it starts truncating the pages between new_size and old_size.
		 * Therefore, we can end up in the situation where writeback
		 * gets a CoW fork mapping but the truncate makes the mapping
		 * invalid and we end up in here trying to get a new mapping.
		 * bail out here so that we simply never get a valid mapping
		 * and so we drop the write altogether.  The page truncation
		 * will kill the contents anyway.
		 */
		if (offset > i_size_read(inode)) {
			wpc->io_type = XFS_IO_HOLE;
			return 0;
		}
		whichfork = XFS_COW_FORK;
		wpc->io_type = XFS_IO_COW;
		goto allocate_blocks;
	}

	/*
	 * Map valid and no COW extent in the way?  We're done.
	 */
	if (imap_valid) {
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
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (imap.br_startoff > offset_fsb) {
		/* landed in a hole or beyond EOF */
		imap.br_blockcount = imap.br_startoff - offset_fsb;
		imap.br_startoff = offset_fsb;
		imap.br_startblock = HOLESTARTBLOCK;
		wpc->io_type = XFS_IO_HOLE;
	} else {
		/*
		 * Truncate to the next COW extent if there is one.  This is the
		 * only opportunity to do this because we can skip COW fork
		 * lookups for the subsequent blocks in the mapping; however,
		 * the requirement to treat the COW range separately remains.
		 */
		if (cow_fsb != NULLFILEOFF &&
		    cow_fsb < imap.br_startoff + imap.br_blockcount)
			imap.br_blockcount = cow_fsb - imap.br_startoff;

		if (isnullstartblock(imap.br_startblock)) {
			/* got a delalloc extent */
			wpc->io_type = XFS_IO_DELALLOC;
			goto allocate_blocks;
		}

		if (imap.br_state == XFS_EXT_UNWRITTEN)
			wpc->io_type = XFS_IO_UNWRITTEN;
		else
			wpc->io_type = XFS_IO_OVERWRITE;
	}

	wpc->imap = imap;
	trace_xfs_map_blocks_found(ip, offset, count, wpc->io_type, &imap);
	return 0;
allocate_blocks:
	error = xfs_iomap_write_allocate(ip, whichfork, offset, &imap,
			&wpc->cow_seq);
	if (error)
		return error;
	ASSERT(whichfork == XFS_COW_FORK || cow_fsb == NULLFILEOFF ||
	       imap.br_startoff + imap.br_blockcount <= cow_fsb);
	wpc->imap = imap;
	trace_xfs_map_blocks_alloc(ip, offset, count, wpc->io_type, &imap);
	return 0;
}

/*
 * Submit the bio for an ioend. We are passed an ioend with a bio attached to
 * it, and we submit that bio. The ioend may be used for multiple bio
 * submissions, so we only want to allocate an append transaction for the ioend
 * once. In the case of multiple bio submission, each bio will take an IO
 * reference to the ioend to ensure that the ioend completion is only done once
 * all bios have been submitted and the ioend is really done.
 *
 * If @fail is non-zero, it means that we have a situation where some part of
 * the submission process has failed after we have marked paged for writeback
 * and unlocked them. In this situation, we need to fail the bio and ioend
 * rather than submit it to IO. This typically only happens on a filesystem
 * shutdown.
 */
STATIC int
xfs_submit_ioend(
	struct writeback_control *wbc,
	struct xfs_ioend	*ioend,
	int			status)
{
	/* Convert CoW extents to regular */
	if (!status && ioend->io_type == XFS_IO_COW) {
		/*
		 * Yuk. This can do memory allocation, but is not a
		 * transactional operation so everything is done in GFP_KERNEL
		 * context. That can deadlock, because we hold pages in
		 * writeback state and GFP_KERNEL allocations can block on them.
		 * Hence we must operate in nofs conditions here.
		 */
		unsigned nofs_flag;

		nofs_flag = memalloc_nofs_save();
		status = xfs_reflink_convert_cow(XFS_I(ioend->io_inode),
				ioend->io_offset, ioend->io_size);
		memalloc_nofs_restore(nofs_flag);
	}

	/* Reserve log space if we might write beyond the on-disk inode size. */
	if (!status &&
	    ioend->io_type != XFS_IO_UNWRITTEN &&
	    xfs_ioend_is_append(ioend) &&
	    !ioend->io_append_trans)
		status = xfs_setfilesize_trans_alloc(ioend);

	ioend->io_bio->bi_private = ioend;
	ioend->io_bio->bi_end_io = xfs_end_bio;
	ioend->io_bio->bi_opf = REQ_OP_WRITE | wbc_to_write_flags(wbc);

	/*
	 * If we are failing the IO now, just mark the ioend with an
	 * error and finish it. This will run IO completion immediately
	 * as there is only one reference to the ioend at this point in
	 * time.
	 */
	if (status) {
		ioend->io_bio->bi_status = errno_to_blk_status(status);
		bio_endio(ioend->io_bio);
		return status;
	}

	ioend->io_bio->bi_write_hint = ioend->io_inode->i_write_hint;
	submit_bio(ioend->io_bio);
	return 0;
}

static struct xfs_ioend *
xfs_alloc_ioend(
	struct inode		*inode,
	unsigned int		type,
	xfs_off_t		offset,
	struct block_device	*bdev,
	sector_t		sector)
{
	struct xfs_ioend	*ioend;
	struct bio		*bio;

	bio = bio_alloc_bioset(GFP_NOFS, BIO_MAX_PAGES, &xfs_ioend_bioset);
	bio_set_dev(bio, bdev);
	bio->bi_iter.bi_sector = sector;

	ioend = container_of(bio, struct xfs_ioend, io_inline_bio);
	INIT_LIST_HEAD(&ioend->io_list);
	ioend->io_type = type;
	ioend->io_inode = inode;
	ioend->io_size = 0;
	ioend->io_offset = offset;
	INIT_WORK(&ioend->io_work, xfs_end_io);
	ioend->io_append_trans = NULL;
	ioend->io_bio = bio;
	return ioend;
}

/*
 * Allocate a new bio, and chain the old bio to the new one.
 *
 * Note that we have to do perform the chaining in this unintuitive order
 * so that the bi_private linkage is set up in the right direction for the
 * traversal in xfs_destroy_ioend().
 */
static void
xfs_chain_bio(
	struct xfs_ioend	*ioend,
	struct writeback_control *wbc,
	struct block_device	*bdev,
	sector_t		sector)
{
	struct bio *new;

	new = bio_alloc(GFP_NOFS, BIO_MAX_PAGES);
	bio_set_dev(new, bdev);
	new->bi_iter.bi_sector = sector;
	bio_chain(ioend->io_bio, new);
	bio_get(ioend->io_bio);		/* for xfs_destroy_ioend */
	ioend->io_bio->bi_opf = REQ_OP_WRITE | wbc_to_write_flags(wbc);
	ioend->io_bio->bi_write_hint = ioend->io_inode->i_write_hint;
	submit_bio(ioend->io_bio);
	ioend->io_bio = new;
}

/*
 * Test to see if we have an existing ioend structure that we could append to
 * first, otherwise finish off the current ioend and start another.
 */
STATIC void
xfs_add_to_ioend(
	struct inode		*inode,
	xfs_off_t		offset,
	struct page		*page,
	struct iomap_page	*iop,
	struct xfs_writepage_ctx *wpc,
	struct writeback_control *wbc,
	struct list_head	*iolist)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	struct block_device	*bdev = xfs_find_bdev_for_inode(inode);
	unsigned		len = i_blocksize(inode);
	unsigned		poff = offset & (PAGE_SIZE - 1);
	sector_t		sector;

	sector = xfs_fsb_to_db(ip, wpc->imap.br_startblock) +
		((offset - XFS_FSB_TO_B(mp, wpc->imap.br_startoff)) >> 9);

	if (!wpc->ioend || wpc->io_type != wpc->ioend->io_type ||
	    sector != bio_end_sector(wpc->ioend->io_bio) ||
	    offset != wpc->ioend->io_offset + wpc->ioend->io_size) {
		if (wpc->ioend)
			list_add(&wpc->ioend->io_list, iolist);
		wpc->ioend = xfs_alloc_ioend(inode, wpc->io_type, offset,
				bdev, sector);
	}

	if (!__bio_try_merge_page(wpc->ioend->io_bio, page, len, poff)) {
		if (iop)
			atomic_inc(&iop->write_count);
		if (bio_full(wpc->ioend->io_bio))
			xfs_chain_bio(wpc->ioend, wbc, bdev, sector);
		__bio_add_page(wpc->ioend->io_bio, page, len, poff);
	}

	wpc->ioend->io_size += len;
}

STATIC void
xfs_vm_invalidatepage(
	struct page		*page,
	unsigned int		offset,
	unsigned int		length)
{
	trace_xfs_invalidatepage(page->mapping->host, page, offset, length);
	iomap_invalidatepage(page, offset, length);
}

/*
 * If the page has delalloc blocks on it, we need to punch them out before we
 * invalidate the page.  If we don't, we leave a stale delalloc mapping on the
 * inode that can trip up a later direct I/O read operation on the same region.
 *
 * We prevent this by truncating away the delalloc regions on the page.  Because
 * they are delalloc, we can do this without needing a transaction. Indeed - if
 * we get ENOSPC errors, we have to be able to do this truncation without a
 * transaction as there is no space left for block reservation (typically why we
 * see a ENOSPC in writeback).
 */
STATIC void
xfs_aops_discard_page(
	struct page		*page)
{
	struct inode		*inode = page->mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	loff_t			offset = page_offset(page);
	xfs_fileoff_t		start_fsb = XFS_B_TO_FSBT(mp, offset);
	int			error;

	if (XFS_FORCED_SHUTDOWN(mp))
		goto out_invalidate;

	xfs_alert(mp,
		"page discard on page "PTR_FMT", inode 0x%llx, offset %llu.",
			page, ip->i_ino, offset);

	error = xfs_bmap_punch_delalloc_range(ip, start_fsb,
			PAGE_SIZE / i_blocksize(inode));
	if (error && !XFS_FORCED_SHUTDOWN(mp))
		xfs_alert(mp, "page discard unable to remove delalloc mapping.");
out_invalidate:
	xfs_vm_invalidatepage(page, 0, PAGE_SIZE);
}

/*
 * We implement an immediate ioend submission policy here to avoid needing to
 * chain multiple ioends and hence nest mempool allocations which can violate
 * forward progress guarantees we need to provide. The current ioend we are
 * adding blocks to is cached on the writepage context, and if the new block
 * does not append to the cached ioend it will create a new ioend and cache that
 * instead.
 *
 * If a new ioend is created and cached, the old ioend is returned and queued
 * locally for submission once the entire page is processed or an error has been
 * detected.  While ioends are submitted immediately after they are completed,
 * batching optimisations are provided by higher level block plugging.
 *
 * At the end of a writeback pass, there will be a cached ioend remaining on the
 * writepage context that the caller will need to submit.
 */
static int
xfs_writepage_map(
	struct xfs_writepage_ctx *wpc,
	struct writeback_control *wbc,
	struct inode		*inode,
	struct page		*page,
	uint64_t		end_offset)
{
	LIST_HEAD(submit_list);
	struct iomap_page	*iop = to_iomap_page(page);
	unsigned		len = i_blocksize(inode);
	struct xfs_ioend	*ioend, *next;
	uint64_t		file_offset;	/* file offset of page */
	int			error = 0, count = 0, i;

	ASSERT(iop || i_blocksize(inode) == PAGE_SIZE);
	ASSERT(!iop || atomic_read(&iop->write_count) == 0);

	/*
	 * Walk through the page to find areas to write back. If we run off the
	 * end of the current map or find the current map invalid, grab a new
	 * one.
	 */
	for (i = 0, file_offset = page_offset(page);
	     i < (PAGE_SIZE >> inode->i_blkbits) && file_offset < end_offset;
	     i++, file_offset += len) {
		if (iop && !test_bit(i, iop->uptodate))
			continue;

		error = xfs_map_blocks(wpc, inode, file_offset);
		if (error)
			break;
		if (wpc->io_type == XFS_IO_HOLE)
			continue;
		xfs_add_to_ioend(inode, file_offset, page, iop, wpc, wbc,
				 &submit_list);
		count++;
	}

	ASSERT(wpc->ioend || list_empty(&submit_list));
	ASSERT(PageLocked(page));
	ASSERT(!PageWriteback(page));

	/*
	 * On error, we have to fail the ioend here because we may have set
	 * pages under writeback, we have to make sure we run IO completion to
	 * mark the error state of the IO appropriately, so we can't cancel the
	 * ioend directly here.  That means we have to mark this page as under
	 * writeback if we included any blocks from it in the ioend chain so
	 * that completion treats it correctly.
	 *
	 * If we didn't include the page in the ioend, the on error we can
	 * simply discard and unlock it as there are no other users of the page
	 * now.  The caller will still need to trigger submission of outstanding
	 * ioends on the writepage context so they are treated correctly on
	 * error.
	 */
	if (unlikely(error)) {
		if (!count) {
			xfs_aops_discard_page(page);
			ClearPageUptodate(page);
			unlock_page(page);
			goto done;
		}

		/*
		 * If the page was not fully cleaned, we need to ensure that the
		 * higher layers come back to it correctly.  That means we need
		 * to keep the page dirty, and for WB_SYNC_ALL writeback we need
		 * to ensure the PAGECACHE_TAG_TOWRITE index mark is not removed
		 * so another attempt to write this page in this writeback sweep
		 * will be made.
		 */
		set_page_writeback_keepwrite(page);
	} else {
		clear_page_dirty_for_io(page);
		set_page_writeback(page);
	}

	unlock_page(page);

	/*
	 * Preserve the original error if there was one, otherwise catch
	 * submission errors here and propagate into subsequent ioend
	 * submissions.
	 */
	list_for_each_entry_safe(ioend, next, &submit_list, io_list) {
		int error2;

		list_del_init(&ioend->io_list);
		error2 = xfs_submit_ioend(wbc, ioend, error);
		if (error2 && !error)
			error = error2;
	}

	/*
	 * We can end up here with no error and nothing to write only if we race
	 * with a partial page truncate on a sub-page block sized filesystem.
	 */
	if (!count)
		end_page_writeback(page);
done:
	mapping_set_error(page->mapping, error);
	return error;
}

/*
 * Write out a dirty page.
 *
 * For delalloc space on the page we need to allocate space and flush it.
 * For unwritten space on the page we need to start the conversion to
 * regular allocated space.
 */
STATIC int
xfs_do_writepage(
	struct page		*page,
	struct writeback_control *wbc,
	void			*data)
{
	struct xfs_writepage_ctx *wpc = data;
	struct inode		*inode = page->mapping->host;
	loff_t			offset;
	uint64_t              end_offset;
	pgoff_t                 end_index;

	trace_xfs_writepage(inode, page, 0, 0);

	/*
	 * Refuse to write the page out if we are called from reclaim context.
	 *
	 * This avoids stack overflows when called from deeply used stacks in
	 * random callers for direct reclaim or memcg reclaim.  We explicitly
	 * allow reclaim from kswapd as the stack usage there is relatively low.
	 *
	 * This should never happen except in the case of a VM regression so
	 * warn about it.
	 */
	if (WARN_ON_ONCE((current->flags & (PF_MEMALLOC|PF_KSWAPD)) ==
			PF_MEMALLOC))
		goto redirty;

	/*
	 * Given that we do not allow direct reclaim to call us, we should
	 * never be called while in a filesystem transaction.
	 */
	if (WARN_ON_ONCE(current->flags & PF_MEMALLOC_NOFS))
		goto redirty;

	/*
	 * Is this page beyond the end of the file?
	 *
	 * The page index is less than the end_index, adjust the end_offset
	 * to the highest offset that this page should represent.
	 * -----------------------------------------------------
	 * |			file mapping	       | <EOF> |
	 * -----------------------------------------------------
	 * | Page ... | Page N-2 | Page N-1 |  Page N  |       |
	 * ^--------------------------------^----------|--------
	 * |     desired writeback range    |      see else    |
	 * ---------------------------------^------------------|
	 */
	offset = i_size_read(inode);
	end_index = offset >> PAGE_SHIFT;
	if (page->index < end_index)
		end_offset = (xfs_off_t)(page->index + 1) << PAGE_SHIFT;
	else {
		/*
		 * Check whether the page to write out is beyond or straddles
		 * i_size or not.
		 * -------------------------------------------------------
		 * |		file mapping		        | <EOF>  |
		 * -------------------------------------------------------
		 * | Page ... | Page N-2 | Page N-1 |  Page N   | Beyond |
		 * ^--------------------------------^-----------|---------
		 * |				    |      Straddles     |
		 * ---------------------------------^-----------|--------|
		 */
		unsigned offset_into_page = offset & (PAGE_SIZE - 1);

		/*
		 * Skip the page if it is fully outside i_size, e.g. due to a
		 * truncate operation that is in progress. We must redirty the
		 * page so that reclaim stops reclaiming it. Otherwise
		 * xfs_vm_releasepage() is called on it and gets confused.
		 *
		 * Note that the end_index is unsigned long, it would overflow
		 * if the given offset is greater than 16TB on 32-bit system
		 * and if we do check the page is fully outside i_size or not
		 * via "if (page->index >= end_index + 1)" as "end_index + 1"
		 * will be evaluated to 0.  Hence this page will be redirtied
		 * and be written out repeatedly which would result in an
		 * infinite loop, the user program that perform this operation
		 * will hang.  Instead, we can verify this situation by checking
		 * if the page to write is totally beyond the i_size or if it's
		 * offset is just equal to the EOF.
		 */
		if (page->index > end_index ||
		    (page->index == end_index && offset_into_page == 0))
			goto redirty;

		/*
		 * The page straddles i_size.  It must be zeroed out on each
		 * and every writepage invocation because it may be mmapped.
		 * "A file is mapped in multiples of the page size.  For a file
		 * that is not a multiple of the page size, the remaining
		 * memory is zeroed when mapped, and writes to that region are
		 * not written out to the file."
		 */
		zero_user_segment(page, offset_into_page, PAGE_SIZE);

		/* Adjust the end_offset to the end of file */
		end_offset = offset;
	}

	return xfs_writepage_map(wpc, wbc, inode, page, end_offset);

redirty:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return 0;
}

STATIC int
xfs_vm_writepage(
	struct page		*page,
	struct writeback_control *wbc)
{
	struct xfs_writepage_ctx wpc = {
		.io_type = XFS_IO_INVALID,
	};
	int			ret;

	ret = xfs_do_writepage(page, wbc, &wpc);
	if (wpc.ioend)
		ret = xfs_submit_ioend(wbc, wpc.ioend, ret);
	return ret;
}

STATIC int
xfs_vm_writepages(
	struct address_space	*mapping,
	struct writeback_control *wbc)
{
	struct xfs_writepage_ctx wpc = {
		.io_type = XFS_IO_INVALID,
	};
	int			ret;

	xfs_iflags_clear(XFS_I(mapping->host), XFS_ITRUNCATED);
	ret = write_cache_pages(mapping, wbc, xfs_do_writepage, &wpc);
	if (wpc.ioend)
		ret = xfs_submit_ioend(wbc, wpc.ioend, ret);
	return ret;
}

STATIC int
xfs_dax_writepages(
	struct address_space	*mapping,
	struct writeback_control *wbc)
{
	xfs_iflags_clear(XFS_I(mapping->host), XFS_ITRUNCATED);
	return dax_writeback_mapping_range(mapping,
			xfs_find_bdev_for_inode(mapping->host), wbc);
}

STATIC int
xfs_vm_releasepage(
	struct page		*page,
	gfp_t			gfp_mask)
{
	trace_xfs_releasepage(page->mapping->host, page, 0, 0);
	return iomap_releasepage(page, gfp_mask);
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
	if (xfs_is_reflink_inode(ip) || XFS_IS_REALTIME_INODE(ip))
		return 0;
	return iomap_bmap(mapping, block, &xfs_iomap_ops);
}

STATIC int
xfs_vm_readpage(
	struct file		*unused,
	struct page		*page)
{
	trace_xfs_vm_readpage(page->mapping->host, 1);
	return iomap_readpage(page, &xfs_iomap_ops);
}

STATIC int
xfs_vm_readpages(
	struct file		*unused,
	struct address_space	*mapping,
	struct list_head	*pages,
	unsigned		nr_pages)
{
	trace_xfs_vm_readpages(mapping->host, nr_pages);
	return iomap_readpages(mapping, pages, nr_pages, &xfs_iomap_ops);
}

static int
xfs_iomap_swapfile_activate(
	struct swap_info_struct		*sis,
	struct file			*swap_file,
	sector_t			*span)
{
	sis->bdev = xfs_find_bdev_for_inode(file_inode(swap_file));
	return iomap_swapfile_activate(sis, swap_file, span, &xfs_iomap_ops);
}

const struct address_space_operations xfs_address_space_operations = {
	.readpage		= xfs_vm_readpage,
	.readpages		= xfs_vm_readpages,
	.writepage		= xfs_vm_writepage,
	.writepages		= xfs_vm_writepages,
	.set_page_dirty		= iomap_set_page_dirty,
	.releasepage		= xfs_vm_releasepage,
	.invalidatepage		= xfs_vm_invalidatepage,
	.bmap			= xfs_vm_bmap,
	.direct_IO		= noop_direct_IO,
	.migratepage		= iomap_migrate_page,
	.is_partially_uptodate  = iomap_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
	.swap_activate		= xfs_iomap_swapfile_activate,
};

const struct address_space_operations xfs_dax_aops = {
	.writepages		= xfs_dax_writepages,
	.direct_IO		= noop_direct_IO,
	.set_page_dirty		= noop_set_page_dirty,
	.invalidatepage		= noop_invalidatepage,
	.swap_activate		= xfs_iomap_swapfile_activate,
};
