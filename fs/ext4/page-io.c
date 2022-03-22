// SPDX-License-Identifier: GPL-2.0
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
#include <linux/sched/mm.h>

#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"

static struct kmem_cache *io_end_cachep;
static struct kmem_cache *io_end_vec_cachep;

int __init ext4_init_pageio(void)
{
	io_end_cachep = KMEM_CACHE(ext4_io_end, SLAB_RECLAIM_ACCOUNT);
	if (io_end_cachep == NULL)
		return -ENOMEM;

	io_end_vec_cachep = KMEM_CACHE(ext4_io_end_vec, 0);
	if (io_end_vec_cachep == NULL) {
		kmem_cache_destroy(io_end_cachep);
		return -ENOMEM;
	}
	return 0;
}

void ext4_exit_pageio(void)
{
	kmem_cache_destroy(io_end_cachep);
	kmem_cache_destroy(io_end_vec_cachep);
}

struct ext4_io_end_vec *ext4_alloc_io_end_vec(ext4_io_end_t *io_end)
{
	struct ext4_io_end_vec *io_end_vec;

	io_end_vec = kmem_cache_zalloc(io_end_vec_cachep, GFP_NOFS);
	if (!io_end_vec)
		return ERR_PTR(-ENOMEM);
	INIT_LIST_HEAD(&io_end_vec->list);
	list_add_tail(&io_end_vec->list, &io_end->list_vec);
	return io_end_vec;
}

static void ext4_free_io_end_vec(ext4_io_end_t *io_end)
{
	struct ext4_io_end_vec *io_end_vec, *tmp;

	if (list_empty(&io_end->list_vec))
		return;
	list_for_each_entry_safe(io_end_vec, tmp, &io_end->list_vec, list) {
		list_del(&io_end_vec->list);
		kmem_cache_free(io_end_vec_cachep, io_end_vec);
	}
}

struct ext4_io_end_vec *ext4_last_io_end_vec(ext4_io_end_t *io_end)
{
	BUG_ON(list_empty(&io_end->list_vec));
	return list_last_entry(&io_end->list_vec, struct ext4_io_end_vec, list);
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
	struct bio_vec *bvec;
	struct bvec_iter_all iter_all;

	bio_for_each_segment_all(bvec, bio, iter_all) {
		struct page *page = bvec->bv_page;
		struct page *bounce_page = NULL;
		struct buffer_head *bh, *head;
		unsigned bio_start = bvec->bv_offset;
		unsigned bio_end = bio_start + bvec->bv_len;
		unsigned under_io = 0;
		unsigned long flags;

		if (fscrypt_is_bounce_page(page)) {
			bounce_page = page;
			page = fscrypt_pagecache_page(bounce_page);
		}

		if (bio->bi_status) {
			SetPageError(page);
			mapping_set_error(page->mapping, -EIO);
		}
		bh = head = page_buffers(page);
		/*
		 * We check all buffers in the page under b_uptodate_lock
		 * to avoid races with other end io clearing async_write flags
		 */
		spin_lock_irqsave(&head->b_uptodate_lock, flags);
		do {
			if (bh_offset(bh) < bio_start ||
			    bh_offset(bh) + bh->b_size > bio_end) {
				if (buffer_async_write(bh))
					under_io++;
				continue;
			}
			clear_buffer_async_write(bh);
			if (bio->bi_status)
				buffer_io_error(bh);
		} while ((bh = bh->b_this_page) != head);
		spin_unlock_irqrestore(&head->b_uptodate_lock, flags);
		if (!under_io) {
			fscrypt_free_bounce_page(bounce_page);
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
	ext4_free_io_end_vec(io_end);
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
static int ext4_end_io_end(ext4_io_end_t *io_end)
{
	struct inode *inode = io_end->inode;
	handle_t *handle = io_end->handle;
	int ret = 0;

	ext4_debug("ext4_end_io_nolock: io_end 0x%p from inode %lu,list->next 0x%p,"
		   "list->prev 0x%p\n",
		   io_end, inode->i_ino, io_end->list.next, io_end->list.prev);

	io_end->handle = NULL;	/* Following call will use up the handle */
	ret = ext4_convert_unwritten_io_end_vec(handle, io_end);
	if (ret < 0 && !ext4_forced_shutdown(EXT4_SB(inode->i_sb))) {
		ext4_msg(inode->i_sb, KERN_EMERG,
			 "failed to convert unwritten extents to written "
			 "extents -- potential data loss!  "
			 "(inode %lu, error %d)", inode->i_ino, ret);
	}
	ext4_clear_io_unwritten_flag(io_end);
	ext4_release_io_end(io_end);
	return ret;
}

static void dump_completed_IO(struct inode *inode, struct list_head *head)
{
#ifdef	EXT4FS_DEBUG
	struct list_head *cur, *before, *after;
	ext4_io_end_t *io_end, *io_end0, *io_end1;

	if (list_empty(head))
		return;

	ext4_debug("Dump inode %lu completed io list\n", inode->i_ino);
	list_for_each_entry(io_end, head, list) {
		cur = &io_end->list;
		before = cur->prev;
		io_end0 = container_of(before, ext4_io_end_t, list);
		after = cur->next;
		io_end1 = container_of(after, ext4_io_end_t, list);

		ext4_debug("io 0x%p from inode %lu,prev 0x%p,next 0x%p\n",
			    io_end, inode->i_ino, io_end0, io_end1);
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
	ext4_io_end_t *io_end;
	struct list_head unwritten;
	unsigned long flags;
	struct ext4_inode_info *ei = EXT4_I(inode);
	int err, ret = 0;

	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	dump_completed_IO(inode, head);
	list_replace_init(head, &unwritten);
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);

	while (!list_empty(&unwritten)) {
		io_end = list_entry(unwritten.next, ext4_io_end_t, list);
		BUG_ON(!(io_end->flag & EXT4_IO_END_UNWRITTEN));
		list_del_init(&io_end->list);

		err = ext4_end_io_end(io_end);
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
	ext4_io_end_t *io_end = kmem_cache_zalloc(io_end_cachep, flags);

	if (io_end) {
		io_end->inode = inode;
		INIT_LIST_HEAD(&io_end->list);
		INIT_LIST_HEAD(&io_end->list_vec);
		refcount_set(&io_end->count, 1);
	}
	return io_end;
}

void ext4_put_io_end_defer(ext4_io_end_t *io_end)
{
	if (refcount_dec_and_test(&io_end->count)) {
		if (!(io_end->flag & EXT4_IO_END_UNWRITTEN) ||
				list_empty(&io_end->list_vec)) {
			ext4_release_io_end(io_end);
			return;
		}
		ext4_add_complete_io(io_end);
	}
}

int ext4_put_io_end(ext4_io_end_t *io_end)
{
	int err = 0;

	if (refcount_dec_and_test(&io_end->count)) {
		if (io_end->flag & EXT4_IO_END_UNWRITTEN) {
			err = ext4_convert_unwritten_io_end_vec(io_end->handle,
								io_end);
			io_end->handle = NULL;
			ext4_clear_io_unwritten_flag(io_end);
		}
		ext4_release_io_end(io_end);
	}
	return err;
}

ext4_io_end_t *ext4_get_io_end(ext4_io_end_t *io_end)
{
	refcount_inc(&io_end->count);
	return io_end;
}

/* BIO completion function for page writeback */
static void ext4_end_bio(struct bio *bio)
{
	ext4_io_end_t *io_end = bio->bi_private;
	sector_t bi_sector = bio->bi_iter.bi_sector;

	if (WARN_ONCE(!io_end, "io_end is NULL: %pg: sector %Lu len %u err %d\n",
		      bio->bi_bdev,
		      (long long) bio->bi_iter.bi_sector,
		      (unsigned) bio_sectors(bio),
		      bio->bi_status)) {
		ext4_finish_bio(bio);
		bio_put(bio);
		return;
	}
	bio->bi_end_io = NULL;

	if (bio->bi_status) {
		struct inode *inode = io_end->inode;

		ext4_warning(inode->i_sb, "I/O error %d writing to inode %lu "
			     "starting block %llu)",
			     bio->bi_status, inode->i_ino,
			     (unsigned long long)
			     bi_sector >> (inode->i_blkbits - 9));
		mapping_set_error(inode->i_mapping,
				blk_status_to_errno(bio->bi_status));
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
		if (io->io_wbc->sync_mode == WB_SYNC_ALL)
			io->io_bio->bi_opf |= REQ_SYNC;
		io->io_bio->bi_write_hint = io->io_end->inode->i_write_hint;
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

static void io_submit_init_bio(struct ext4_io_submit *io,
			       struct buffer_head *bh)
{
	struct bio *bio;

	/*
	 * bio_alloc will _always_ be able to allocate a bio if
	 * __GFP_DIRECT_RECLAIM is set, see comments for bio_alloc_bioset().
	 */
	bio = bio_alloc(bh->b_bdev, BIO_MAX_VECS, REQ_OP_WRITE, GFP_NOIO);
	fscrypt_set_bio_crypt_ctx_bh(bio, bh, GFP_NOIO);
	bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> 9);
	bio->bi_end_io = ext4_end_bio;
	bio->bi_private = ext4_get_io_end(io->io_end);
	io->io_bio = bio;
	io->io_next_block = bh->b_blocknr;
	wbc_init_bio(io->io_wbc, bio);
}

static void io_submit_add_bh(struct ext4_io_submit *io,
			     struct inode *inode,
			     struct page *page,
			     struct buffer_head *bh)
{
	int ret;

	if (io->io_bio && (bh->b_blocknr != io->io_next_block ||
			   !fscrypt_mergeable_bio_bh(io->io_bio, bh))) {
submit_and_retry:
		ext4_io_submit(io);
	}
	if (io->io_bio == NULL) {
		io_submit_init_bio(io, bh);
		io->io_bio->bi_write_hint = inode->i_write_hint;
	}
	ret = bio_add_page(io->io_bio, page, bh->b_size, bh_offset(bh));
	if (ret != bh->b_size)
		goto submit_and_retry;
	wbc_account_cgroup_owner(io->io_wbc, page, bh->b_size);
	io->io_next_block++;
}

int ext4_bio_write_page(struct ext4_io_submit *io,
			struct page *page,
			int len,
			bool keep_towrite)
{
	struct page *bounce_page = NULL;
	struct inode *inode = page->mapping->host;
	unsigned block_start;
	struct buffer_head *bh, *head;
	int ret = 0;
	int nr_submitted = 0;
	int nr_to_submit = 0;
	struct writeback_control *wbc = io->io_wbc;

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
		if (buffer_new(bh))
			clear_buffer_new(bh);
		set_buffer_async_write(bh);
		nr_to_submit++;
	} while ((bh = bh->b_this_page) != head);

	bh = head = page_buffers(page);

	/*
	 * If any blocks are being written to an encrypted file, encrypt them
	 * into a bounce page.  For simplicity, just encrypt until the last
	 * block which might be needed.  This may cause some unneeded blocks
	 * (e.g. holes) to be unnecessarily encrypted, but this is rare and
	 * can't happen in the common case of blocksize == PAGE_SIZE.
	 */
	if (fscrypt_inode_uses_fs_layer_crypto(inode) && nr_to_submit) {
		gfp_t gfp_flags = GFP_NOFS;
		unsigned int enc_bytes = round_up(len, i_blocksize(inode));

		/*
		 * Since bounce page allocation uses a mempool, we can only use
		 * a waiting mask (i.e. request guaranteed allocation) on the
		 * first page of the bio.  Otherwise it can deadlock.
		 */
		if (io->io_bio)
			gfp_flags = GFP_NOWAIT | __GFP_NOWARN;
	retry_encrypt:
		bounce_page = fscrypt_encrypt_pagecache_blocks(page, enc_bytes,
							       0, gfp_flags);
		if (IS_ERR(bounce_page)) {
			ret = PTR_ERR(bounce_page);
			if (ret == -ENOMEM &&
			    (io->io_bio || wbc->sync_mode == WB_SYNC_ALL)) {
				gfp_t new_gfp_flags = GFP_NOFS;
				if (io->io_bio)
					ext4_io_submit(io);
				else
					new_gfp_flags |= __GFP_NOFAIL;
				memalloc_retry_wait(gfp_flags);
				gfp_flags = new_gfp_flags;
				goto retry_encrypt;
			}

			printk_ratelimited(KERN_ERR "%s: ret = %d\n", __func__, ret);
			redirty_page_for_writepage(wbc, page);
			do {
				clear_buffer_async_write(bh);
				bh = bh->b_this_page;
			} while (bh != head);
			goto unlock;
		}
	}

	/* Now submit buffers to write */
	do {
		if (!buffer_async_write(bh))
			continue;
		io_submit_add_bh(io, inode,
				 bounce_page ? bounce_page : page, bh);
		nr_submitted++;
		clear_buffer_dirty(bh);
	} while ((bh = bh->b_this_page) != head);

unlock:
	unlock_page(page);
	/* Nothing submitted - we have to end page writeback */
	if (!nr_submitted)
		end_page_writeback(page);
	return ret;
}
