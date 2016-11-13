/*
 * linux/fs/ext4/page-io.c
 *
 * This contains the new page_io functions for ext4
 *
 * Written by Theodore Ts'o, 2010.
 */

#include <linux/fs.h>
#include <linux/time.h>
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
#include <linux/mm.h>
#include <linux/backing-dev.h>
#include <linux/fscrypto.h>

#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"

static struct kmem_cache *io_end_cachep;

int __init ext4_init_pageio(void)
{
	io_end_cachep = KMEM_CACHE(ext4_io_end, SLAB_RECLAIM_ACCOUNT);
	if (io_end_cachep == NULL)
		return -ENOMEM;
	return 0;
}

void ext4_exit_pageio(void)
{
	kmem_cache_destroy(io_end_cachep);
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
	printk_ratelimited(KERN_ERR "Buffer I/O error on device %pg, logical block %llu\n",
		       bh->b_bdev,
			(unsigned long long)bh->b_blocknr);
}

static void ext4_finish_bio(struct bio *bio)
{
	int i;
	struct bio_vec *bvec;

	bio_for_each_segment_all(bvec, bio, i) {
		struct page *page = bvec->bv_page;
#ifdef CONFIG_EXT4_FS_ENCRYPTION
		struct page *data_page = NULL;
#endif
		struct buffer_head *bh, *head;
		unsigned bio_start = bvec->bv_offset;
		unsigned bio_end = bio_start + bvec->bv_len;
		unsigned under_io = 0;
		unsigned long flags;

		if (!page)
			continue;

#ifdef CONFIG_EXT4_FS_ENCRYPTION
		if (!page->mapping) {
			/* The bounce data pages are unmapped. */
			data_page = page;
			fscrypt_pullback_bio_page(&page, false);
		}
#endif

		if (bio->bi_error) {
			SetPageError(page);
			mapping_set_error(page->mapping, -EIO);
		}
		bh = head = page_buffers(page);
		/*
		 * We check all buffers in the page under BH_Uptodate_Lock
		 * to avoid races with other end io clearing async_write flags
		 */
		local_irq_save(flags);
		bit_spin_lock(BH_Uptodate_Lock, &head->b_state);
		do {
			if (bh_offset(bh) < bio_start ||
			    bh_offset(bh) + bh->b_size > bio_end) {
				if (buffer_async_write(bh))
					under_io++;
				continue;
			}
			clear_buffer_async_write(bh);
			if (bio->bi_error)
				buffer_io_error(bh);
		} while ((bh = bh->b_this_page) != head);
		bit_spin_unlock(BH_Uptodate_Lock, &head->b_state);
		local_irq_restore(flags);
		if (!under_io) {
#ifdef CONFIG_EXT4_FS_ENCRYPTION
			if (data_page)
				fscrypt_restore_control_page(data_page);
#endif
			end_page_writeback(page);
		}
	}
}

static void ext4_release_io_end(ext4_io_end_t *io_end)
{
	struct bio *bio, *next_bio;

	BUG_ON(!list_empty(&io_end->list));
	BUG_ON(io_end->flag & EXT4_IO_END_UNWRITTEN);
	WARN_ON(io_end->handle);

	for (bio = io_end->bio; bio; bio = next_bio) {
		next_bio = bio->bi_private;
		ext4_finish_bio(bio);
		bio_put(bio);
	}
	kmem_cache_free(io_end_cachep, io_end);
}

/*
 * Check a range of space and convert unwritten extents to written. Note that
 * we are protected from truncate touching same part of extent tree by the
 * fact that truncate code waits for all DIO to finish (thus exclusion from
 * direct IO is achieved) and also waits for PageWriteback bits. Thus we
 * cannot get to ext4_ext_truncate() before all IOs overlapping that range are
 * completed (happens from ext4_free_ioend()).
 */
static int ext4_end_io(ext4_io_end_t *io)
{
	struct inode *inode = io->inode;
	loff_t offset = io->offset;
	ssize_t size = io->size;
	handle_t *handle = io->handle;
	int ret = 0;

	ext4_debug("ext4_end_io_nolock: io 0x%p from inode %lu,list->next 0x%p,"
		   "list->prev 0x%p\n",
		   io, inode->i_ino, io->list.next, io->list.prev);

	io->handle = NULL;	/* Following call will use up the handle */
	ret = ext4_convert_unwritten_extents(handle, inode, offset, size);
	if (ret < 0) {
		ext4_msg(inode->i_sb, KERN_EMERG,
			 "failed to convert unwritten extents to written "
			 "extents -- potential data loss!  "
			 "(inode %lu, offset %llu, size %zd, error %d)",
			 inode->i_ino, offset, size, ret);
	}
	ext4_clear_io_unwritten_flag(io);
	ext4_release_io_end(io);
	return ret;
}

static void dump_completed_IO(struct inode *inode, struct list_head *head)
{
#ifdef	EXT4FS_DEBUG
	struct list_head *cur, *before, *after;
	ext4_io_end_t *io, *io0, *io1;

	if (list_empty(head))
		return;

	ext4_debug("Dump inode %lu completed io list\n", inode->i_ino);
	list_for_each_entry(io, head, list) {
		cur = &io->list;
		before = cur->prev;
		io0 = container_of(before, ext4_io_end_t, list);
		after = cur->next;
		io1 = container_of(after, ext4_io_end_t, list);

		ext4_debug("io 0x%p from inode %lu,prev 0x%p,next 0x%p\n",
			    io, inode->i_ino, io0, io1);
	}
#endif
}

/* Add the io_end to per-inode completed end_io list. */
static void ext4_add_complete_io(ext4_io_end_t *io_end)
{
	struct ext4_inode_info *ei = EXT4_I(io_end->inode);
	struct ext4_sb_info *sbi = EXT4_SB(io_end->inode->i_sb);
	struct workqueue_struct *wq;
	unsigned long flags;

	/* Only reserved conversions from writeback should enter here */
	WARN_ON(!(io_end->flag & EXT4_IO_END_UNWRITTEN));
	WARN_ON(!io_end->handle && sbi->s_journal);
	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	wq = sbi->rsv_conversion_wq;
	if (list_empty(&ei->i_rsv_conversion_list))
		queue_work(wq, &ei->i_rsv_conversion_work);
	list_add_tail(&io_end->list, &ei->i_rsv_conversion_list);
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);
}

static int ext4_do_flush_completed_IO(struct inode *inode,
				      struct list_head *head)
{
	ext4_io_end_t *io;
	struct list_head unwritten;
	unsigned long flags;
	struct ext4_inode_info *ei = EXT4_I(inode);
	int err, ret = 0;

	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	dump_completed_IO(inode, head);
	list_replace_init(head, &unwritten);
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);

	while (!list_empty(&unwritten)) {
		io = list_entry(unwritten.next, ext4_io_end_t, list);
		BUG_ON(!(io->flag & EXT4_IO_END_UNWRITTEN));
		list_del_init(&io->list);

		err = ext4_end_io(io);
		if (unlikely(!ret && err))
			ret = err;
	}
	return ret;
}

/*
 * work on completed IO, to convert unwritten extents to extents
 */
void ext4_end_io_rsv_work(struct work_struct *work)
{
	struct ext4_inode_info *ei = container_of(work, struct ext4_inode_info,
						  i_rsv_conversion_work);
	ext4_do_flush_completed_IO(&ei->vfs_inode, &ei->i_rsv_conversion_list);
}

ext4_io_end_t *ext4_init_io_end(struct inode *inode, gfp_t flags)
{
	ext4_io_end_t *io = kmem_cache_zalloc(io_end_cachep, flags);
	if (io) {
		io->inode = inode;
		INIT_LIST_HEAD(&io->list);
		atomic_set(&io->count, 1);
	}
	return io;
}

void ext4_put_io_end_defer(ext4_io_end_t *io_end)
{
	if (atomic_dec_and_test(&io_end->count)) {
		if (!(io_end->flag & EXT4_IO_END_UNWRITTEN) || !io_end->size) {
			ext4_release_io_end(io_end);
			return;
		}
		ext4_add_complete_io(io_end);
	}
}

int ext4_put_io_end(ext4_io_end_t *io_end)
{
	int err = 0;

	if (atomic_dec_and_test(&io_end->count)) {
		if (io_end->flag & EXT4_IO_END_UNWRITTEN) {
			err = ext4_convert_unwritten_extents(io_end->handle,
						io_end->inode, io_end->offset,
						io_end->size);
			io_end->handle = NULL;
			ext4_clear_io_unwritten_flag(io_end);
		}
		ext4_release_io_end(io_end);
	}
	return err;
}

ext4_io_end_t *ext4_get_io_end(ext4_io_end_t *io_end)
{
	atomic_inc(&io_end->count);
	return io_end;
}

/* BIO completion function for page writeback */
static void ext4_end_bio(struct bio *bio)
{
	ext4_io_end_t *io_end = bio->bi_private;
	sector_t bi_sector = bio->bi_iter.bi_sector;

	BUG_ON(!io_end);
	bio->bi_end_io = NULL;

	if (bio->bi_error) {
		struct inode *inode = io_end->inode;

		ext4_warning(inode->i_sb, "I/O error %d writing to inode %lu "
			     "(offset %llu size %ld starting block %llu)",
			     bio->bi_error, inode->i_ino,
			     (unsigned long long) io_end->offset,
			     (long) io_end->size,
			     (unsigned long long)
			     bi_sector >> (inode->i_blkbits - 9));
		mapping_set_error(inode->i_mapping, bio->bi_error);
	}

	if (io_end->flag & EXT4_IO_END_UNWRITTEN) {
		/*
		 * Link bio into list hanging from io_end. We have to do it
		 * atomically as bio completions can be racing against each
		 * other.
		 */
		bio->bi_private = xchg(&io_end->bio, bio);
		ext4_put_io_end_defer(io_end);
	} else {
		/*
		 * Drop io_end reference early. Inode can get freed once
		 * we finish the bio.
		 */
		ext4_put_io_end_defer(io_end);
		ext4_finish_bio(bio);
		bio_put(bio);
	}
}

void ext4_io_submit(struct ext4_io_submit *io)
{
	struct bio *bio = io->io_bio;

	if (bio) {
		int io_op_flags = io->io_wbc->sync_mode == WB_SYNC_ALL ?
				  WRITE_SYNC : 0;
		bio_set_op_attrs(io->io_bio, REQ_OP_WRITE, io_op_flags);
		submit_bio(io->io_bio);
	}
	io->io_bio = NULL;
}

void ext4_io_submit_init(struct ext4_io_submit *io,
			 struct writeback_control *wbc)
{
	io->io_wbc = wbc;
	io->io_bio = NULL;
	io->io_end = NULL;
}

static int io_submit_init_bio(struct ext4_io_submit *io,
			      struct buffer_head *bh)
{
	struct bio *bio;

	bio = bio_alloc(GFP_NOIO, BIO_MAX_PAGES);
	if (!bio)
		return -ENOMEM;
	wbc_init_bio(io->io_wbc, bio);
	bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_bdev = bh->b_bdev;
	bio->bi_end_io = ext4_end_bio;
	bio->bi_private = ext4_get_io_end(io->io_end);
	io->io_bio = bio;
	io->io_next_block = bh->b_blocknr;
	return 0;
}

static int io_submit_add_bh(struct ext4_io_submit *io,
			    struct inode *inode,
			    struct page *page,
			    struct buffer_head *bh)
{
	int ret;

	if (io->io_bio && bh->b_blocknr != io->io_next_block) {
submit_and_retry:
		ext4_io_submit(io);
	}
	if (io->io_bio == NULL) {
		ret = io_submit_init_bio(io, bh);
		if (ret)
			return ret;
	}
	ret = bio_add_page(io->io_bio, page, bh->b_size, bh_offset(bh));
	if (ret != bh->b_size)
		goto submit_and_retry;
	wbc_account_io(io->io_wbc, page, bh->b_size);
	io->io_next_block++;
	return 0;
}

int ext4_bio_write_page(struct ext4_io_submit *io,
			struct page *page,
			int len,
			struct writeback_control *wbc,
			bool keep_towrite)
{
	struct page *data_page = NULL;
	struct inode *inode = page->mapping->host;
	unsigned block_start;
	struct buffer_head *bh, *head;
	int ret = 0;
	int nr_submitted = 0;
	int nr_to_submit = 0;

	BUG_ON(!PageLocked(page));
	BUG_ON(PageWriteback(page));

	if (keep_towrite)
		set_page_writeback_keepwrite(page);
	else
		set_page_writeback(page);
	ClearPageError(page);

	/*
	 * Comments copied from block_write_full_page:
	 *
	 * The page straddles i_size.  It must be zeroed out on each and every
	 * writepage invocation because it may be mmapped.  "A file is mapped
	 * in multiples of the page size.  For a file that is not a multiple of
	 * the page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	if (len < PAGE_SIZE)
		zero_user_segment(page, len, PAGE_SIZE);
	/*
	 * In the first loop we prepare and mark buffers to submit. We have to
	 * mark all buffers in the page before submitting so that
	 * end_page_writeback() cannot be called from ext4_bio_end_io() when IO
	 * on the first buffer finishes and we are still working on submitting
	 * the second buffer.
	 */
	bh = head = page_buffers(page);
	do {
		block_start = bh_offset(bh);
		if (block_start >= len) {
			clear_buffer_dirty(bh);
			set_buffer_uptodate(bh);
			continue;
		}
		if (!buffer_dirty(bh) || buffer_delay(bh) ||
		    !buffer_mapped(bh) || buffer_unwritten(bh)) {
			/* A hole? We can safely clear the dirty bit */
			if (!buffer_mapped(bh))
				clear_buffer_dirty(bh);
			if (io->io_bio)
				ext4_io_submit(io);
			continue;
		}
		if (buffer_new(bh)) {
			clear_buffer_new(bh);
			unmap_underlying_metadata(bh->b_bdev, bh->b_blocknr);
		}
		set_buffer_async_write(bh);
		nr_to_submit++;
	} while ((bh = bh->b_this_page) != head);

	bh = head = page_buffers(page);

	if (ext4_encrypted_inode(inode) && S_ISREG(inode->i_mode) &&
	    nr_to_submit) {
		gfp_t gfp_flags = GFP_NOFS;

	retry_encrypt:
		data_page = fscrypt_encrypt_page(inode, page, PAGE_SIZE, 0,
						page->index, gfp_flags);
		if (IS_ERR(data_page)) {
			ret = PTR_ERR(data_page);
			if (ret == -ENOMEM && wbc->sync_mode == WB_SYNC_ALL) {
				if (io->io_bio) {
					ext4_io_submit(io);
					congestion_wait(BLK_RW_ASYNC, HZ/50);
				}
				gfp_flags |= __GFP_NOFAIL;
				goto retry_encrypt;
			}
			data_page = NULL;
			goto out;
		}
	}

	/* Now submit buffers to write */
	do {
		if (!buffer_async_write(bh))
			continue;
		ret = io_submit_add_bh(io, inode,
				       data_page ? data_page : page, bh);
		if (ret) {
			/*
			 * We only get here on ENOMEM.  Not much else
			 * we can do but mark the page as dirty, and
			 * better luck next time.
			 */
			break;
		}
		nr_submitted++;
		clear_buffer_dirty(bh);
	} while ((bh = bh->b_this_page) != head);

	/* Error stopped previous loop? Clean up buffers... */
	if (ret) {
	out:
		if (data_page)
			fscrypt_restore_control_page(data_page);
		printk_ratelimited(KERN_ERR "%s: ret = %d\n", __func__, ret);
		redirty_page_for_writepage(wbc, page);
		do {
			clear_buffer_async_write(bh);
			bh = bh->b_this_page;
		} while (bh != head);
	}
	unlock_page(page);
	/* Nothing submitted - we have to end page writeback */
	if (!nr_submitted)
		end_page_writeback(page);
	return ret;
}
