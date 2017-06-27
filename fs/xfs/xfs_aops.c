/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write the Free Software Foundation,
 * Inc.,  51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
#include <linux/gfp.h>
#include <linux/mpage.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>

/*
 * structure owned by writepages passed to individual writepage calls
 */
struct xfs_writepage_ctx {
	struct xfs_bmbt_irec    imap;
	bool			imap_valid;
	unsigned int		io_type;
	struct xfs_ioend	*ioend;
	sector_t		last_block;
};

void
xfs_count_page_state(
	struct page		*page,
	int			*delalloc,
	int			*unwritten)
{
	struct buffer_head	*bh, *head;

	*delalloc = *unwritten = 0;

	bh = head = page_buffers(page);
	do {
		if (buffer_unwritten(bh))
			(*unwritten) = 1;
		else if (buffer_delay(bh))
			(*delalloc) = 1;
	} while ((bh = bh->b_this_page) != head);
}

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

/*
 * We're now finished for good with this page.  Update the page state via the
 * associated buffer_heads, paying attention to the start and end offsets that
 * we need to process on the page.
 *
 * Landmine Warning: bh->b_end_io() will call end_page_writeback() on the last
 * buffer in the IO. Once it does this, it is unsafe to access the bufferhead or
 * the page at all, as we may be racing with memory reclaim and it can free both
 * the bufferhead chain and the page as it will see the page as clean and
 * unused.
 */
static void
xfs_finish_page_writeback(
	struct inode		*inode,
	struct bio_vec		*bvec,
	int			error)
{
	unsigned int		end = bvec->bv_offset + bvec->bv_len - 1;
	struct buffer_head	*head, *bh, *next;
	unsigned int		off = 0;
	unsigned int		bsize;

	ASSERT(bvec->bv_offset < PAGE_SIZE);
	ASSERT((bvec->bv_offset & (i_blocksize(inode) - 1)) == 0);
	ASSERT(end < PAGE_SIZE);
	ASSERT((bvec->bv_len & (i_blocksize(inode) - 1)) == 0);

	bh = head = page_buffers(bvec->bv_page);

	bsize = bh->b_size;
	do {
		if (off > end)
			break;
		next = bh->b_this_page;
		if (off < bvec->bv_offset)
			goto next_bh;
		bh->b_end_io(bh, !error);
next_bh:
		off += bsize;
	} while ((bh = next) != head);
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
	struct bio		*last = ioend->io_bio;
	struct bio		*bio, *next;

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

	error = xfs_trans_alloc(mp, &M_RES(mp)->tr_fsyncts, 0, 0, 0, &tp);
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
	int			error = ioend->io_bio->bi_error;

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
		error = xfs_iomap_write_unwritten(ip, offset, size);
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
		xfs_destroy_ioend(ioend, bio->bi_error);
}

STATIC int
xfs_map_blocks(
	struct inode		*inode,
	loff_t			offset,
	struct xfs_bmbt_irec	*imap,
	int			type)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			count = i_blocksize(inode);
	xfs_fileoff_t		offset_fsb, end_fsb;
	int			error = 0;
	int			bmapi_flags = XFS_BMAPI_ENTIRE;
	int			nimaps = 1;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	ASSERT(type != XFS_IO_COW);
	if (type == XFS_IO_UNWRITTEN)
		bmapi_flags |= XFS_BMAPI_IGSTATE;

	xfs_ilock(ip, XFS_ILOCK_SHARED);
	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       (ip->i_df.if_flags & XFS_IFEXTENTS));
	ASSERT(offset <= mp->m_super->s_maxbytes);

	if (offset + count > mp->m_super->s_maxbytes)
		count = mp->m_super->s_maxbytes - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb,
				imap, &nimaps, bmapi_flags);
	/*
	 * Truncate an overwrite extent if there's a pending CoW
	 * reservation before the end of this extent.  This forces us
	 * to come back to writepage to take care of the CoW.
	 */
	if (nimaps && type == XFS_IO_OVERWRITE)
		xfs_reflink_trim_irec_to_next_cow(ip, offset_fsb, imap);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (error)
		return error;

	if (type == XFS_IO_DELALLOC &&
	    (!nimaps || isnullstartblock(imap->br_startblock))) {
		error = xfs_iomap_write_allocate(ip, XFS_DATA_FORK, offset,
				imap);
		if (!error)
			trace_xfs_map_blocks_alloc(ip, offset, count, type, imap);
		return error;
	}

#ifdef DEBUG
	if (type == XFS_IO_UNWRITTEN) {
		ASSERT(nimaps);
		ASSERT(imap->br_startblock != HOLESTARTBLOCK);
		ASSERT(imap->br_startblock != DELAYSTARTBLOCK);
	}
#endif
	if (nimaps)
		trace_xfs_map_blocks_found(ip, offset, count, type, imap);
	return 0;
}

STATIC bool
xfs_imap_valid(
	struct inode		*inode,
	struct xfs_bmbt_irec	*imap,
	xfs_off_t		offset)
{
	offset >>= inode->i_blkbits;

	return offset >= imap->br_startoff &&
		offset < imap->br_startoff + imap->br_blockcount;
}

STATIC void
xfs_start_buffer_writeback(
	struct buffer_head	*bh)
{
	ASSERT(buffer_mapped(bh));
	ASSERT(buffer_locked(bh));
	ASSERT(!buffer_delay(bh));
	ASSERT(!buffer_unwritten(bh));

	mark_buffer_async_write(bh);
	set_buffer_uptodate(bh);
	clear_buffer_dirty(bh);
}

STATIC void
xfs_start_page_writeback(
	struct page		*page,
	int			clear_dirty)
{
	ASSERT(PageLocked(page));
	ASSERT(!PageWriteback(page));

	/*
	 * if the page was not fully cleaned, we need to ensure that the higher
	 * layers come back to it correctly. That means we need to keep the page
	 * dirty, and for WB_SYNC_ALL writeback we need to ensure the
	 * PAGECACHE_TAG_TOWRITE index mark is not removed so another attempt to
	 * write this page in this writeback sweep will be made.
	 */
	if (clear_dirty) {
		clear_page_dirty_for_io(page);
		set_page_writeback(page);
	} else
		set_page_writeback_keepwrite(page);

	unlock_page(page);
}

static inline int xfs_bio_add_buffer(struct bio *bio, struct buffer_head *bh)
{
	return bio_add_page(bio, bh->b_page, bh->b_size, bh_offset(bh));
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
		status = xfs_reflink_convert_cow(XFS_I(ioend->io_inode),
				ioend->io_offset, ioend->io_size);
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
		ioend->io_bio->bi_error = status;
		bio_endio(ioend->io_bio);
		return status;
	}

	submit_bio(ioend->io_bio);
	return 0;
}

static void
xfs_init_bio_from_bh(
	struct bio		*bio,
	struct buffer_head	*bh)
{
	bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_bdev = bh->b_bdev;
}

static struct xfs_ioend *
xfs_alloc_ioend(
	struct inode		*inode,
	unsigned int		type,
	xfs_off_t		offset,
	struct buffer_head	*bh)
{
	struct xfs_ioend	*ioend;
	struct bio		*bio;

	bio = bio_alloc_bioset(GFP_NOFS, BIO_MAX_PAGES, xfs_ioend_bioset);
	xfs_init_bio_from_bh(bio, bh);

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
	struct buffer_head	*bh)
{
	struct bio *new;

	new = bio_alloc(GFP_NOFS, BIO_MAX_PAGES);
	xfs_init_bio_from_bh(new, bh);

	bio_chain(ioend->io_bio, new);
	bio_get(ioend->io_bio);		/* for xfs_destroy_ioend */
	ioend->io_bio->bi_opf = REQ_OP_WRITE | wbc_to_write_flags(wbc);
	submit_bio(ioend->io_bio);
	ioend->io_bio = new;
}

/*
 * Test to see if we've been building up a completion structure for
 * earlier buffers -- if so, we try to append to this ioend if we
 * can, otherwise we finish off any current ioend and start another.
 * Return the ioend we finished off so that the caller can submit it
 * once it has finished processing the dirty page.
 */
STATIC void
xfs_add_to_ioend(
	struct inode		*inode,
	struct buffer_head	*bh,
	xfs_off_t		offset,
	struct xfs_writepage_ctx *wpc,
	struct writeback_control *wbc,
	struct list_head	*iolist)
{
	if (!wpc->ioend || wpc->io_type != wpc->ioend->io_type ||
	    bh->b_blocknr != wpc->last_block + 1 ||
	    offset != wpc->ioend->io_offset + wpc->ioend->io_size) {
		if (wpc->ioend)
			list_add(&wpc->ioend->io_list, iolist);
		wpc->ioend = xfs_alloc_ioend(inode, wpc->io_type, offset, bh);
	}

	/*
	 * If the buffer doesn't fit into the bio we need to allocate a new
	 * one.  This shouldn't happen more than once for a given buffer.
	 */
	while (xfs_bio_add_buffer(wpc->ioend->io_bio, bh) != bh->b_size)
		xfs_chain_bio(wpc->ioend, wbc, bh);

	wpc->ioend->io_size += bh->b_size;
	wpc->last_block = bh->b_blocknr;
	xfs_start_buffer_writeback(bh);
}

STATIC void
xfs_map_buffer(
	struct inode		*inode,
	struct buffer_head	*bh,
	struct xfs_bmbt_irec	*imap,
	xfs_off_t		offset)
{
	sector_t		bn;
	struct xfs_mount	*m = XFS_I(inode)->i_mount;
	xfs_off_t		iomap_offset = XFS_FSB_TO_B(m, imap->br_startoff);
	xfs_daddr_t		iomap_bn = xfs_fsb_to_db(XFS_I(inode), imap->br_startblock);

	ASSERT(imap->br_startblock != HOLESTARTBLOCK);
	ASSERT(imap->br_startblock != DELAYSTARTBLOCK);

	bn = (iomap_bn >> (inode->i_blkbits - BBSHIFT)) +
	      ((offset - iomap_offset) >> inode->i_blkbits);

	ASSERT(bn || XFS_IS_REALTIME_INODE(XFS_I(inode)));

	bh->b_blocknr = bn;
	set_buffer_mapped(bh);
}

STATIC void
xfs_map_at_offset(
	struct inode		*inode,
	struct buffer_head	*bh,
	struct xfs_bmbt_irec	*imap,
	xfs_off_t		offset)
{
	ASSERT(imap->br_startblock != HOLESTARTBLOCK);
	ASSERT(imap->br_startblock != DELAYSTARTBLOCK);

	xfs_map_buffer(inode, bh, imap, offset);
	set_buffer_mapped(bh);
	clear_buffer_delay(bh);
	clear_buffer_unwritten(bh);
}

/*
 * Test if a given page contains at least one buffer of a given @type.
 * If @check_all_buffers is true, then we walk all the buffers in the page to
 * try to find one of the type passed in. If it is not set, then the caller only
 * needs to check the first buffer on the page for a match.
 */
STATIC bool
xfs_check_page_type(
	struct page		*page,
	unsigned int		type,
	bool			check_all_buffers)
{
	struct buffer_head	*bh;
	struct buffer_head	*head;

	if (PageWriteback(page))
		return false;
	if (!page->mapping)
		return false;
	if (!page_has_buffers(page))
		return false;

	bh = head = page_buffers(page);
	do {
		if (buffer_unwritten(bh)) {
			if (type == XFS_IO_UNWRITTEN)
				return true;
		} else if (buffer_delay(bh)) {
			if (type == XFS_IO_DELALLOC)
				return true;
		} else if (buffer_dirty(bh) && buffer_mapped(bh)) {
			if (type == XFS_IO_OVERWRITE)
				return true;
		}

		/* If we are only checking the first buffer, we are done now. */
		if (!check_all_buffers)
			break;
	} while ((bh = bh->b_this_page) != head);

	return false;
}

STATIC void
xfs_vm_invalidatepage(
	struct page		*page,
	unsigned int		offset,
	unsigned int		length)
{
	trace_xfs_invalidatepage(page->mapping->host, page, offset,
				 length);
	block_invalidatepage(page, offset, length);
}

/*
 * If the page has delalloc buffers on it, we need to punch them out before we
 * invalidate the page. If we don't, we leave a stale delalloc mapping on the
 * inode that can trip a BUG() in xfs_get_blocks() later on if a direct IO read
 * is done on that same region - the delalloc extent is returned when none is
 * supposed to be there.
 *
 * We prevent this by truncating away the delalloc regions on the page before
 * invalidating it. Because they are delalloc, we can do this without needing a
 * transaction. Indeed - if we get ENOSPC errors, we have to be able to do this
 * truncation without a transaction as there is no space left for block
 * reservation (typically why we see a ENOSPC in writeback).
 *
 * This is not a performance critical path, so for now just do the punching a
 * buffer head at a time.
 */
STATIC void
xfs_aops_discard_page(
	struct page		*page)
{
	struct inode		*inode = page->mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);
	struct buffer_head	*bh, *head;
	loff_t			offset = page_offset(page);

	if (!xfs_check_page_type(page, XFS_IO_DELALLOC, true))
		goto out_invalidate;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		goto out_invalidate;

	xfs_alert(ip->i_mount,
		"page discard on page %p, inode 0x%llx, offset %llu.",
			page, ip->i_ino, offset);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	bh = head = page_buffers(page);
	do {
		int		error;
		xfs_fileoff_t	start_fsb;

		if (!buffer_delay(bh))
			goto next_buffer;

		start_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);
		error = xfs_bmap_punch_delalloc_range(ip, start_fsb, 1);
		if (error) {
			/* something screwed, just bail */
			if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
				xfs_alert(ip->i_mount,
			"page discard unable to remove delalloc mapping.");
			}
			break;
		}
next_buffer:
		offset += i_blocksize(inode);

	} while ((bh = bh->b_this_page) != head);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out_invalidate:
	xfs_vm_invalidatepage(page, 0, PAGE_SIZE);
	return;
}

static int
xfs_map_cow(
	struct xfs_writepage_ctx *wpc,
	struct inode		*inode,
	loff_t			offset,
	unsigned int		*new_type)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_bmbt_irec	imap;
	bool			is_cow = false;
	int			error;

	/*
	 * If we already have a valid COW mapping keep using it.
	 */
	if (wpc->io_type == XFS_IO_COW) {
		wpc->imap_valid = xfs_imap_valid(inode, &wpc->imap, offset);
		if (wpc->imap_valid) {
			*new_type = XFS_IO_COW;
			return 0;
		}
	}

	/*
	 * Else we need to check if there is a COW mapping at this offset.
	 */
	xfs_ilock(ip, XFS_ILOCK_SHARED);
	is_cow = xfs_reflink_find_cow_mapping(ip, offset, &imap);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (!is_cow)
		return 0;

	/*
	 * And if the COW mapping has a delayed extent here we need to
	 * allocate real space for it now.
	 */
	if (isnullstartblock(imap.br_startblock)) {
		error = xfs_iomap_write_allocate(ip, XFS_COW_FORK, offset,
				&imap);
		if (error)
			return error;
	}

	wpc->io_type = *new_type = XFS_IO_COW;
	wpc->imap_valid = true;
	wpc->imap = imap;
	return 0;
}

/*
 * We implement an immediate ioend submission policy here to avoid needing to
 * chain multiple ioends and hence nest mempool allocations which can violate
 * forward progress guarantees we need to provide. The current ioend we are
 * adding buffers to is cached on the writepage context, and if the new buffer
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
	loff_t			offset,
	__uint64_t              end_offset)
{
	LIST_HEAD(submit_list);
	struct xfs_ioend	*ioend, *next;
	struct buffer_head	*bh, *head;
	ssize_t			len = i_blocksize(inode);
	int			error = 0;
	int			count = 0;
	int			uptodate = 1;
	unsigned int		new_type;

	bh = head = page_buffers(page);
	offset = page_offset(page);
	do {
		if (offset >= end_offset)
			break;
		if (!buffer_uptodate(bh))
			uptodate = 0;

		/*
		 * set_page_dirty dirties all buffers in a page, independent
		 * of their state.  The dirty state however is entirely
		 * meaningless for holes (!mapped && uptodate), so skip
		 * buffers covering holes here.
		 */
		if (!buffer_mapped(bh) && buffer_uptodate(bh)) {
			wpc->imap_valid = false;
			continue;
		}

		if (buffer_unwritten(bh))
			new_type = XFS_IO_UNWRITTEN;
		else if (buffer_delay(bh))
			new_type = XFS_IO_DELALLOC;
		else if (buffer_uptodate(bh))
			new_type = XFS_IO_OVERWRITE;
		else {
			if (PageUptodate(page))
				ASSERT(buffer_mapped(bh));
			/*
			 * This buffer is not uptodate and will not be
			 * written to disk.  Ensure that we will put any
			 * subsequent writeable buffers into a new
			 * ioend.
			 */
			wpc->imap_valid = false;
			continue;
		}

		if (xfs_is_reflink_inode(XFS_I(inode))) {
			error = xfs_map_cow(wpc, inode, offset, &new_type);
			if (error)
				goto out;
		}

		if (wpc->io_type != new_type) {
			wpc->io_type = new_type;
			wpc->imap_valid = false;
		}

		if (wpc->imap_valid)
			wpc->imap_valid = xfs_imap_valid(inode, &wpc->imap,
							 offset);
		if (!wpc->imap_valid) {
			error = xfs_map_blocks(inode, offset, &wpc->imap,
					     wpc->io_type);
			if (error)
				goto out;
			wpc->imap_valid = xfs_imap_valid(inode, &wpc->imap,
							 offset);
		}
		if (wpc->imap_valid) {
			lock_buffer(bh);
			if (wpc->io_type != XFS_IO_OVERWRITE)
				xfs_map_at_offset(inode, bh, &wpc->imap, offset);
			xfs_add_to_ioend(inode, bh, offset, wpc, wbc, &submit_list);
			count++;
		}

	} while (offset += len, ((bh = bh->b_this_page) != head));

	if (uptodate && bh == head)
		SetPageUptodate(page);

	ASSERT(wpc->ioend || list_empty(&submit_list));

out:
	/*
	 * On error, we have to fail the ioend here because we have locked
	 * buffers in the ioend. If we don't do this, we'll deadlock
	 * invalidating the page as that tries to lock the buffers on the page.
	 * Also, because we may have set pages under writeback, we have to make
	 * sure we run IO completion to mark the error state of the IO
	 * appropriately, so we can't cancel the ioend directly here. That means
	 * we have to mark this page as under writeback if we included any
	 * buffers from it in the ioend chain so that completion treats it
	 * correctly.
	 *
	 * If we didn't include the page in the ioend, the on error we can
	 * simply discard and unlock it as there are no other users of the page
	 * or it's buffers right now. The caller will still need to trigger
	 * submission of outstanding ioends on the writepage context so they are
	 * treated correctly on error.
	 */
	if (count) {
		xfs_start_page_writeback(page, !error);

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
	} else if (error) {
		xfs_aops_discard_page(page);
		ClearPageUptodate(page);
		unlock_page(page);
	} else {
		/*
		 * We can end up here with no error and nothing to write if we
		 * race with a partial page truncate on a sub-page block sized
		 * filesystem. In that case we need to mark the page clean.
		 */
		xfs_start_page_writeback(page, 1);
		end_page_writeback(page);
	}

	mapping_set_error(page->mapping, error);
	return error;
}

/*
 * Write out a dirty page.
 *
 * For delalloc space on the page we need to allocate space and flush it.
 * For unwritten space on the page we need to start the conversion to
 * regular allocated space.
 * For any other dirty buffer heads on the page we should flush them.
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
	__uint64_t              end_offset;
	pgoff_t                 end_index;

	trace_xfs_writepage(inode, page, 0, 0);

	ASSERT(page_has_buffers(page));

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

	return xfs_writepage_map(wpc, wbc, inode, page, offset, end_offset);

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
	if (dax_mapping(mapping))
		return dax_writeback_mapping_range(mapping,
				xfs_find_bdev_for_inode(mapping->host), wbc);

	ret = write_cache_pages(mapping, wbc, xfs_do_writepage, &wpc);
	if (wpc.ioend)
		ret = xfs_submit_ioend(wbc, wpc.ioend, ret);
	return ret;
}

/*
 * Called to move a page into cleanable state - and from there
 * to be released. The page should already be clean. We always
 * have buffer heads in this call.
 *
 * Returns 1 if the page is ok to release, 0 otherwise.
 */
STATIC int
xfs_vm_releasepage(
	struct page		*page,
	gfp_t			gfp_mask)
{
	int			delalloc, unwritten;

	trace_xfs_releasepage(page->mapping->host, page, 0, 0);

	/*
	 * mm accommodates an old ext3 case where clean pages might not have had
	 * the dirty bit cleared. Thus, it can send actual dirty pages to
	 * ->releasepage() via shrink_active_list(). Conversely,
	 * block_invalidatepage() can send pages that are still marked dirty
	 * but otherwise have invalidated buffers.
	 *
	 * We want to release the latter to avoid unnecessary buildup of the
	 * LRU, skip the former and warn if we've left any lingering
	 * delalloc/unwritten buffers on clean pages. Skip pages with delalloc
	 * or unwritten buffers and warn if the page is not dirty. Otherwise
	 * try to release the buffers.
	 */
	xfs_count_page_state(page, &delalloc, &unwritten);

	if (delalloc) {
		WARN_ON_ONCE(!PageDirty(page));
		return 0;
	}
	if (unwritten) {
		WARN_ON_ONCE(!PageDirty(page));
		return 0;
	}

	return try_to_free_buffers(page);
}

/*
 * If this is O_DIRECT or the mpage code calling tell them how large the mapping
 * is, so that we can avoid repeated get_blocks calls.
 *
 * If the mapping spans EOF, then we have to break the mapping up as the mapping
 * for blocks beyond EOF must be marked new so that sub block regions can be
 * correctly zeroed. We can't do this for mappings within EOF unless the mapping
 * was just allocated or is unwritten, otherwise the callers would overwrite
 * existing data with zeros. Hence we have to split the mapping into a range up
 * to and including EOF, and a second mapping for beyond EOF.
 */
static void
xfs_map_trim_size(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	struct xfs_bmbt_irec	*imap,
	xfs_off_t		offset,
	ssize_t			size)
{
	xfs_off_t		mapping_size;

	mapping_size = imap->br_startoff + imap->br_blockcount - iblock;
	mapping_size <<= inode->i_blkbits;

	ASSERT(mapping_size > 0);
	if (mapping_size > size)
		mapping_size = size;
	if (offset < i_size_read(inode) &&
	    offset + mapping_size >= i_size_read(inode)) {
		/* limit mapping to block that spans EOF */
		mapping_size = roundup_64(i_size_read(inode) - offset,
					  i_blocksize(inode));
	}
	if (mapping_size > LONG_MAX)
		mapping_size = LONG_MAX;

	bh_result->b_size = mapping_size;
}

static int
xfs_get_blocks(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	xfs_fileoff_t		offset_fsb, end_fsb;
	int			error = 0;
	int			lockmode = 0;
	struct xfs_bmbt_irec	imap;
	int			nimaps = 1;
	xfs_off_t		offset;
	ssize_t			size;

	BUG_ON(create);

	if (XFS_FORCED_SHUTDOWN(mp))
		return -EIO;

	offset = (xfs_off_t)iblock << inode->i_blkbits;
	ASSERT(bh_result->b_size >= i_blocksize(inode));
	size = bh_result->b_size;

	if (offset >= i_size_read(inode))
		return 0;

	/*
	 * Direct I/O is usually done on preallocated files, so try getting
	 * a block mapping without an exclusive lock first.
	 */
	lockmode = xfs_ilock_data_map_shared(ip);

	ASSERT(offset <= mp->m_super->s_maxbytes);
	if (offset + size > mp->m_super->s_maxbytes)
		size = mp->m_super->s_maxbytes - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + size);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb,
				&imap, &nimaps, XFS_BMAPI_ENTIRE);
	if (error)
		goto out_unlock;

	if (nimaps) {
		trace_xfs_get_blocks_found(ip, offset, size,
			imap.br_state == XFS_EXT_UNWRITTEN ?
				XFS_IO_UNWRITTEN : XFS_IO_OVERWRITE, &imap);
		xfs_iunlock(ip, lockmode);
	} else {
		trace_xfs_get_blocks_notfound(ip, offset, size);
		goto out_unlock;
	}

	/* trim mapping down to size requested */
	xfs_map_trim_size(inode, iblock, bh_result, &imap, offset, size);

	/*
	 * For unwritten extents do not report a disk address in the buffered
	 * read case (treat as if we're reading into a hole).
	 */
	if (xfs_bmap_is_real_extent(&imap))
		xfs_map_buffer(inode, bh_result, &imap, offset);

	/*
	 * If this is a realtime file, data may be on a different device.
	 * to that pointed to from the buffer_head b_bdev currently.
	 */
	bh_result->b_bdev = xfs_find_bdev_for_inode(inode);
	return 0;

out_unlock:
	xfs_iunlock(ip, lockmode);
	return error;
}

STATIC ssize_t
xfs_vm_direct_IO(
	struct kiocb		*iocb,
	struct iov_iter		*iter)
{
	/*
	 * We just need the method present so that open/fcntl allow direct I/O.
	 */
	return -EINVAL;
}

STATIC sector_t
xfs_vm_bmap(
	struct address_space	*mapping,
	sector_t		block)
{
	struct inode		*inode = (struct inode *)mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);

	trace_xfs_vm_bmap(XFS_I(inode));

	/*
	 * The swap code (ab-)uses ->bmap to get a block mapping and then
	 * bypasseѕ the file system for actual I/O.  We really can't allow
	 * that on reflinks inodes, so we have to skip out here.  And yes,
	 * 0 is the magic code for a bmap error..
	 */
	if (xfs_is_reflink_inode(ip))
		return 0;

	filemap_write_and_wait(mapping);
	return generic_block_bmap(mapping, block, xfs_get_blocks);
}

STATIC int
xfs_vm_readpage(
	struct file		*unused,
	struct page		*page)
{
	trace_xfs_vm_readpage(page->mapping->host, 1);
	return mpage_readpage(page, xfs_get_blocks);
}

STATIC int
xfs_vm_readpages(
	struct file		*unused,
	struct address_space	*mapping,
	struct list_head	*pages,
	unsigned		nr_pages)
{
	trace_xfs_vm_readpages(mapping->host, nr_pages);
	return mpage_readpages(mapping, pages, nr_pages, xfs_get_blocks);
}

/*
 * This is basically a copy of __set_page_dirty_buffers() with one
 * small tweak: buffers beyond EOF do not get marked dirty. If we mark them
 * dirty, we'll never be able to clean them because we don't write buffers
 * beyond EOF, and that means we can't invalidate pages that span EOF
 * that have been marked dirty. Further, the dirty state can leak into
 * the file interior if the file is extended, resulting in all sorts of
 * bad things happening as the state does not match the underlying data.
 *
 * XXX: this really indicates that bufferheads in XFS need to die. Warts like
 * this only exist because of bufferheads and how the generic code manages them.
 */
STATIC int
xfs_vm_set_page_dirty(
	struct page		*page)
{
	struct address_space	*mapping = page->mapping;
	struct inode		*inode = mapping->host;
	loff_t			end_offset;
	loff_t			offset;
	int			newly_dirty;

	if (unlikely(!mapping))
		return !TestSetPageDirty(page);

	end_offset = i_size_read(inode);
	offset = page_offset(page);

	spin_lock(&mapping->private_lock);
	if (page_has_buffers(page)) {
		struct buffer_head *head = page_buffers(page);
		struct buffer_head *bh = head;

		do {
			if (offset < end_offset)
				set_buffer_dirty(bh);
			bh = bh->b_this_page;
			offset += i_blocksize(inode);
		} while (bh != head);
	}
	/*
	 * Lock out page->mem_cgroup migration to keep PageDirty
	 * synchronized with per-memcg dirty page counters.
	 */
	lock_page_memcg(page);
	newly_dirty = !TestSetPageDirty(page);
	spin_unlock(&mapping->private_lock);

	if (newly_dirty) {
		/* sigh - __set_page_dirty() is static, so copy it here, too */
		unsigned long flags;

		spin_lock_irqsave(&mapping->tree_lock, flags);
		if (page->mapping) {	/* Race with truncate? */
			WARN_ON_ONCE(!PageUptodate(page));
			account_page_dirtied(page, mapping);
			radix_tree_tag_set(&mapping->page_tree,
					page_index(page), PAGECACHE_TAG_DIRTY);
		}
		spin_unlock_irqrestore(&mapping->tree_lock, flags);
	}
	unlock_page_memcg(page);
	if (newly_dirty)
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	return newly_dirty;
}

const struct address_space_operations xfs_address_space_operations = {
	.readpage		= xfs_vm_readpage,
	.readpages		= xfs_vm_readpages,
	.writepage		= xfs_vm_writepage,
	.writepages		= xfs_vm_writepages,
	.set_page_dirty		= xfs_vm_set_page_dirty,
	.releasepage		= xfs_vm_releasepage,
	.invalidatepage		= xfs_vm_invalidatepage,
	.bmap			= xfs_vm_bmap,
	.direct_IO		= xfs_vm_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};
