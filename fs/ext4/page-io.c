/*
 * linux/fs/ext4/page-io.c
 *
 * This contains the new page_io functions for ext4
 *
 * Written by Theodore Ts'o, 2010.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/jbd2.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/string.h>
#include <linux/buffer_head.h>
#include <linux/writeback.h>
#include <linux/pagevec.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/uio.h>
#include <linux/bio.h>
#include <linux/workqueue.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "ext4_extents.h"

static struct kmem_cache *io_page_cachep, *io_end_cachep;

#define WQ_HASH_SZ		37
#define to_ioend_wq(v)	(&ioend_wq[((unsigned long)v) % WQ_HASH_SZ])
static wait_queue_head_t ioend_wq[WQ_HASH_SZ];

int __init ext4_init_pageio(void)
{
	int i;

	io_page_cachep = KMEM_CACHE(ext4_io_page, SLAB_RECLAIM_ACCOUNT);
	if (io_page_cachep == NULL)
		return -ENOMEM;
	io_end_cachep = KMEM_CACHE(ext4_io_end, SLAB_RECLAIM_ACCOUNT);
	if (io_end_cachep == NULL) {
		kmem_cache_destroy(io_page_cachep);
		return -ENOMEM;
	}
	for (i = 0; i < WQ_HASH_SZ; i++)
		init_waitqueue_head(&ioend_wq[i]);

	return 0;
}

void ext4_exit_pageio(void)
{
	kmem_cache_destroy(io_end_cachep);
	kmem_cache_destroy(io_page_cachep);
}

void ext4_ioend_wait(struct inode *inode)
{
	wait_queue_head_t *wq = to_ioend_wq(inode);

	wait_event(*wq, (atomic_read(&EXT4_I(inode)->i_ioend_count) == 0));
}

static void put_io_page(struct ext4_io_page *io_page)
{
	if (atomic_dec_and_test(&io_page->p_count)) {
		end_page_writeback(io_page->p_page);
		put_page(io_page->p_page);
		kmem_cache_free(io_page_cachep, io_page);
	}
}

void ext4_free_io_end(ext4_io_end_t *io)
{
	int i;
	wait_queue_head_t *wq;

	BUG_ON(!io);
	if (io->page)
		put_page(io->page);
	for (i = 0; i < io->num_io_pages; i++)
		put_io_page(io->pages[i]);
	io->num_io_pages = 0;
	wq = to_ioend_wq(io->inode);
	if (atomic_dec_and_test(&EXT4_I(io->inode)->i_ioend_count) &&
	    waitqueue_active(wq))
		wake_up_all(wq);
	kmem_cache_free(io_end_cachep, io);
}

/*
 * check a range of space and convert unwritten extents to written.
 */
int ext4_end_io_nolock(ext4_io_end_t *io)
{
	struct inode *inode = io->inode;
	loff_t offset = io->offset;
	ssize_t size = io->size;
	int ret = 0;

	ext4_debug("ext4_end_io_nolock: io 0x%p from inode %lu,list->next 0x%p,"
		   "list->prev 0x%p\n",
		   io, inode->i_ino, io->list.next, io->list.prev);

	if (list_empty(&io->list))
		return ret;

	if (!(io->flag & EXT4_IO_END_UNWRITTEN))
		return ret;

	ret = ext4_convert_unwritten_extents(inode, offset, size);
	if (ret < 0) {
		printk(KERN_EMERG "%s: failed to convert unwritten "
			"extents to written extents, error is %d "
			"io is still on inode %lu aio dio list\n",
		       __func__, ret, inode->i_ino);
		return ret;
	}

	if (io->iocb)
		aio_complete(io->iocb, io->result, 0);
	/* clear the DIO AIO unwritten flag */
	io->flag &= ~EXT4_IO_END_UNWRITTEN;
	return ret;
}

/*
 * work on completed aio dio IO, to convert unwritten extents to extents
 */
static void ext4_end_io_work(struct work_struct *work)
{
	ext4_io_end_t		*io = container_of(work, ext4_io_end_t, work);
	struct inode		*inode = io->inode;
	struct ext4_inode_info	*ei = EXT4_I(inode);
	unsigned long		flags;
	int			ret;

	mutex_lock(&inode->i_mutex);
	ret = ext4_end_io_nolock(io);
	if (ret < 0) {
		mutex_unlock(&inode->i_mutex);
		return;
	}

	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	if (!list_empty(&io->list))
		list_del_init(&io->list);
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);
	mutex_unlock(&inode->i_mutex);
	ext4_free_io_end(io);
}

ext4_io_end_t *ext4_init_io_end(struct inode *inode, gfp_t flags)
{
	ext4_io_end_t *io = kmem_cache_zalloc(io_end_cachep, flags);
	if (io) {
		atomic_inc(&EXT4_I(inode)->i_ioend_count);
		io->inode = inode;
		INIT_WORK(&io->work, ext4_end_io_work);
		INIT_LIST_HEAD(&io->list);
	}
	return io;
}

/*
 * Print an buffer I/O error compatible with the fs/buffer.c.  This
 * provides compatibility with dmesg scrapers that look for a specific
 * buffer I/O error message.  We really need a unified error reporting
 * structure to userspace ala Digital Unix's uerf system, but it's
 * probably not going to happen in my lifetime, due to LKML politics...
 */
static void buffer_io_error(struct buffer_head *bh)
{
	char b[BDEVNAME_SIZE];
	printk(KERN_ERR "Buffer I/O error on device %s, logical block %llu\n",
			bdevname(bh->b_bdev, b),
			(unsigned long long)bh->b_blocknr);
}

static void ext4_end_bio(struct bio *bio, int error)
{
	ext4_io_end_t *io_end = bio->bi_private;
	struct workqueue_struct *wq;
	struct inode *inode;
	unsigned long flags;
	int i;

	BUG_ON(!io_end);
	bio->bi_private = NULL;
	bio->bi_end_io = NULL;
	if (test_bit(BIO_UPTODATE, &bio->bi_flags))
		error = 0;
	bio_put(bio);

	for (i = 0; i < io_end->num_io_pages; i++) {
		struct page *page = io_end->pages[i]->p_page;
		struct buffer_head *bh, *head;
		int partial_write = 0;

		head = page_buffers(page);
		if (error)
			SetPageError(page);
		BUG_ON(!head);
		if (head->b_size == PAGE_CACHE_SIZE)
			clear_buffer_dirty(head);
		else {
			loff_t offset;
			loff_t io_end_offset = io_end->offset + io_end->size;

			offset = (sector_t) page->index << PAGE_CACHE_SHIFT;
			bh = head;
			do {
				if ((offset >= io_end->offset) &&
				    (offset+bh->b_size <= io_end_offset)) {
					if (error)
						buffer_io_error(bh);

					clear_buffer_dirty(bh);
				}
				if (buffer_delay(bh))
					partial_write = 1;
				else if (!buffer_mapped(bh))
					clear_buffer_dirty(bh);
				else if (buffer_dirty(bh))
					partial_write = 1;
				offset += bh->b_size;
				bh = bh->b_this_page;
			} while (bh != head);
		}

		/*
		 * If this is a partial write which happened to make
		 * all buffers uptodate then we can optimize away a
		 * bogus readpage() for the next read(). Here we
		 * 'discover' whether the page went uptodate as a
		 * result of this (potentially partial) write.
		 */
		if (!partial_write)
			SetPageUptodate(page);

		put_io_page(io_end->pages[i]);
	}
	io_end->num_io_pages = 0;
	inode = io_end->inode;

	if (error) {
		io_end->flag |= EXT4_IO_END_ERROR;
		ext4_warning(inode->i_sb, "I/O error writing to inode %lu "
			     "(offset %llu size %ld starting block %llu)",
			     inode->i_ino,
			     (unsigned long long) io_end->offset,
			     (long) io_end->size,
			     (unsigned long long)
			     bio->bi_sector >> (inode->i_blkbits - 9));
	}

	/* Add the io_end to per-inode completed io list*/
	spin_lock_irqsave(&EXT4_I(inode)->i_completed_io_lock, flags);
	list_add_tail(&io_end->list, &EXT4_I(inode)->i_completed_io_list);
	spin_unlock_irqrestore(&EXT4_I(inode)->i_completed_io_lock, flags);

	wq = EXT4_SB(inode->i_sb)->dio_unwritten_wq;
	/* queue the work to convert unwritten extents to written */
	queue_work(wq, &io_end->work);
}

void ext4_io_submit(struct ext4_io_submit *io)
{
	struct bio *bio = io->io_bio;

	if (bio) {
		bio_get(io->io_bio);
		submit_bio(io->io_op, io->io_bio);
		BUG_ON(bio_flagged(io->io_bio, BIO_EOPNOTSUPP));
		bio_put(io->io_bio);
	}
	io->io_bio = 0;
	io->io_op = 0;
	io->io_end = 0;
}

static int io_submit_init(struct ext4_io_submit *io,
			  struct inode *inode,
			  struct writeback_control *wbc,
			  struct buffer_head *bh)
{
	ext4_io_end_t *io_end;
	struct page *page = bh->b_page;
	int nvecs = bio_get_nr_vecs(bh->b_bdev);
	struct bio *bio;

	io_end = ext4_init_io_end(inode, GFP_NOFS);
	if (!io_end)
		return -ENOMEM;
	do {
		bio = bio_alloc(GFP_NOIO, nvecs);
		nvecs >>= 1;
	} while (bio == NULL);

	bio->bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_bdev = bh->b_bdev;
	bio->bi_private = io->io_end = io_end;
	bio->bi_end_io = ext4_end_bio;

	io_end->offset = (page->index << PAGE_CACHE_SHIFT) + bh_offset(bh);

	io->io_bio = bio;
	io->io_op = (wbc->sync_mode == WB_SYNC_ALL ?
			WRITE_SYNC_PLUG : WRITE);
	io->io_next_block = bh->b_blocknr;
	return 0;
}

static int io_submit_add_bh(struct ext4_io_submit *io,
			    struct ext4_io_page *io_page,
			    struct inode *inode,
			    struct writeback_control *wbc,
			    struct buffer_head *bh)
{
	ext4_io_end_t *io_end;
	int ret;

	if (buffer_new(bh)) {
		clear_buffer_new(bh);
		unmap_underlying_metadata(bh->b_bdev, bh->b_blocknr);
	}

	if (!buffer_mapped(bh) || buffer_delay(bh)) {
		if (!buffer_mapped(bh))
			clear_buffer_dirty(bh);
		if (io->io_bio)
			ext4_io_submit(io);
		return 0;
	}

	if (io->io_bio && bh->b_blocknr != io->io_next_block) {
submit_and_retry:
		ext4_io_submit(io);
	}
	if (io->io_bio == NULL) {
		ret = io_submit_init(io, inode, wbc, bh);
		if (ret)
			return ret;
	}
	io_end = io->io_end;
	if ((io_end->num_io_pages >= MAX_IO_PAGES) &&
	    (io_end->pages[io_end->num_io_pages-1] != io_page))
		goto submit_and_retry;
	if (buffer_uninit(bh))
		io->io_end->flag |= EXT4_IO_END_UNWRITTEN;
	io->io_end->size += bh->b_size;
	io->io_next_block++;
	ret = bio_add_page(io->io_bio, bh->b_page, bh->b_size, bh_offset(bh));
	if (ret != bh->b_size)
		goto submit_and_retry;
	if ((io_end->num_io_pages == 0) ||
	    (io_end->pages[io_end->num_io_pages-1] != io_page)) {
		io_end->pages[io_end->num_io_pages++] = io_page;
		atomic_inc(&io_page->p_count);
	}
	return 0;
}

int ext4_bio_write_page(struct ext4_io_submit *io,
			struct page *page,
			int len,
			struct writeback_control *wbc)
{
	struct inode *inode = page->mapping->host;
	unsigned block_start, block_end, blocksize;
	struct ext4_io_page *io_page;
	struct buffer_head *bh, *head;
	int ret = 0;

	blocksize = 1 << inode->i_blkbits;

	BUG_ON(PageWriteback(page));
	set_page_writeback(page);
	ClearPageError(page);

	io_page = kmem_cache_alloc(io_page_cachep, GFP_NOFS);
	if (!io_page) {
		set_page_dirty(page);
		unlock_page(page);
		return -ENOMEM;
	}
	io_page->p_page = page;
	atomic_set(&io_page->p_count, 1);
	get_page(page);

	for (bh = head = page_buffers(page), block_start = 0;
	     bh != head || !block_start;
	     block_start = block_end, bh = bh->b_this_page) {
		block_end = block_start + blocksize;
		if (block_start >= len) {
			clear_buffer_dirty(bh);
			set_buffer_uptodate(bh);
			continue;
		}
		ret = io_submit_add_bh(io, io_page, inode, wbc, bh);
		if (ret) {
			/*
			 * We only get here on ENOMEM.  Not much else
			 * we can do but mark the page as dirty, and
			 * better luck next time.
			 */
			set_page_dirty(page);
			break;
		}
	}
	unlock_page(page);
	/*
	 * If the page was truncated before we could do the writeback,
	 * or we had a memory allocation error while trying to write
	 * the first buffer head, we won't have submitted any pages for
	 * I/O.  In that case we need to make sure we've cleared the
	 * PageWriteback bit from the page to prevent the system from
	 * wedging later on.
	 */
	put_io_page(io_page);
	return ret;
}
