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

/*
 * Types of I/O for bmap clustering and I/O completion tracking.
 */
enum {
	IO_READ,	/* mapping for a read */
	IO_DELAY,	/* mapping covers delalloc region */
	IO_UNWRITTEN,	/* mapping covers allocated but uninitialized data */
	IO_NEW		/* just allocated */
};

/*
 * Prime number of hash buckets since address is used as the key.
 */
#define NVSYNC		37
#define to_ioend_wq(v)	(&xfs_ioend_wq[((unsigned long)v) % NVSYNC])
static wait_queue_head_t xfs_ioend_wq[NVSYNC];

void __init
xfs_ioend_init(void)
{
	int i;

	for (i = 0; i < NVSYNC; i++)
		init_waitqueue_head(&xfs_ioend_wq[i]);
}

void
xfs_ioend_wait(
	xfs_inode_t	*ip)
{
	wait_queue_head_t *wq = to_ioend_wq(ip);

	wait_event(*wq, (atomic_read(&ip->i_iocount) == 0));
}

STATIC void
xfs_ioend_wake(
	xfs_inode_t	*ip)
{
	if (atomic_dec_and_test(&ip->i_iocount))
		wake_up(to_ioend_wq(ip));
}

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
	struct xfs_inode	*ip = XFS_I(ioend->io_inode);

	for (bh = ioend->io_buffer_head; bh; bh = next) {
		next = bh->b_private;
		bh->b_end_io(bh, !ioend->io_error);
	}

	/*
	 * Volume managers supporting multiple paths can send back ENODEV
	 * when the final path disappears.  In this case continuing to fill
	 * the page cache with dirty data which cannot be written out is
	 * evil, so prevent that.
	 */
	if (unlikely(ioend->io_error == -ENODEV)) {
		xfs_do_force_shutdown(ip->i_mount, SHUTDOWN_DEVICE_REQ,
				      __FILE__, __LINE__);
	}

	xfs_ioend_wake(ip);
	mempool_free(ioend, xfs_ioend_pool);
}

/*
 * If the end of the current ioend is beyond the current EOF,
 * return the new EOF value, otherwise zero.
 */
STATIC xfs_fsize_t
xfs_ioend_new_eof(
	xfs_ioend_t		*ioend)
{
	xfs_inode_t		*ip = XFS_I(ioend->io_inode);
	xfs_fsize_t		isize;
	xfs_fsize_t		bsize;

	bsize = ioend->io_offset + ioend->io_size;
	isize = MAX(ip->i_size, ip->i_new_size);
	isize = MIN(isize, bsize);
	return isize > ip->i_d.di_size ? isize : 0;
}

/*
 * Update on-disk file size now that data has been written to disk.  The
 * current in-memory file size is i_size.  If a write is beyond eof i_new_size
 * will be the intended file size until i_size is updated.  If this write does
 * not extend all the way to the valid file size then restrict this update to
 * the end of the write.
 *
 * This function does not block as blocking on the inode lock in IO completion
 * can lead to IO completion order dependency deadlocks.. If it can't get the
 * inode ilock it will return EAGAIN. Callers must handle this.
 */
STATIC int
xfs_setfilesize(
	xfs_ioend_t		*ioend)
{
	xfs_inode_t		*ip = XFS_I(ioend->io_inode);
	xfs_fsize_t		isize;

	ASSERT((ip->i_d.di_mode & S_IFMT) == S_IFREG);
	ASSERT(ioend->io_type != IO_READ);

	if (unlikely(ioend->io_error))
		return 0;

	if (!xfs_ilock_nowait(ip, XFS_ILOCK_EXCL))
		return EAGAIN;

	isize = xfs_ioend_new_eof(ioend);
	if (isize) {
		ip->i_d.di_size = isize;
		xfs_mark_inode_dirty(ip);
	}

	xfs_iunlock(ip, XFS_ILOCK_EXCL);
	return 0;
}

/*
 * Schedule IO completion handling on the final put of an ioend.
 */
STATIC void
xfs_finish_ioend(
	struct xfs_ioend	*ioend)
{
	if (atomic_dec_and_test(&ioend->io_remaining)) {
		if (ioend->io_type == IO_UNWRITTEN)
			queue_work(xfsconvertd_workqueue, &ioend->io_work);
		else
			queue_work(xfsdatad_workqueue, &ioend->io_work);
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

	/*
	 * For unwritten extents we need to issue transactions to convert a
	 * range to normal written extens after the data I/O has finished.
	 */
	if (ioend->io_type == IO_UNWRITTEN &&
	    likely(!ioend->io_error && !XFS_FORCED_SHUTDOWN(ip->i_mount))) {

		error = xfs_iomap_write_unwritten(ip, ioend->io_offset,
						 ioend->io_size);
		if (error)
			ioend->io_error = error;
	}

	/*
	 * We might have to update the on-disk file size after extending
	 * writes.
	 */
	if (ioend->io_type != IO_READ) {
		error = xfs_setfilesize(ioend);
		ASSERT(!error || error == EAGAIN);
	}

	/*
	 * If we didn't complete processing of the ioend, requeue it to the
	 * tail of the workqueue for another attempt later. Otherwise destroy
	 * it.
	 */
	if (error == EAGAIN) {
		atomic_inc(&ioend->io_remaining);
		xfs_finish_ioend(ioend);
		/* ensure we don't spin on blocked ioends */
		delay(1);
	} else {
		if (ioend->io_iocb)
			aio_complete(ioend->io_iocb, ioend->io_result, 0);
		xfs_destroy_ioend(ioend);
	}
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
	ioend->io_error = 0;
	ioend->io_list = NULL;
	ioend->io_type = type;
	ioend->io_inode = inode;
	ioend->io_buffer_head = NULL;
	ioend->io_buffer_tail = NULL;
	atomic_inc(&XFS_I(ioend->io_inode)->i_iocount);
	ioend->io_offset = 0;
	ioend->io_size = 0;
	ioend->io_iocb = NULL;
	ioend->io_result = 0;

	INIT_WORK(&ioend->io_work, xfs_end_io);
	return ioend;
}

STATIC int
xfs_map_blocks(
	struct inode		*inode,
	loff_t			offset,
	ssize_t			count,
	struct xfs_bmbt_irec	*imap,
	int			flags)
{
	int			nmaps = 1;
	int			new = 0;

	return -xfs_iomap(XFS_I(inode), offset, count, flags, imap, &nmaps, &new);
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

	/*
	 * If the I/O is beyond EOF we mark the inode dirty immediately
	 * but don't update the inode size until I/O completion.
	 */
	if (xfs_ioend_new_eof(ioend))
		xfs_mark_inode_dirty(XFS_I(ioend->io_inode));

	submit_bio(wbc->sync_mode == WB_SYNC_ALL ?
		   WRITE_SYNC_PLUG : WRITE, bio);
	ASSERT(!bio_flagged(bio, BIO_EOPNOTSUPP));
	bio_put(bio);
}

STATIC struct bio *
xfs_alloc_ioend_bio(
	struct buffer_head	*bh)
{
	struct bio		*bio;
	int			nvecs = bio_get_nr_vecs(bh->b_bdev);

	do {
		bio = bio_alloc(GFP_NOIO, nvecs);
		nvecs >>= 1;
	} while (!bio);

	ASSERT(bio->bi_private == NULL);
	bio->bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_bdev = bh->b_bdev;
	bio_get(bio);
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
		for (bh = ioend->io_buffer_head; bh; bh = bh->b_private) {
			xfs_start_buffer_writeback(bh);
		}
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

		xfs_ioend_wake(XFS_I(ioend->io_inode));
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

	lock_buffer(bh);
	xfs_map_buffer(inode, bh, imap, offset);
	bh->b_bdev = xfs_find_bdev_for_inode(inode);
	set_buffer_mapped(bh);
	clear_buffer_delay(bh);
	clear_buffer_unwritten(bh);
}

/*
 * Look for a page at index that is suitable for clustering.
 */
STATIC unsigned int
xfs_probe_page(
	struct page		*page,
	unsigned int		pg_offset)
{
	struct buffer_head	*bh, *head;
	int			ret = 0;

	if (PageWriteback(page))
		return 0;
	if (!PageDirty(page))
		return 0;
	if (!page->mapping)
		return 0;
	if (!page_has_buffers(page))
		return 0;

	bh = head = page_buffers(page);
	do {
		if (!buffer_uptodate(bh))
			break;
		if (!buffer_mapped(bh))
			break;
		ret += bh->b_size;
		if (ret >= pg_offset)
			break;
	} while ((bh = bh->b_this_page) != head);

	return ret;
}

STATIC size_t
xfs_probe_cluster(
	struct inode		*inode,
	struct page		*startpage,
	struct buffer_head	*bh,
	struct buffer_head	*head)
{
	struct pagevec		pvec;
	pgoff_t			tindex, tlast, tloff;
	size_t			total = 0;
	int			done = 0, i;

	/* First sum forwards in this page */
	do {
		if (!buffer_uptodate(bh) || !buffer_mapped(bh))
			return total;
		total += bh->b_size;
	} while ((bh = bh->b_this_page) != head);

	/* if we reached the end of the page, sum forwards in following pages */
	tlast = i_size_read(inode) >> PAGE_CACHE_SHIFT;
	tindex = startpage->index + 1;

	/* Prune this back to avoid pathological behavior */
	tloff = min(tlast, startpage->index + 64);

	pagevec_init(&pvec, 0);
	while (!done && tindex <= tloff) {
		unsigned len = min_t(pgoff_t, PAGEVEC_SIZE, tlast - tindex + 1);

		if (!pagevec_lookup(&pvec, inode->i_mapping, tindex, len))
			break;

		for (i = 0; i < pagevec_count(&pvec); i++) {
			struct page *page = pvec.pages[i];
			size_t pg_offset, pg_len = 0;

			if (tindex == tlast) {
				pg_offset =
				    i_size_read(inode) & (PAGE_CACHE_SIZE - 1);
				if (!pg_offset) {
					done = 1;
					break;
				}
			} else
				pg_offset = PAGE_CACHE_SIZE;

			if (page->index == tindex && trylock_page(page)) {
				pg_len = xfs_probe_page(page, pg_offset);
				unlock_page(page);
			}

			if (!pg_len) {
				done = 1;
				break;
			}

			total += pg_len;
			tindex++;
		}

		pagevec_release(&pvec);
		cond_resched();
	}

	return total;
}

/*
 * Test if a given page is suitable for writing as part of an unwritten
 * or delayed allocate extent.
 */
STATIC int
xfs_is_delayed_page(
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
				acceptable = (type == IO_UNWRITTEN);
			else if (buffer_delay(bh))
				acceptable = (type == IO_DELAY);
			else if (buffer_dirty(bh) && buffer_mapped(bh))
				acceptable = (type == IO_NEW);
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
	struct writeback_control *wbc,
	int			all_bh)
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
	if (!xfs_is_delayed_page(page, (*ioendp)->io_type))
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

		if (buffer_unwritten(bh) || buffer_delay(bh)) {
			if (buffer_unwritten(bh))
				type = IO_UNWRITTEN;
			else
				type = IO_DELAY;

			if (!xfs_imap_valid(inode, imap, offset)) {
				done = 1;
				continue;
			}

			ASSERT(imap->br_startblock != HOLESTARTBLOCK);
			ASSERT(imap->br_startblock != DELAYSTARTBLOCK);

			xfs_map_at_offset(inode, bh, imap, offset);
			xfs_add_to_ioend(inode, bh, offset, type,
					 ioendp, done);

			page_dirty--;
			count++;
		} else {
			type = IO_NEW;
			if (buffer_mapped(bh) && all_bh) {
				lock_buffer(bh);
				xfs_add_to_ioend(inode, bh, offset,
						type, ioendp, done);
				count++;
				page_dirty--;
			} else {
				done = 1;
			}
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
	int			all_bh,
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
					imap, ioendp, wbc, all_bh);
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
	ssize_t			len = 1 << inode->i_blkbits;

	if (!xfs_is_delayed_page(page, IO_DELAY))
		goto out_invalidate;

	if (XFS_FORCED_SHUTDOWN(ip->i_mount))
		goto out_invalidate;

	xfs_fs_cmn_err(CE_ALERT, ip->i_mount,
		"page discard on page %p, inode 0x%llx, offset %llu.",
			page, ip->i_ino, offset);

	xfs_ilock(ip, XFS_ILOCK_EXCL);
	bh = head = page_buffers(page);
	do {
		int		done;
		xfs_fileoff_t	offset_fsb;
		xfs_bmbt_irec_t	imap;
		int		nimaps = 1;
		int		error;
		xfs_fsblock_t	firstblock;
		xfs_bmap_free_t flist;

		if (!buffer_delay(bh))
			goto next_buffer;

		offset_fsb = XFS_B_TO_FSBT(ip->i_mount, offset);

		/*
		 * Map the range first and check that it is a delalloc extent
		 * before trying to unmap the range. Otherwise we will be
		 * trying to remove a real extent (which requires a
		 * transaction) or a hole, which is probably a bad idea...
		 */
		error = xfs_bmapi(NULL, ip, offset_fsb, 1,
				XFS_BMAPI_ENTIRE,  NULL, 0, &imap,
				&nimaps, NULL);

		if (error) {
			/* something screwed, just bail */
			if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
				xfs_fs_cmn_err(CE_ALERT, ip->i_mount,
				"page discard failed delalloc mapping lookup.");
			}
			break;
		}
		if (!nimaps) {
			/* nothing there */
			goto next_buffer;
		}
		if (imap.br_startblock != DELAYSTARTBLOCK) {
			/* been converted, ignore */
			goto next_buffer;
		}
		WARN_ON(imap.br_blockcount == 0);

		/*
		 * Note: while we initialise the firstblock/flist pair, they
		 * should never be used because blocks should never be
		 * allocated or freed for a delalloc extent and hence we need
		 * don't cancel or finish them after the xfs_bunmapi() call.
		 */
		xfs_bmap_init(&flist, &firstblock);
		error = xfs_bunmapi(NULL, ip, offset_fsb, 1, 0, 1, &firstblock,
					&flist, &done);

		ASSERT(!flist.xbf_count && !flist.xbf_first);
		if (error) {
			/* something screwed, just bail */
			if (!XFS_FORCED_SHUTDOWN(ip->i_mount)) {
				xfs_fs_cmn_err(CE_ALERT, ip->i_mount,
			"page discard unable to remove delalloc mapping.");
			}
			break;
		}
next_buffer:
		offset += len;

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
 *
 * If we detect that a transaction would be required to flush the page, we
 * have to check the process flags first, if we are already in a transaction
 * or disk I/O during allocations is off, we need to fail the writepage and
 * redirty the page.
 */
STATIC int
xfs_vm_writepage(
	struct page		*page,
	struct writeback_control *wbc)
{
	struct inode		*inode = page->mapping->host;
	int			delalloc, unwritten;
	struct buffer_head	*bh, *head;
	struct xfs_bmbt_irec	imap;
	xfs_ioend_t		*ioend = NULL, *iohead = NULL;
	loff_t			offset;
	unsigned int		type;
	__uint64_t              end_offset;
	pgoff_t                 end_index, last_index;
	ssize_t			size, len;
	int			flags, err, imap_valid = 0, uptodate = 1;
	int			count = 0;
	int			all_bh = 0;

	trace_xfs_writepage(inode, page, 0);

	ASSERT(page_has_buffers(page));

	/*
	 * Refuse to write the page out if we are called from reclaim context.
	 *
	 * This avoids stack overflows when called from deeply used stacks in
	 * random callers for direct reclaim or memcg reclaim.  We explicitly
	 * allow reclaim from kswapd as the stack usage there is relatively low.
	 *
	 * This should really be done by the core VM, but until that happens
	 * filesystems like XFS, btrfs and ext4 have to take care of this
	 * by themselves.
	 */
	if ((current->flags & (PF_MEMALLOC|PF_KSWAPD)) == PF_MEMALLOC)
		goto redirty;

	/*
	 * We need a transaction if there are delalloc or unwritten buffers
	 * on the page.
	 *
	 * If we need a transaction and the process flags say we are already
	 * in a transaction, or no IO is allowed then mark the page dirty
	 * again and leave the page as is.
	 */
	xfs_count_page_state(page, &delalloc, &unwritten);
	if ((current->flags & PF_FSTRANS) && (delalloc || unwritten))
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
	flags = BMAPI_READ;
	type = IO_NEW;

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
			imap_valid = 0;
			continue;
		}

		if (imap_valid)
			imap_valid = xfs_imap_valid(inode, &imap, offset);

		if (buffer_unwritten(bh) || buffer_delay(bh)) {
			int new_ioend = 0;

			/*
			 * Make sure we don't use a read-only iomap
			 */
			if (flags == BMAPI_READ)
				imap_valid = 0;

			if (buffer_unwritten(bh)) {
				type = IO_UNWRITTEN;
				flags = BMAPI_WRITE | BMAPI_IGNSTATE;
			} else if (buffer_delay(bh)) {
				type = IO_DELAY;
				flags = BMAPI_ALLOCATE;

				if (wbc->sync_mode == WB_SYNC_NONE)
					flags |= BMAPI_TRYLOCK;
			}

			if (!imap_valid) {
				/*
				 * If we didn't have a valid mapping then we
				 * need to ensure that we put the new mapping
				 * in a new ioend structure. This needs to be
				 * done to ensure that the ioends correctly
				 * reflect the block mappings at io completion
				 * for unwritten extent conversion.
				 */
				new_ioend = 1;
				err = xfs_map_blocks(inode, offset, len,
						&imap, flags);
				if (err)
					goto error;
				imap_valid = xfs_imap_valid(inode, &imap,
							    offset);
			}
			if (imap_valid) {
				xfs_map_at_offset(inode, bh, &imap, offset);
				xfs_add_to_ioend(inode, bh, offset, type,
						 &ioend, new_ioend);
				count++;
			}
		} else if (buffer_uptodate(bh)) {
			/*
			 * we got here because the buffer is already mapped.
			 * That means it must already have extents allocated
			 * underneath it. Map the extent by reading it.
			 */
			if (!imap_valid || flags != BMAPI_READ) {
				flags = BMAPI_READ;
				size = xfs_probe_cluster(inode, page, bh, head);
				err = xfs_map_blocks(inode, offset, size,
						&imap, flags);
				if (err)
					goto error;
				imap_valid = xfs_imap_valid(inode, &imap,
							    offset);
			}

			/*
			 * We set the type to IO_NEW in case we are doing a
			 * small write at EOF that is extending the file but
			 * without needing an allocation. We need to update the
			 * file size on I/O completion in this case so it is
			 * the same case as having just allocated a new extent
			 * that we are writing into for the first time.
			 */
			type = IO_NEW;
			if (trylock_buffer(bh)) {
				if (imap_valid)
					all_bh = 1;
				xfs_add_to_ioend(inode, bh, offset, type,
						&ioend, !imap_valid);
				count++;
			} else {
				imap_valid = 0;
			}
		} else if (PageUptodate(page)) {
			ASSERT(buffer_mapped(bh));
			imap_valid = 0;
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
					wbc, all_bh, end_index);
	}

	if (iohead)
		xfs_submit_ioend(wbc, iohead);

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
	int			flags = create ? BMAPI_WRITE : BMAPI_READ;
	struct xfs_bmbt_irec	imap;
	xfs_off_t		offset;
	ssize_t			size;
	int			nimap = 1;
	int			new = 0;
	int			error;

	offset = (xfs_off_t)iblock << inode->i_blkbits;
	ASSERT(bh_result->b_size >= (1 << inode->i_blkbits));
	size = bh_result->b_size;

	if (!create && direct && offset >= i_size_read(inode))
		return 0;

	if (direct && create)
		flags |= BMAPI_DIRECT;

	error = xfs_iomap(XFS_I(inode), offset, size, flags, &imap, &nimap,
			  &new);
	if (error)
		return -error;
	if (nimap == 0)
		return 0;

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
 * to do this and we are done.  But in case this was a successfull AIO
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
	 * blockdev_direct_IO can return an error even after the I/O
	 * completion handler was called.  Thus we need to protect
	 * against double-freeing.
	 */
	iocb->private = NULL;

	ioend->io_offset = offset;
	ioend->io_size = size;
	if (private && size > 0)
		ioend->io_type = IO_UNWRITTEN;

	if (is_async) {
		/*
		 * If we are converting an unwritten extent we need to delay
		 * the AIO completion until after the unwrittent extent
		 * conversion has completed, otherwise do it ASAP.
		 */
		if (ioend->io_type == IO_UNWRITTEN) {
			ioend->io_iocb = iocb;
			ioend->io_result = ret;
		} else {
			aio_complete(iocb, ret, 0);
		}
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
	ssize_t			ret;

	if (rw & WRITE) {
		iocb->private = xfs_alloc_ioend(inode, IO_NEW);

		ret = __blockdev_direct_IO(rw, iocb, inode, bdev, iov,
					    offset, nr_segs,
					    xfs_get_blocks_direct,
					    xfs_end_io_direct_write, NULL, 0);
		if (ret != -EIOCBQUEUED && iocb->private)
			xfs_destroy_ioend(iocb->private);
	} else {
		ret = __blockdev_direct_IO(rw, iocb, inode, bdev, iov,
					    offset, nr_segs,
					    xfs_get_blocks_direct,
					    NULL, NULL, 0);
	}

	return ret;
}

STATIC void
xfs_vm_write_failed(
	struct address_space	*mapping,
	loff_t			to)
{
	struct inode		*inode = mapping->host;

	if (to > inode->i_size) {
		struct iattr	ia = {
			.ia_valid	= ATTR_SIZE | ATTR_FORCE,
			.ia_size	= inode->i_size,
		};
		xfs_setattr(XFS_I(inode), &ia, XFS_ATTR_NOLOCK);
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
	.sync_page		= block_sync_page,
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
