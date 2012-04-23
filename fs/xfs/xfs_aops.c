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
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_inum.h"
#include "xfs_sb.h"
#include "xfs_ag.h"
#include "xfs_trans.h"
#include "xfs_mount.h"
#include "xfs_bmap_btree.h"
#include "xfs_dinode.h"
#include "xfs_inode.h"
#include "xfs_inode_item.h"
#include "xfs_alloc.h"
#include "xfs_error.h"
#include "xfs_rw.h"
#include "xfs_iomap.h"
#include "xfs_vnodeops.h"
#include "xfs_trace.h"
#include "xfs_bmap.h"
#include <linux/gfp.h>
#include <linux/mpage.h>
#include <linux/pagevec.h>
#include <linux/writeback.h>

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

STATIC struct block_device *
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
 * We're now finished for good with this ioend structure.
 * Update the page state via the associated buffer_heads,
 * release holds on the inode and bio, and finally free
 * up memory.  Do not use the ioend after this.
 */
STATIC void
xfs_destroy_ioend(
	xfs_ioend_t		*ioend)
{
	struct buffer_head	*bh, *next;

	for (bh = ioend->io_buffer_head; bh; bh = next) {
		next = bh->b_private;
		bh->b_end_io(bh, !ioend->io_error);
	}

	if (ioend->io_iocb) {
		if (ioend->io_isasync) {
			aio_complete(ioend->io_iocb, ioend->io_error ?
					ioend->io_error : ioend->io_result, 0);
		}
		inode_dio_done(ioend->io_inode);
	}

	mempool_free(ioend, xfs_ioend_pool);
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

	tp = xfs_trans_alloc(mp, XFS_TRANS_FSYNC_TS);

	error = xfs_trans_reserve(tp, 0, XFS_FSYNC_TS_LOG_RES(mp), 0, 0, 0);
	if (error) {
		xfs_trans_cancel(tp, 0);
		return error;
	}

	ioend->io_append_trans = tp;

	/*
	 * We hand off the transaction to the completion thread now, so
	 * clear the flag here.
	 */
	current_restore_flags_nested(&tp->t_pflags, PF_FSTRANS);
	return 0;
}

/*
 * Update on-disk file size now that data has been written to disk.
 */
STATIC int
xfs_setfilesize(
	struct xfs_ioend	*ioend)
{
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);
	struct xfs_trans	*tp = ioend->io_append_trans;
	xfs_fsize_t		isize;

	/*
	 * The transaction was allocated in the I/O submission thread,
	 * thus we need to mark ourselves as beeing in a transaction
	 * manually.
	 */
	current_set_flags_nested(&tp->t_pflags, PF_FSTRANS);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	isize = xfs_new_eof(ip, ioend->io_offset + ioend->io_size);
	if (!isize) {
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
		xfs_trans_cancel(tp, 0);
		return 0;
	}

	trace_xfs_setfilesize(ip, ioend->io_offset, ioend->io_size);

	ip->i_d.di_size = isize;
	xfs_trans_ijoin(tp, ip, XFS_ILOCK_EXCL);
	xfs_trans_log_inode(tp, ip, XFS_ILOG_CORE);

	return xfs_trans_commit(tp, 0);
}

/*
 * Schedule IO completion handling on the final put of an ioend.
 *
 * If there is no work to do we might as well call it a day and free the
 * ioend right now.
 */
STATIC void
xfs_finish_ioend(
	struct xfs_ioend	*ioend)
{
	if (atomic_dec_and_test(&ioend->io_remaining)) {
		struct xfs_mount	*mp = XFS_I(ioend->io_inode)->i_mount;

		if (ioend->io_type == IO_UNWRITTEN)
			queue_work(mp->m_unwritten_workqueue, &ioend->io_work);
		else if (ioend->io_append_trans)
			queue_work(mp->m_data_workqueue, &ioend->io_work);
		else
			xfs_destroy_ioend(ioend);
	}
}

/*
 * IO write completion.
 */
STATIC void
xfs_end_io(
	struct work_struct *work)
{
	xfs_ioend_t	*ioend = container_of(work, xfs_ioend_t, io_work);
	struct xfs_inode *ip = XFS_I(ioend->io_inode);
	int		error = 0;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount)) {
		ioend->io_error = -EIO;
		goto done;
	}
	if (ioend->io_error)
		goto done;

	/*
	 * For unwritten extents we need to issue transactions to convert a
	 * range to normal written extens after the data I/O has finished.
	 */
	if (ioend->io_type == IO_UNWRITTEN) {
		/*
		 * For buffered I/O we never preallocate a transaction when
		 * doing the unwritten extent conversion, but for direct I/O
		 * we do not know if we are converting an unwritten extent
		 * or not at the point where we preallocate the transaction.
		 */
		if (ioend->io_append_trans) {
			ASSERT(ioend->io_isdirect);

			current_set_flags_nested(
				&ioend->io_append_trans->t_pflags, PF_FSTRANS);
			xfs_trans_cancel(ioend->io_append_trans, 0);
		}

		error = xfs_iomap_write_unwritten(ip, ioend->io_offset,
						 ioend->io_size);
		if (error) {
			ioend->io_error = -error;
			goto done;
		}
	} else if (ioend->io_append_trans) {
		error = xfs_setfilesize(ioend);
		if (error)
			ioend->io_error = -error;
	} else {
		ASSERT(!xfs_ioend_is_append(ioend));
	}

done:
	xfs_destroy_ioend(ioend);
}

/*
 * Call IO completion handling in caller context on the final put of an ioend.
 */
STATIC void
xfs_finish_ioend_sync(
	struct xfs_ioend	*ioend)
{
	if (atomic_dec_and_test(&ioend->io_remaining))
		xfs_end_io(&ioend->io_work);
}

/*
 * Allocate and initialise an IO completion structure.
 * We need to track unwritten extent write completion here initially.
 * We'll need to extend this for updating the ondisk inode size later
 * (vs. incore size).
 */
STATIC xfs_ioend_t *
xfs_alloc_ioend(
	struct inode		*inode,
	unsigned int		type)
{
	xfs_ioend_t		*ioend;

	ioend = mempool_alloc(xfs_ioend_pool, GFP_NOFS);

	/*
	 * Set the count to 1 initially, which will prevent an I/O
	 * completion callback from happening before we have started
	 * all the I/O from calling the completion routine too early.
	 */
	atomic_set(&ioend->io_remaining, 1);
	ioend->io_isasync = 0;
	ioend->io_isdirect = 0;
	ioend->io_error = 0;
	ioend->io_list = NULL;
	ioend->io_type = type;
	ioend->io_inode = inode;
	ioend->io_buffer_head = NULL;
	ioend->io_buffer_tail = NULL;
	ioend->io_offset = 0;
	ioend->io_size = 0;
	ioend->io_iocb = NULL;
	ioend->io_result = 0;
	ioend->io_append_trans = NULL;

	INIT_WORK(&ioend->io_work, xfs_end_io);
	return ioend;
}

STATIC int
xfs_map_blocks(
	struct inode		*inode,
	loff_t			offset,
	struct xfs_bmbt_irec	*imap,
	int			type,
	int			nonblocking)
{
	struct xfs_inode	*ip = XFS_I(inode);
	struct xfs_mount	*mp = ip->i_mount;
	ssize_t			count = 1 << inode->i_blkbits;
	xfs_fileoff_t		offset_fsb, end_fsb;
	int			error = 0;
	int			bmapi_flags = XFS_BMAPI_ENTIRE;
	int			nimaps = 1;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -XFS_ERROR(EIO);

	if (type == IO_UNWRITTEN)
		bmapi_flags |= XFS_BMAPI_IGSTATE;

	if (!xfs_ilock_nowait(ip, XFS_ILOCK_SHARED)) {
		if (nonblocking)
			return -XFS_ERROR(EAGAIN);
		xfs_ilock(ip, XFS_ILOCK_SHARED);
	}

	ASSERT(ip->i_d.di_format != XFS_DINODE_FMT_BTREE ||
	       (ip->i_df.if_flags & XFS_IFEXTENTS));
	ASSERT(offset <= mp->m_maxioffset);

	if (offset + count > mp->m_maxioffset)
		count = mp->m_maxioffset - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + count);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);
	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb,
				imap, &nimaps, bmapi_flags);
	xfs_iunlock(ip, XFS_ILOCK_SHARED);

	if (error)
		return -XFS_ERROR(error);

	if (type == IO_DELALLOC &&
	    (!nimaps || isnullstartblock(imap->br_startblock))) {
		error = xfs_iomap_write_allocate(ip, offset, count, imap);
		if (!error)
			trace_xfs_map_blocks_alloc(ip, offset, count, type, imap);
		return -XFS_ERROR(error);
	}

#ifdef DEBUG
	if (type == IO_UNWRITTEN) {
		ASSERT(nimaps);
		ASSERT(imap->br_startblock != HOLESTARTBLOCK);
		ASSERT(imap->br_startblock != DELAYSTARTBLOCK);
	}
#endif
	if (nimaps)
		trace_xfs_map_blocks_found(ip, offset, count, type, imap);
	return 0;
}

STATIC int
xfs_imap_valid(
	struct inode		*inode,
	struct xfs_bmbt_irec	*imap,
	xfs_off_t		offset)
{
	offset >>= inode->i_blkbits;

	return offset >= imap->br_startoff &&
		offset < imap->br_startoff + imap->br_blockcount;
}

/*
 * BIO completion handler for buffered IO.
 */
STATIC void
xfs_end_bio(
	struct bio		*bio,
	int			error)
{
	xfs_ioend_t		*ioend = bio->bi_private;

	ASSERT(atomic_read(&bio->bi_cnt) >= 1);
	ioend->io_error = test_bit(BIO_UPTODATE, &bio->bi_flags) ? 0 : error;

	/* Toss bio and pass work off to an xfsdatad thread */
	bio->bi_private = NULL;
	bio->bi_end_io = NULL;
	bio_put(bio);

	xfs_finish_ioend(ioend);
}

STATIC void
xfs_submit_ioend_bio(
	struct writeback_control *wbc,
	xfs_ioend_t		*ioend,
	struct bio		*bio)
{
	atomic_inc(&ioend->io_remaining);
	bio->bi_private = ioend;
	bio->bi_end_io = xfs_end_bio;
	submit_bio(wbc->sync_mode == WB_SYNC_ALL ? WRITE_SYNC : WRITE, bio);
}

STATIC struct bio *
xfs_alloc_ioend_bio(
	struct buffer_head	*bh)
{
	int			nvecs = bio_get_nr_vecs(bh->b_bdev);
	struct bio		*bio = bio_alloc(GFP_NOIO, nvecs);

	ASSERT(bio->bi_private == NULL);
	bio->bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_bdev = bh->b_bdev;
	return bio;
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
	int			clear_dirty,
	int			buffers)
{
	ASSERT(PageLocked(page));
	ASSERT(!PageWriteback(page));
	if (clear_dirty)
		clear_page_dirty_for_io(page);
	set_page_writeback(page);
	unlock_page(page);
	/* If no buffers on the page are to be written, finish it here */
	if (!buffers)
		end_page_writeback(page);
}

static inline int bio_add_buffer(struct bio *bio, struct buffer_head *bh)
{
	return bio_add_page(bio, bh->b_page, bh->b_size, bh_offset(bh));
}

/*
 * Submit all of the bios for all of the ioends we have saved up, covering the
 * initial writepage page and also any probed pages.
 *
 * Because we may have multiple ioends spanning a page, we need to start
 * writeback on all the buffers before we submit them for I/O. If we mark the
 * buffers as we got, then we can end up with a page that only has buffers
 * marked async write and I/O complete on can occur before we mark the other
 * buffers async write.
 *
 * The end result of this is that we trip a bug in end_page_writeback() because
 * we call it twice for the one page as the code in end_buffer_async_write()
 * assumes that all buffers on the page are started at the same time.
 *
 * The fix is two passes across the ioend list - one to start writeback on the
 * buffer_heads, and then submit them for I/O on the second pass.
 */
STATIC void
xfs_submit_ioend(
	struct writeback_control *wbc,
	xfs_ioend_t		*ioend)
{
	xfs_ioend_t		*head = ioend;
	xfs_ioend_t		*next;
	struct buffer_head	*bh;
	struct bio		*bio;
	sector_t		lastblock = 0;

	/* Pass 1 - start writeback */
	do {
		next = ioend->io_list;
		for (bh = ioend->io_buffer_head; bh; bh = bh->b_private)
			xfs_start_buffer_writeback(bh);
	} while ((ioend = next) != NULL);

	/* Pass 2 - submit I/O */
	ioend = head;
	do {
		next = ioend->io_list;
		bio = NULL;

		for (bh = ioend->io_buffer_head; bh; bh = bh->b_private) {

			if (!bio) {
 retry:
				bio = xfs_alloc_ioend_bio(bh);
			} else if (bh->b_blocknr != lastblock + 1) {
				xfs_submit_ioend_bio(wbc, ioend, bio);
				goto retry;
			}

			if (bio_add_buffer(bio, bh) != bh->b_size) {
				xfs_submit_ioend_bio(wbc, ioend, bio);
				goto retry;
			}

			lastblock = bh->b_blocknr;
		}
		if (bio)
			xfs_submit_ioend_bio(wbc, ioend, bio);
		xfs_finish_ioend(ioend);
	} while ((ioend = next) != NULL);
}

/*
 * Cancel submission of all buffer_heads so far in this endio.
 * Toss the endio too.  Only ever called for the initial page
 * in a writepage request, so only ever one page.
 */
STATIC void
xfs_cancel_ioend(
	xfs_ioend_t		*ioend)
{
	xfs_ioend_t		*next;
	struct buffer_head	*bh, *next_bh;

	do {
		next = ioend->io_list;
		bh = ioend->io_buffer_head;
		do {
			next_bh = bh->b_private;
			clear_buffer_async_write(bh);
			unlock_buffer(bh);
		} while ((bh = next_bh) != NULL);

		mempool_free(ioend, xfs_ioend_pool);
	} while ((ioend = next) != NULL);
}

/*
 * Test to see if we've been building up a completion structure for
 * earlier buffers -- if so, we try to append to this ioend if we
 * can, otherwise we finish off any current ioend and start another.
 * Return true if we've finished the given ioend.
 */
STATIC void
xfs_add_to_ioend(
	struct inode		*inode,
	struct buffer_head	*bh,
	xfs_off_t		offset,
	unsigned int		type,
	xfs_ioend_t		**result,
	int			need_ioend)
{
	xfs_ioend_t		*ioend = *result;

	if (!ioend || need_ioend || type != ioend->io_type) {
		xfs_ioend_t	*previous = *result;

		ioend = xfs_alloc_ioend(inode, type);
		ioend->io_offset = offset;
		ioend->io_buffer_head = bh;
		ioend->io_buffer_tail = bh;
		if (previous)
			previous->io_list = ioend;
		*result = ioend;
	} else {
		ioend->io_buffer_tail->b_private = bh;
		ioend->io_buffer_tail = bh;
	}

	bh->b_private = NULL;
	ioend->io_size += bh->b_size;
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
 * Test if a given page is suitable for writing as part of an unwritten
 * or delayed allocate extent.
 */
STATIC int
xfs_check_page_type(
	struct page		*page,
	unsigned int		type)
{
	if (PageWriteback(page))
		return 0;

	if (page->mapping && page_has_buffers(page)) {
		struct buffer_head	*bh, *head;
		int			acceptable = 0;

		bh = head = page_buffers(page);
		do {
			if (buffer_unwritten(bh))
				acceptable += (type == IO_UNWRITTEN);
			else if (buffer_delay(bh))
				acceptable += (type == IO_DELALLOC);
			else if (buffer_dirty(bh) && buffer_mapped(bh))
				acceptable += (type == IO_OVERWRITE);
			else
				break;
		} while ((bh = bh->b_this_page) != head);

		if (acceptable)
			return 1;
	}

	return 0;
}

/*
 * Allocate & map buffers for page given the extent map. Write it out.
 * except for the original page of a writepage, this is called on
 * delalloc/unwritten pages only, for the original page it is possible
 * that the page has no mapping at all.
 */
STATIC int
xfs_convert_page(
	struct inode		*inode,
	struct page		*page,
	loff_t			tindex,
	struct xfs_bmbt_irec	*imap,
	xfs_ioend_t		**ioendp,
	struct writeback_control *wbc)
{
	struct buffer_head	*bh, *head;
	xfs_off_t		end_offset;
	unsigned long		p_offset;
	unsigned int		type;
	int			len, page_dirty;
	int			count = 0, done = 0, uptodate = 1;
 	xfs_off_t		offset = page_offset(page);

	if (page->index != tindex)
		goto fail;
	if (!trylock_page(page))
		goto fail;
	if (PageWriteback(page))
		goto fail_unlock_page;
	if (page->mapping != inode->i_mapping)
		goto fail_unlock_page;
	if (!xfs_check_page_type(page, (*ioendp)->io_type))
		goto fail_unlock_page;

	/*
	 * page_dirty is initially a count of buffers on the page before
	 * EOF and is decremented as we move each into a cleanable state.
	 *
	 * Derivation:
	 *
	 * End offset is the highest offset that this page should represent.
	 * If we are on the last page, (end_offset & (PAGE_CACHE_SIZE - 1))
	 * will evaluate non-zero and be less than PAGE_CACHE_SIZE and
	 * hence give us the correct page_dirty count. On any other page,
	 * it will be zero and in that case we need page_dirty to be the
	 * count of buffers on the page.
	 */
	end_offset = min_t(unsigned long long,
			(xfs_off_t)(page->index + 1) << PAGE_CACHE_SHIFT,
			i_size_read(inode));

	len = 1 << inode->i_blkbits;
	p_offset = min_t(unsigned long, end_offset & (PAGE_CACHE_SIZE - 1),
					PAGE_CACHE_SIZE);
	p_offset = p_offset ? roundup(p_offset, len) : PAGE_CACHE_SIZE;
	page_dirty = p_offset / len;

	bh = head = page_buffers(page);
	do {
		if (offset >= end_offset)
			break;
		if (!buffer_uptodate(bh))
			uptodate = 0;
		if (!(PageUptodate(page) || buffer_uptodate(bh))) {
			done = 1;
			continue;
		}

		if (buffer_unwritten(bh) || buffer_delay(bh) ||
		    buffer_mapped(bh)) {
			if (buffer_unwritten(bh))
				type = IO_UNWRITTEN;
			else if (buffer_delay(bh))
				type = IO_DELALLOC;
			else
				type = IO_OVERWRITE;

			if (!xfs_imap_valid(inode, imap, offset)) {
				done = 1;
				continue;
			}

			lock_buffer(bh);
			if (type != IO_OVERWRITE)
				xfs_map_at_offset(inode, bh, imap, offset);
			xfs_add_to_ioend(inode, bh, offset, type,
					 ioendp, done);

			page_dirty--;
			count++;
		} else {
			done = 1;
		}
	} while (offset += len, (bh = bh->b_this_page) != head);

	if (uptodate && bh == head)
		SetPageUptodate(page);

	if (count) {
		if (--wbc->nr_to_write <= 0 &&
		    wbc->sync_mode == WB_SYNC_NONE)
			done = 1;
	}
	xfs_start_page_writeback(page, !page_dirty, count);

	return done;
 fail_unlock_page:
	unlock_page(page);
 fail:
	return 1;
}

/*
 * Convert & write out a cluster of pages in the same extent as defined
 * by mp and following the start page.
 */
STATIC void
xfs_cluster_write(
	struct inode		*inode,
	pgoff_t			tindex,
	struct xfs_bmbt_irec	*imap,
	xfs_ioend_t		**ioendp,
	struct writeback_control *wbc,
	pgoff_t			tlast)
{
	struct pagevec		pvec;
	int			done = 0, i;

	pagevec_init(&pvec, 0);
	while (!done && tindex <= tlast) {
		unsigned len = min_t(pgoff_t, PAGEVEC_SIZE, tlast - tindex + 1);

		if (!pagevec_lookup(&pvec, inode->i_mapping, tindex, len))
			break;

		for (i = 0; i < pagevec_count(&pvec); i++) {
			done = xfs_convert_page(inode, pvec.pages[i], tindex++,
					imap, ioendp, wbc);
			if (done)
				break;
		}

		pagevec_release(&pvec);
		cond_resched();
	}
}

STATIC void
xfs_vm_invalidatepage(
	struct page		*page,
	unsigned long		offset)
{
	trace_xfs_invalidatepage(page->mapping->host, page, offset);
	block_invalidatepage(page, offset);
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

	if (!xfs_check_page_type(page, IO_DELALLOC))
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
		offset += 1 << inode->i_blkbits;

	} while ((bh = bh->b_this_page) != head);

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
out_invalidate:
	xfs_vm_invalidatepage(page, 0);
	return;
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
xfs_vm_writepage(
	struct page		*page,
	struct writeback_control *wbc)
{
	struct inode		*inode = page->mapping->host;
	struct buffer_head	*bh, *head;
	struct xfs_bmbt_irec	imap;
	xfs_ioend_t		*ioend = NULL, *iohead = NULL;
	loff_t			offset;
	unsigned int		type;
	__uint64_t              end_offset;
	pgoff_t                 end_index, last_index;
	ssize_t			len;
	int			err, imap_valid = 0, uptodate = 1;
	int			count = 0;
	int			nonblocking = 0;

	trace_xfs_writepage(inode, page, 0);

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
	if (WARN_ON(current->flags & PF_FSTRANS))
		goto redirty;

	/* Is this page beyond the end of the file? */
	offset = i_size_read(inode);
	end_index = offset >> PAGE_CACHE_SHIFT;
	last_index = (offset - 1) >> PAGE_CACHE_SHIFT;
	if (page->index >= end_index) {
		if ((page->index >= end_index + 1) ||
		    !(i_size_read(inode) & (PAGE_CACHE_SIZE - 1))) {
			unlock_page(page);
			return 0;
		}
	}

	end_offset = min_t(unsigned long long,
			(xfs_off_t)(page->index + 1) << PAGE_CACHE_SHIFT,
			offset);
	len = 1 << inode->i_blkbits;

	bh = head = page_buffers(page);
	offset = page_offset(page);
	type = IO_OVERWRITE;

	if (wbc->sync_mode == WB_SYNC_NONE)
		nonblocking = 1;

	do {
		int new_ioend = 0;

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
			imap_valid = 0;
			continue;
		}

		if (buffer_unwritten(bh)) {
			if (type != IO_UNWRITTEN) {
				type = IO_UNWRITTEN;
				imap_valid = 0;
			}
		} else if (buffer_delay(bh)) {
			if (type != IO_DELALLOC) {
				type = IO_DELALLOC;
				imap_valid = 0;
			}
		} else if (buffer_uptodate(bh)) {
			if (type != IO_OVERWRITE) {
				type = IO_OVERWRITE;
				imap_valid = 0;
			}
		} else {
			if (PageUptodate(page)) {
				ASSERT(buffer_mapped(bh));
				imap_valid = 0;
			}
			continue;
		}

		if (imap_valid)
			imap_valid = xfs_imap_valid(inode, &imap, offset);
		if (!imap_valid) {
			/*
			 * If we didn't have a valid mapping then we need to
			 * put the new mapping into a separate ioend structure.
			 * This ensures non-contiguous extents always have
			 * separate ioends, which is particularly important
			 * for unwritten extent conversion at I/O completion
			 * time.
			 */
			new_ioend = 1;
			err = xfs_map_blocks(inode, offset, &imap, type,
					     nonblocking);
			if (err)
				goto error;
			imap_valid = xfs_imap_valid(inode, &imap, offset);
		}
		if (imap_valid) {
			lock_buffer(bh);
			if (type != IO_OVERWRITE)
				xfs_map_at_offset(inode, bh, &imap, offset);
			xfs_add_to_ioend(inode, bh, offset, type, &ioend,
					 new_ioend);
			count++;
		}

		if (!iohead)
			iohead = ioend;

	} while (offset += len, ((bh = bh->b_this_page) != head));

	if (uptodate && bh == head)
		SetPageUptodate(page);

	xfs_start_page_writeback(page, 1, count);

	if (ioend && imap_valid) {
		xfs_off_t		end_index;

		end_index = imap.br_startoff + imap.br_blockcount;

		/* to bytes */
		end_index <<= inode->i_blkbits;

		/* to pages */
		end_index = (end_index - 1) >> PAGE_CACHE_SHIFT;

		/* check against file size */
		if (end_index > last_index)
			end_index = last_index;

		xfs_cluster_write(inode, page->index + 1, &imap, &ioend,
				  wbc, end_index);
	}

	if (iohead) {
		/*
		 * Reserve log space if we might write beyond the on-disk
		 * inode size.
		 */
		if (ioend->io_type != IO_UNWRITTEN &&
		    xfs_ioend_is_append(ioend)) {
			err = xfs_setfilesize_trans_alloc(ioend);
			if (err)
				goto error;
		}

		xfs_submit_ioend(wbc, iohead);
	}

	return 0;

error:
	if (iohead)
		xfs_cancel_ioend(iohead);

	if (err == -EAGAIN)
		goto redirty;

	xfs_aops_discard_page(page);
	ClearPageUptodate(page);
	unlock_page(page);
	return err;

redirty:
	redirty_page_for_writepage(wbc, page);
	unlock_page(page);
	return 0;
}

STATIC int
xfs_vm_writepages(
	struct address_space	*mapping,
	struct writeback_control *wbc)
{
	xfs_iflags_clear(XFS_I(mapping->host), XFS_ITRUNCATED);
	return generic_writepages(mapping, wbc);
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

	trace_xfs_releasepage(page->mapping->host, page, 0);

	xfs_count_page_state(page, &delalloc, &unwritten);

	if (WARN_ON(delalloc))
		return 0;
	if (WARN_ON(unwritten))
		return 0;

	return try_to_free_buffers(page);
}

STATIC int
__xfs_get_blocks(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create,
	int			direct)
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
	int			new = 0;

	if (XFS_FORCED_SHUTDOWN(mp))
		return -XFS_ERROR(EIO);

	offset = (xfs_off_t)iblock << inode->i_blkbits;
	ASSERT(bh_result->b_size >= (1 << inode->i_blkbits));
	size = bh_result->b_size;

	if (!create && direct && offset >= i_size_read(inode))
		return 0;

	/*
	 * Direct I/O is usually done on preallocated files, so try getting
	 * a block mapping without an exclusive lock first.  For buffered
	 * writes we already have the exclusive iolock anyway, so avoiding
	 * a lock roundtrip here by taking the ilock exclusive from the
	 * beginning is a useful micro optimization.
	 */
	if (create && !direct) {
		lockmode = XFS_ILOCK_EXCL;
		xfs_ilock(ip, lockmode);
	} else {
		lockmode = xfs_ilock_map_shared(ip);
	}

	ASSERT(offset <= mp->m_maxioffset);
	if (offset + size > mp->m_maxioffset)
		size = mp->m_maxioffset - offset;
	end_fsb = XFS_B_TO_FSB(mp, (xfs_ufsize_t)offset + size);
	offset_fsb = XFS_B_TO_FSBT(mp, offset);

	error = xfs_bmapi_read(ip, offset_fsb, end_fsb - offset_fsb,
				&imap, &nimaps, XFS_BMAPI_ENTIRE);
	if (error)
		goto out_unlock;

	if (create &&
	    (!nimaps ||
	     (imap.br_startblock == HOLESTARTBLOCK ||
	      imap.br_startblock == DELAYSTARTBLOCK))) {
		if (direct) {
			/*
			 * Drop the ilock in preparation for starting the block
			 * allocation transaction.  It will be retaken
			 * exclusively inside xfs_iomap_write_direct for the
			 * actual allocation.
			 */
			xfs_iunlock(ip, lockmode);
			error = xfs_iomap_write_direct(ip, offset, size,
						       &imap, nimaps);
			if (error)
				return -error;
		} else {
			/*
			 * Delalloc reservations do not require a transaction,
			 * we can go on without dropping the lock here.
			 */
			error = xfs_iomap_write_delay(ip, offset, size, &imap);
			if (error)
				goto out_unlock;

			xfs_iunlock(ip, lockmode);
		}

		trace_xfs_get_blocks_alloc(ip, offset, size, 0, &imap);
	} else if (nimaps) {
		trace_xfs_get_blocks_found(ip, offset, size, 0, &imap);
		xfs_iunlock(ip, lockmode);
	} else {
		trace_xfs_get_blocks_notfound(ip, offset, size);
		goto out_unlock;
	}

	if (imap.br_startblock != HOLESTARTBLOCK &&
	    imap.br_startblock != DELAYSTARTBLOCK) {
		/*
		 * For unwritten extents do not report a disk address on
		 * the read case (treat as if we're reading into a hole).
		 */
		if (create || !ISUNWRITTEN(&imap))
			xfs_map_buffer(inode, bh_result, &imap, offset);
		if (create && ISUNWRITTEN(&imap)) {
			if (direct)
				bh_result->b_private = inode;
			set_buffer_unwritten(bh_result);
		}
	}

	/*
	 * If this is a realtime file, data may be on a different device.
	 * to that pointed to from the buffer_head b_bdev currently.
	 */
	bh_result->b_bdev = xfs_find_bdev_for_inode(inode);

	/*
	 * If we previously allocated a block out beyond eof and we are now
	 * coming back to use it then we will need to flag it as new even if it
	 * has a disk address.
	 *
	 * With sub-block writes into unwritten extents we also need to mark
	 * the buffer as new so that the unwritten parts of the buffer gets
	 * correctly zeroed.
	 */
	if (create &&
	    ((!buffer_mapped(bh_result) && !buffer_uptodate(bh_result)) ||
	     (offset >= i_size_read(inode)) ||
	     (new || ISUNWRITTEN(&imap))))
		set_buffer_new(bh_result);

	if (imap.br_startblock == DELAYSTARTBLOCK) {
		BUG_ON(direct);
		if (create) {
			set_buffer_uptodate(bh_result);
			set_buffer_mapped(bh_result);
			set_buffer_delay(bh_result);
		}
	}

	/*
	 * If this is O_DIRECT or the mpage code calling tell them how large
	 * the mapping is, so that we can avoid repeated get_blocks calls.
	 */
	if (direct || size > (1 << inode->i_blkbits)) {
		xfs_off_t		mapping_size;

		mapping_size = imap.br_startoff + imap.br_blockcount - iblock;
		mapping_size <<= inode->i_blkbits;

		ASSERT(mapping_size > 0);
		if (mapping_size > size)
			mapping_size = size;
		if (mapping_size > LONG_MAX)
			mapping_size = LONG_MAX;

		bh_result->b_size = mapping_size;
	}

	return 0;

out_unlock:
	xfs_iunlock(ip, lockmode);
	return -error;
}

int
xfs_get_blocks(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create)
{
	return __xfs_get_blocks(inode, iblock, bh_result, create, 0);
}

STATIC int
xfs_get_blocks_direct(
	struct inode		*inode,
	sector_t		iblock,
	struct buffer_head	*bh_result,
	int			create)
{
	return __xfs_get_blocks(inode, iblock, bh_result, create, 1);
}

/*
 * Complete a direct I/O write request.
 *
 * If the private argument is non-NULL __xfs_get_blocks signals us that we
 * need to issue a transaction to convert the range from unwritten to written
 * extents.  In case this is regular synchronous I/O we just call xfs_end_io
 * to do this and we are done.  But in case this was a successful AIO
 * request this handler is called from interrupt context, from which we
 * can't start transactions.  In that case offload the I/O completion to
 * the workqueues we also use for buffered I/O completion.
 */
STATIC void
xfs_end_io_direct_write(
	struct kiocb		*iocb,
	loff_t			offset,
	ssize_t			size,
	void			*private,
	int			ret,
	bool			is_async)
{
	struct xfs_ioend	*ioend = iocb->private;

	/*
	 * While the generic direct I/O code updates the inode size, it does
	 * so only after the end_io handler is called, which means our
	 * end_io handler thinks the on-disk size is outside the in-core
	 * size.  To prevent this just update it a little bit earlier here.
	 */
	if (offset + size > i_size_read(ioend->io_inode))
		i_size_write(ioend->io_inode, offset + size);

	/*
	 * blockdev_direct_IO can return an error even after the I/O
	 * completion handler was called.  Thus we need to protect
	 * against double-freeing.
	 */
	iocb->private = NULL;

	ioend->io_offset = offset;
	ioend->io_size = size;
	ioend->io_iocb = iocb;
	ioend->io_result = ret;
	if (private && size > 0)
		ioend->io_type = IO_UNWRITTEN;

	if (is_async) {
		ioend->io_isasync = 1;
		xfs_finish_ioend(ioend);
	} else {
		xfs_finish_ioend_sync(ioend);
	}
}

STATIC ssize_t
xfs_vm_direct_IO(
	int			rw,
	struct kiocb		*iocb,
	const struct iovec	*iov,
	loff_t			offset,
	unsigned long		nr_segs)
{
	struct inode		*inode = iocb->ki_filp->f_mapping->host;
	struct block_device	*bdev = xfs_find_bdev_for_inode(inode);
	struct xfs_ioend	*ioend = NULL;
	ssize_t			ret;

	if (rw & WRITE) {
		size_t size = iov_length(iov, nr_segs);

		/*
		 * We need to preallocate a transaction for a size update
		 * here.  In the case that this write both updates the size
		 * and converts at least on unwritten extent we will cancel
		 * the still clean transaction after the I/O has finished.
		 */
		iocb->private = ioend = xfs_alloc_ioend(inode, IO_DIRECT);
		if (offset + size > XFS_I(inode)->i_d.di_size) {
			ret = xfs_setfilesize_trans_alloc(ioend);
			if (ret)
				goto out_destroy_ioend;
			ioend->io_isdirect = 1;
		}

		ret = __blockdev_direct_IO(rw, iocb, inode, bdev, iov,
					    offset, nr_segs,
					    xfs_get_blocks_direct,
					    xfs_end_io_direct_write, NULL, 0);
		if (ret != -EIOCBQUEUED && iocb->private)
			goto out_trans_cancel;
	} else {
		ret = __blockdev_direct_IO(rw, iocb, inode, bdev, iov,
					    offset, nr_segs,
					    xfs_get_blocks_direct,
					    NULL, NULL, 0);
	}

	return ret;

out_trans_cancel:
	if (ioend->io_append_trans) {
		current_set_flags_nested(&ioend->io_append_trans->t_pflags,
					 PF_FSTRANS);
		xfs_trans_cancel(ioend->io_append_trans, 0);
	}
out_destroy_ioend:
	xfs_destroy_ioend(ioend);
	return ret;
}

STATIC void
xfs_vm_write_failed(
	struct address_space	*mapping,
	loff_t			to)
{
	struct inode		*inode = mapping->host;

	if (to > inode->i_size) {
		/*
		 * Punch out the delalloc blocks we have already allocated.
		 *
		 * Don't bother with xfs_setattr given that nothing can have
		 * made it to disk yet as the page is still locked at this
		 * point.
		 */
		struct xfs_inode	*ip = XFS_I(inode);
		xfs_fileoff_t		start_fsb;
		xfs_fileoff_t		end_fsb;
		int			error;

		truncate_pagecache(inode, to, inode->i_size);

		/*
		 * Check if there are any blocks that are outside of i_size
		 * that need to be trimmed back.
		 */
		start_fsb = XFS_B_TO_FSB(ip->i_mount, inode->i_size);
		end_fsb = XFS_B_TO_FSB(ip->i_mount, to);
		if (end_fsb <= start_fsb)
			return;

		xfs_ilock(ip, XFS_ILOCK_EXCL);
		error = xfs_bmap_punch_delalloc_range(ip, start_fsb,
							end_fsb - start_fsb);
		if (error) {
			/* something screwed, just bail */
			if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
				xfs_alert(ip->i_mount,
			"xfs_vm_write_failed: unable to clean up ino %lld",
						ip->i_ino);
			}
		}
		xfs_iunlock(ip, XFS_ILOCK_EXCL);
	}
}

STATIC int
xfs_vm_write_begin(
	struct file		*file,
	struct address_space	*mapping,
	loff_t			pos,
	unsigned		len,
	unsigned		flags,
	struct page		**pagep,
	void			**fsdata)
{
	int			ret;

	ret = block_write_begin(mapping, pos, len, flags | AOP_FLAG_NOFS,
				pagep, xfs_get_blocks);
	if (unlikely(ret))
		xfs_vm_write_failed(mapping, pos + len);
	return ret;
}

STATIC int
xfs_vm_write_end(
	struct file		*file,
	struct address_space	*mapping,
	loff_t			pos,
	unsigned		len,
	unsigned		copied,
	struct page		*page,
	void			*fsdata)
{
	int			ret;

	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (unlikely(ret < len))
		xfs_vm_write_failed(mapping, pos + len);
	return ret;
}

STATIC sector_t
xfs_vm_bmap(
	struct address_space	*mapping,
	sector_t		block)
{
	struct inode		*inode = (struct inode *)mapping->host;
	struct xfs_inode	*ip = XFS_I(inode);

	trace_xfs_vm_bmap(XFS_I(inode));
	xfs_ilock(ip, XFS_IOLOCK_SHARED);
	xfs_flush_pages(ip, (xfs_off_t)0, -1, 0, FI_REMAPF);
	xfs_iunlock(ip, XFS_IOLOCK_SHARED);
	return generic_block_bmap(mapping, block, xfs_get_blocks);
}

STATIC int
xfs_vm_readpage(
	struct file		*unused,
	struct page		*page)
{
	return mpage_readpage(page, xfs_get_blocks);
}

STATIC int
xfs_vm_readpages(
	struct file		*unused,
	struct address_space	*mapping,
	struct list_head	*pages,
	unsigned		nr_pages)
{
	return mpage_readpages(mapping, pages, nr_pages, xfs_get_blocks);
}

const struct address_space_operations xfs_address_space_operations = {
	.readpage		= xfs_vm_readpage,
	.readpages		= xfs_vm_readpages,
	.writepage		= xfs_vm_writepage,
	.writepages		= xfs_vm_writepages,
	.releasepage		= xfs_vm_releasepage,
	.invalidatepage		= xfs_vm_invalidatepage,
	.write_begin		= xfs_vm_write_begin,
	.write_end		= xfs_vm_write_end,
	.bmap			= xfs_vm_bmap,
	.direct_IO		= xfs_vm_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate  = block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};
