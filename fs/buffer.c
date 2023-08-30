// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/fs/buffer.c
 *
 *  Copyright (C) 1991, 1992, 2002  Linus Torvalds
 */

/*
 * Start bdflush() with kernel_thread not syscall - Paul Gortmaker, 12/95
 *
 * Removed a lot of unnecessary code and simplified things now that
 * the buffer cache isn't our primary cache - Andrew Tridgell 12/96
 *
 * Speed up hash, lru, and free list operations.  Use gfp() for allocating
 * hash table, use SLAB cache for buffer heads. SMP threading.  -DaveM
 *
 * Added 32k buffer block sizes - these are required older ARM systems. - RMK
 *
 * async buffer flushing, 1999 Andrea Arcangeli <andrea@suse.de>
 */

#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/iomap.h>
#include <linux/mm.h>
#include <linux/percpu.h>
#include <linux/slab.h>
#include <linux/capability.h>
#include <linux/blkdev.h>
#include <linux/file.h>
#include <linux/quotaops.h>
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/backing-dev.h>
#include <linux/writeback.h>
#include <linux/hash.h>
#include <linux/suspend.h>
#include <linux/buffer_head.h>
#include <linux/task_io_accounting_ops.h>
#include <linux/bio.h>
#include <linux/cpu.h>
#include <linux/bitops.h>
#include <linux/mpage.h>
#include <linux/bit_spinlock.h>
#include <linux/pagevec.h>
#include <linux/sched/mm.h>
#include <trace/events/block.h>
#include <linux/fscrypt.h>
#include <linux/fsverity.h>

#include "internal.h"

static int fsync_buffers_list(spinlock_t *lock, struct list_head *list);
static void submit_bh_wbc(blk_opf_t opf, struct buffer_head *bh,
			  struct writeback_control *wbc);

#define BH_ENTRY(list) list_entry((list), struct buffer_head, b_assoc_buffers)

inline void touch_buffer(struct buffer_head *bh)
{
	trace_block_touch_buffer(bh);
	folio_mark_accessed(bh->b_folio);
}
EXPORT_SYMBOL(touch_buffer);

void __lock_buffer(struct buffer_head *bh)
{
	wait_on_bit_lock_io(&bh->b_state, BH_Lock, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(__lock_buffer);

void unlock_buffer(struct buffer_head *bh)
{
	clear_bit_unlock(BH_Lock, &bh->b_state);
	smp_mb__after_atomic();
	wake_up_bit(&bh->b_state, BH_Lock);
}
EXPORT_SYMBOL(unlock_buffer);

/*
 * Returns if the folio has dirty or writeback buffers. If all the buffers
 * are unlocked and clean then the folio_test_dirty information is stale. If
 * any of the buffers are locked, it is assumed they are locked for IO.
 */
void buffer_check_dirty_writeback(struct folio *folio,
				     bool *dirty, bool *writeback)
{
	struct buffer_head *head, *bh;
	*dirty = false;
	*writeback = false;

	BUG_ON(!folio_test_locked(folio));

	head = folio_buffers(folio);
	if (!head)
		return;

	if (folio_test_writeback(folio))
		*writeback = true;

	bh = head;
	do {
		if (buffer_locked(bh))
			*writeback = true;

		if (buffer_dirty(bh))
			*dirty = true;

		bh = bh->b_this_page;
	} while (bh != head);
}

/*
 * Block until a buffer comes unlocked.  This doesn't stop it
 * from becoming locked again - you have to lock it yourself
 * if you want to preserve its state.
 */
void __wait_on_buffer(struct buffer_head * bh)
{
	wait_on_bit_io(&bh->b_state, BH_Lock, TASK_UNINTERRUPTIBLE);
}
EXPORT_SYMBOL(__wait_on_buffer);

static void buffer_io_error(struct buffer_head *bh, char *msg)
{
	if (!test_bit(BH_Quiet, &bh->b_state))
		printk_ratelimited(KERN_ERR
			"Buffer I/O error on dev %pg, logical block %llu%s\n",
			bh->b_bdev, (unsigned long long)bh->b_blocknr, msg);
}

/*
 * End-of-IO handler helper function which does not touch the bh after
 * unlocking it.
 * Note: unlock_buffer() sort-of does touch the bh after unlocking it, but
 * a race there is benign: unlock_buffer() only use the bh's address for
 * hashing after unlocking the buffer, so it doesn't actually touch the bh
 * itself.
 */
static void __end_buffer_read_notouch(struct buffer_head *bh, int uptodate)
{
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		/* This happens, due to failed read-ahead attempts. */
		clear_buffer_uptodate(bh);
	}
	unlock_buffer(bh);
}

/*
 * Default synchronous end-of-IO handler..  Just mark it up-to-date and
 * unlock the buffer.
 */
void end_buffer_read_sync(struct buffer_head *bh, int uptodate)
{
	__end_buffer_read_notouch(bh, uptodate);
	put_bh(bh);
}
EXPORT_SYMBOL(end_buffer_read_sync);

void end_buffer_write_sync(struct buffer_head *bh, int uptodate)
{
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		buffer_io_error(bh, ", lost sync page write");
		mark_buffer_write_io_error(bh);
		clear_buffer_uptodate(bh);
	}
	unlock_buffer(bh);
	put_bh(bh);
}
EXPORT_SYMBOL(end_buffer_write_sync);

/*
 * Various filesystems appear to want __find_get_block to be non-blocking.
 * But it's the page lock which protects the buffers.  To get around this,
 * we get exclusion from try_to_free_buffers with the blockdev mapping's
 * private_lock.
 *
 * Hack idea: for the blockdev mapping, private_lock contention
 * may be quite high.  This code could TryLock the page, and if that
 * succeeds, there is no need to take private_lock.
 */
static struct buffer_head *
__find_get_block_slow(struct block_device *bdev, sector_t block)
{
	struct inode *bd_inode = bdev->bd_inode;
	struct address_space *bd_mapping = bd_inode->i_mapping;
	struct buffer_head *ret = NULL;
	pgoff_t index;
	struct buffer_head *bh;
	struct buffer_head *head;
	struct folio *folio;
	int all_mapped = 1;
	static DEFINE_RATELIMIT_STATE(last_warned, HZ, 1);

	index = block >> (PAGE_SHIFT - bd_inode->i_blkbits);
	folio = __filemap_get_folio(bd_mapping, index, FGP_ACCESSED, 0);
	if (IS_ERR(folio))
		goto out;

	spin_lock(&bd_mapping->private_lock);
	head = folio_buffers(folio);
	if (!head)
		goto out_unlock;
	bh = head;
	do {
		if (!buffer_mapped(bh))
			all_mapped = 0;
		else if (bh->b_blocknr == block) {
			ret = bh;
			get_bh(bh);
			goto out_unlock;
		}
		bh = bh->b_this_page;
	} while (bh != head);

	/* we might be here because some of the buffers on this page are
	 * not mapped.  This is due to various races between
	 * file io on the block device and getblk.  It gets dealt with
	 * elsewhere, don't buffer_error if we had some unmapped buffers
	 */
	ratelimit_set_flags(&last_warned, RATELIMIT_MSG_ON_RELEASE);
	if (all_mapped && __ratelimit(&last_warned)) {
		printk("__find_get_block_slow() failed. block=%llu, "
		       "b_blocknr=%llu, b_state=0x%08lx, b_size=%zu, "
		       "device %pg blocksize: %d\n",
		       (unsigned long long)block,
		       (unsigned long long)bh->b_blocknr,
		       bh->b_state, bh->b_size, bdev,
		       1 << bd_inode->i_blkbits);
	}
out_unlock:
	spin_unlock(&bd_mapping->private_lock);
	folio_put(folio);
out:
	return ret;
}

static void end_buffer_async_read(struct buffer_head *bh, int uptodate)
{
	unsigned long flags;
	struct buffer_head *first;
	struct buffer_head *tmp;
	struct folio *folio;
	int folio_uptodate = 1;

	BUG_ON(!buffer_async_read(bh));

	folio = bh->b_folio;
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		clear_buffer_uptodate(bh);
		buffer_io_error(bh, ", async page read");
		folio_set_error(folio);
	}

	/*
	 * Be _very_ careful from here on. Bad things can happen if
	 * two buffer heads end IO at almost the same time and both
	 * decide that the page is now completely done.
	 */
	first = folio_buffers(folio);
	spin_lock_irqsave(&first->b_uptodate_lock, flags);
	clear_buffer_async_read(bh);
	unlock_buffer(bh);
	tmp = bh;
	do {
		if (!buffer_uptodate(tmp))
			folio_uptodate = 0;
		if (buffer_async_read(tmp)) {
			BUG_ON(!buffer_locked(tmp));
			goto still_busy;
		}
		tmp = tmp->b_this_page;
	} while (tmp != bh);
	spin_unlock_irqrestore(&first->b_uptodate_lock, flags);

	/*
	 * If all of the buffers are uptodate then we can set the page
	 * uptodate.
	 */
	if (folio_uptodate)
		folio_mark_uptodate(folio);
	folio_unlock(folio);
	return;

still_busy:
	spin_unlock_irqrestore(&first->b_uptodate_lock, flags);
	return;
}

struct postprocess_bh_ctx {
	struct work_struct work;
	struct buffer_head *bh;
};

static void verify_bh(struct work_struct *work)
{
	struct postprocess_bh_ctx *ctx =
		container_of(work, struct postprocess_bh_ctx, work);
	struct buffer_head *bh = ctx->bh;
	bool valid;

	valid = fsverity_verify_blocks(bh->b_folio, bh->b_size, bh_offset(bh));
	end_buffer_async_read(bh, valid);
	kfree(ctx);
}

static bool need_fsverity(struct buffer_head *bh)
{
	struct folio *folio = bh->b_folio;
	struct inode *inode = folio->mapping->host;

	return fsverity_active(inode) &&
		/* needed by ext4 */
		folio->index < DIV_ROUND_UP(inode->i_size, PAGE_SIZE);
}

static void decrypt_bh(struct work_struct *work)
{
	struct postprocess_bh_ctx *ctx =
		container_of(work, struct postprocess_bh_ctx, work);
	struct buffer_head *bh = ctx->bh;
	int err;

	err = fscrypt_decrypt_pagecache_blocks(bh->b_folio, bh->b_size,
					       bh_offset(bh));
	if (err == 0 && need_fsverity(bh)) {
		/*
		 * We use different work queues for decryption and for verity
		 * because verity may require reading metadata pages that need
		 * decryption, and we shouldn't recurse to the same workqueue.
		 */
		INIT_WORK(&ctx->work, verify_bh);
		fsverity_enqueue_verify_work(&ctx->work);
		return;
	}
	end_buffer_async_read(bh, err == 0);
	kfree(ctx);
}

/*
 * I/O completion handler for block_read_full_folio() - pages
 * which come unlocked at the end of I/O.
 */
static void end_buffer_async_read_io(struct buffer_head *bh, int uptodate)
{
	struct inode *inode = bh->b_folio->mapping->host;
	bool decrypt = fscrypt_inode_uses_fs_layer_crypto(inode);
	bool verify = need_fsverity(bh);

	/* Decrypt (with fscrypt) and/or verify (with fsverity) if needed. */
	if (uptodate && (decrypt || verify)) {
		struct postprocess_bh_ctx *ctx =
			kmalloc(sizeof(*ctx), GFP_ATOMIC);

		if (ctx) {
			ctx->bh = bh;
			if (decrypt) {
				INIT_WORK(&ctx->work, decrypt_bh);
				fscrypt_enqueue_decrypt_work(&ctx->work);
			} else {
				INIT_WORK(&ctx->work, verify_bh);
				fsverity_enqueue_verify_work(&ctx->work);
			}
			return;
		}
		uptodate = 0;
	}
	end_buffer_async_read(bh, uptodate);
}

/*
 * Completion handler for block_write_full_page() - pages which are unlocked
 * during I/O, and which have PageWriteback cleared upon I/O completion.
 */
void end_buffer_async_write(struct buffer_head *bh, int uptodate)
{
	unsigned long flags;
	struct buffer_head *first;
	struct buffer_head *tmp;
	struct folio *folio;

	BUG_ON(!buffer_async_write(bh));

	folio = bh->b_folio;
	if (uptodate) {
		set_buffer_uptodate(bh);
	} else {
		buffer_io_error(bh, ", lost async page write");
		mark_buffer_write_io_error(bh);
		clear_buffer_uptodate(bh);
		folio_set_error(folio);
	}

	first = folio_buffers(folio);
	spin_lock_irqsave(&first->b_uptodate_lock, flags);

	clear_buffer_async_write(bh);
	unlock_buffer(bh);
	tmp = bh->b_this_page;
	while (tmp != bh) {
		if (buffer_async_write(tmp)) {
			BUG_ON(!buffer_locked(tmp));
			goto still_busy;
		}
		tmp = tmp->b_this_page;
	}
	spin_unlock_irqrestore(&first->b_uptodate_lock, flags);
	folio_end_writeback(folio);
	return;

still_busy:
	spin_unlock_irqrestore(&first->b_uptodate_lock, flags);
	return;
}
EXPORT_SYMBOL(end_buffer_async_write);

/*
 * If a page's buffers are under async readin (end_buffer_async_read
 * completion) then there is a possibility that another thread of
 * control could lock one of the buffers after it has completed
 * but while some of the other buffers have not completed.  This
 * locked buffer would confuse end_buffer_async_read() into not unlocking
 * the page.  So the absence of BH_Async_Read tells end_buffer_async_read()
 * that this buffer is not under async I/O.
 *
 * The page comes unlocked when it has no locked buffer_async buffers
 * left.
 *
 * PageLocked prevents anyone starting new async I/O reads any of
 * the buffers.
 *
 * PageWriteback is used to prevent simultaneous writeout of the same
 * page.
 *
 * PageLocked prevents anyone from starting writeback of a page which is
 * under read I/O (PageWriteback is only ever set against a locked page).
 */
static void mark_buffer_async_read(struct buffer_head *bh)
{
	bh->b_end_io = end_buffer_async_read_io;
	set_buffer_async_read(bh);
}

static void mark_buffer_async_write_endio(struct buffer_head *bh,
					  bh_end_io_t *handler)
{
	bh->b_end_io = handler;
	set_buffer_async_write(bh);
}

void mark_buffer_async_write(struct buffer_head *bh)
{
	mark_buffer_async_write_endio(bh, end_buffer_async_write);
}
EXPORT_SYMBOL(mark_buffer_async_write);


/*
 * fs/buffer.c contains helper functions for buffer-backed address space's
 * fsync functions.  A common requirement for buffer-based filesystems is
 * that certain data from the backing blockdev needs to be written out for
 * a successful fsync().  For example, ext2 indirect blocks need to be
 * written back and waited upon before fsync() returns.
 *
 * The functions mark_buffer_inode_dirty(), fsync_inode_buffers(),
 * inode_has_buffers() and invalidate_inode_buffers() are provided for the
 * management of a list of dependent buffers at ->i_mapping->private_list.
 *
 * Locking is a little subtle: try_to_free_buffers() will remove buffers
 * from their controlling inode's queue when they are being freed.  But
 * try_to_free_buffers() will be operating against the *blockdev* mapping
 * at the time, not against the S_ISREG file which depends on those buffers.
 * So the locking for private_list is via the private_lock in the address_space
 * which backs the buffers.  Which is different from the address_space 
 * against which the buffers are listed.  So for a particular address_space,
 * mapping->private_lock does *not* protect mapping->private_list!  In fact,
 * mapping->private_list will always be protected by the backing blockdev's
 * ->private_lock.
 *
 * Which introduces a requirement: all buffers on an address_space's
 * ->private_list must be from the same address_space: the blockdev's.
 *
 * address_spaces which do not place buffers at ->private_list via these
 * utility functions are free to use private_lock and private_list for
 * whatever they want.  The only requirement is that list_empty(private_list)
 * be true at clear_inode() time.
 *
 * FIXME: clear_inode should not call invalidate_inode_buffers().  The
 * filesystems should do that.  invalidate_inode_buffers() should just go
 * BUG_ON(!list_empty).
 *
 * FIXME: mark_buffer_dirty_inode() is a data-plane operation.  It should
 * take an address_space, not an inode.  And it should be called
 * mark_buffer_dirty_fsync() to clearly define why those buffers are being
 * queued up.
 *
 * FIXME: mark_buffer_dirty_inode() doesn't need to add the buffer to the
 * list if it is already on a list.  Because if the buffer is on a list,
 * it *must* already be on the right one.  If not, the filesystem is being
 * silly.  This will save a ton of locking.  But first we have to ensure
 * that buffers are taken *off* the old inode's list when they are freed
 * (presumably in truncate).  That requires careful auditing of all
 * filesystems (do it inside bforget()).  It could also be done by bringing
 * b_inode back.
 */

/*
 * The buffer's backing address_space's private_lock must be held
 */
static void __remove_assoc_queue(struct buffer_head *bh)
{
	list_del_init(&bh->b_assoc_buffers);
	WARN_ON(!bh->b_assoc_map);
	bh->b_assoc_map = NULL;
}

int inode_has_buffers(struct inode *inode)
{
	return !list_empty(&inode->i_data.private_list);
}

/*
 * osync is designed to support O_SYNC io.  It waits synchronously for
 * all already-submitted IO to complete, but does not queue any new
 * writes to the disk.
 *
 * To do O_SYNC writes, just queue the buffer writes with write_dirty_buffer
 * as you dirty the buffers, and then use osync_inode_buffers to wait for
 * completion.  Any other dirty buffers which are not yet queued for
 * write will not be flushed to disk by the osync.
 */
static int osync_buffers_list(spinlock_t *lock, struct list_head *list)
{
	struct buffer_head *bh;
	struct list_head *p;
	int err = 0;

	spin_lock(lock);
repeat:
	list_for_each_prev(p, list) {
		bh = BH_ENTRY(p);
		if (buffer_locked(bh)) {
			get_bh(bh);
			spin_unlock(lock);
			wait_on_buffer(bh);
			if (!buffer_uptodate(bh))
				err = -EIO;
			brelse(bh);
			spin_lock(lock);
			goto repeat;
		}
	}
	spin_unlock(lock);
	return err;
}

/**
 * sync_mapping_buffers - write out & wait upon a mapping's "associated" buffers
 * @mapping: the mapping which wants those buffers written
 *
 * Starts I/O against the buffers at mapping->private_list, and waits upon
 * that I/O.
 *
 * Basically, this is a convenience function for fsync().
 * @mapping is a file or directory which needs those buffers to be written for
 * a successful fsync().
 */
int sync_mapping_buffers(struct address_space *mapping)
{
	struct address_space *buffer_mapping = mapping->private_data;

	if (buffer_mapping == NULL || list_empty(&mapping->private_list))
		return 0;

	return fsync_buffers_list(&buffer_mapping->private_lock,
					&mapping->private_list);
}
EXPORT_SYMBOL(sync_mapping_buffers);

/**
 * generic_buffers_fsync_noflush - generic buffer fsync implementation
 * for simple filesystems with no inode lock
 *
 * @file:	file to synchronize
 * @start:	start offset in bytes
 * @end:	end offset in bytes (inclusive)
 * @datasync:	only synchronize essential metadata if true
 *
 * This is a generic implementation of the fsync method for simple
 * filesystems which track all non-inode metadata in the buffers list
 * hanging off the address_space structure.
 */
int generic_buffers_fsync_noflush(struct file *file, loff_t start, loff_t end,
				  bool datasync)
{
	struct inode *inode = file->f_mapping->host;
	int err;
	int ret;

	err = file_write_and_wait_range(file, start, end);
	if (err)
		return err;

	ret = sync_mapping_buffers(inode->i_mapping);
	if (!(inode->i_state & I_DIRTY_ALL))
		goto out;
	if (datasync && !(inode->i_state & I_DIRTY_DATASYNC))
		goto out;

	err = sync_inode_metadata(inode, 1);
	if (ret == 0)
		ret = err;

out:
	/* check and advance again to catch errors after syncing out buffers */
	err = file_check_and_advance_wb_err(file);
	if (ret == 0)
		ret = err;
	return ret;
}
EXPORT_SYMBOL(generic_buffers_fsync_noflush);

/**
 * generic_buffers_fsync - generic buffer fsync implementation
 * for simple filesystems with no inode lock
 *
 * @file:	file to synchronize
 * @start:	start offset in bytes
 * @end:	end offset in bytes (inclusive)
 * @datasync:	only synchronize essential metadata if true
 *
 * This is a generic implementation of the fsync method for simple
 * filesystems which track all non-inode metadata in the buffers list
 * hanging off the address_space structure. This also makes sure that
 * a device cache flush operation is called at the end.
 */
int generic_buffers_fsync(struct file *file, loff_t start, loff_t end,
			  bool datasync)
{
	struct inode *inode = file->f_mapping->host;
	int ret;

	ret = generic_buffers_fsync_noflush(file, start, end, datasync);
	if (!ret)
		ret = blkdev_issue_flush(inode->i_sb->s_bdev);
	return ret;
}
EXPORT_SYMBOL(generic_buffers_fsync);

/*
 * Called when we've recently written block `bblock', and it is known that
 * `bblock' was for a buffer_boundary() buffer.  This means that the block at
 * `bblock + 1' is probably a dirty indirect block.  Hunt it down and, if it's
 * dirty, schedule it for IO.  So that indirects merge nicely with their data.
 */
void write_boundary_block(struct block_device *bdev,
			sector_t bblock, unsigned blocksize)
{
	struct buffer_head *bh = __find_get_block(bdev, bblock + 1, blocksize);
	if (bh) {
		if (buffer_dirty(bh))
			write_dirty_buffer(bh, 0);
		put_bh(bh);
	}
}

void mark_buffer_dirty_inode(struct buffer_head *bh, struct inode *inode)
{
	struct address_space *mapping = inode->i_mapping;
	struct address_space *buffer_mapping = bh->b_folio->mapping;

	mark_buffer_dirty(bh);
	if (!mapping->private_data) {
		mapping->private_data = buffer_mapping;
	} else {
		BUG_ON(mapping->private_data != buffer_mapping);
	}
	if (!bh->b_assoc_map) {
		spin_lock(&buffer_mapping->private_lock);
		list_move_tail(&bh->b_assoc_buffers,
				&mapping->private_list);
		bh->b_assoc_map = mapping;
		spin_unlock(&buffer_mapping->private_lock);
	}
}
EXPORT_SYMBOL(mark_buffer_dirty_inode);

/*
 * Add a page to the dirty page list.
 *
 * It is a sad fact of life that this function is called from several places
 * deeply under spinlocking.  It may not sleep.
 *
 * If the page has buffers, the uptodate buffers are set dirty, to preserve
 * dirty-state coherency between the page and the buffers.  It the page does
 * not have buffers then when they are later attached they will all be set
 * dirty.
 *
 * The buffers are dirtied before the page is dirtied.  There's a small race
 * window in which a writepage caller may see the page cleanness but not the
 * buffer dirtiness.  That's fine.  If this code were to set the page dirty
 * before the buffers, a concurrent writepage caller could clear the page dirty
 * bit, see a bunch of clean buffers and we'd end up with dirty buffers/clean
 * page on the dirty page list.
 *
 * We use private_lock to lock against try_to_free_buffers while using the
 * page's buffer list.  Also use this to protect against clean buffers being
 * added to the page after it was set dirty.
 *
 * FIXME: may need to call ->reservepage here as well.  That's rather up to the
 * address_space though.
 */
bool block_dirty_folio(struct address_space *mapping, struct folio *folio)
{
	struct buffer_head *head;
	bool newly_dirty;

	spin_lock(&mapping->private_lock);
	head = folio_buffers(folio);
	if (head) {
		struct buffer_head *bh = head;

		do {
			set_buffer_dirty(bh);
			bh = bh->b_this_page;
		} while (bh != head);
	}
	/*
	 * Lock out page's memcg migration to keep PageDirty
	 * synchronized with per-memcg dirty page counters.
	 */
	folio_memcg_lock(folio);
	newly_dirty = !folio_test_set_dirty(folio);
	spin_unlock(&mapping->private_lock);

	if (newly_dirty)
		__folio_mark_dirty(folio, mapping, 1);

	folio_memcg_unlock(folio);

	if (newly_dirty)
		__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);

	return newly_dirty;
}
EXPORT_SYMBOL(block_dirty_folio);

/*
 * Write out and wait upon a list of buffers.
 *
 * We have conflicting pressures: we want to make sure that all
 * initially dirty buffers get waited on, but that any subsequently
 * dirtied buffers don't.  After all, we don't want fsync to last
 * forever if somebody is actively writing to the file.
 *
 * Do this in two main stages: first we copy dirty buffers to a
 * temporary inode list, queueing the writes as we go.  Then we clean
 * up, waiting for those writes to complete.
 * 
 * During this second stage, any subsequent updates to the file may end
 * up refiling the buffer on the original inode's dirty list again, so
 * there is a chance we will end up with a buffer queued for write but
 * not yet completed on that list.  So, as a final cleanup we go through
 * the osync code to catch these locked, dirty buffers without requeuing
 * any newly dirty buffers for write.
 */
static int fsync_buffers_list(spinlock_t *lock, struct list_head *list)
{
	struct buffer_head *bh;
	struct list_head tmp;
	struct address_space *mapping;
	int err = 0, err2;
	struct blk_plug plug;

	INIT_LIST_HEAD(&tmp);
	blk_start_plug(&plug);

	spin_lock(lock);
	while (!list_empty(list)) {
		bh = BH_ENTRY(list->next);
		mapping = bh->b_assoc_map;
		__remove_assoc_queue(bh);
		/* Avoid race with mark_buffer_dirty_inode() which does
		 * a lockless check and we rely on seeing the dirty bit */
		smp_mb();
		if (buffer_dirty(bh) || buffer_locked(bh)) {
			list_add(&bh->b_assoc_buffers, &tmp);
			bh->b_assoc_map = mapping;
			if (buffer_dirty(bh)) {
				get_bh(bh);
				spin_unlock(lock);
				/*
				 * Ensure any pending I/O completes so that
				 * write_dirty_buffer() actually writes the
				 * current contents - it is a noop if I/O is
				 * still in flight on potentially older
				 * contents.
				 */
				write_dirty_buffer(bh, REQ_SYNC);

				/*
				 * Kick off IO for the previous mapping. Note
				 * that we will not run the very last mapping,
				 * wait_on_buffer() will do that for us
				 * through sync_buffer().
				 */
				brelse(bh);
				spin_lock(lock);
			}
		}
	}

	spin_unlock(lock);
	blk_finish_plug(&plug);
	spin_lock(lock);

	while (!list_empty(&tmp)) {
		bh = BH_ENTRY(tmp.prev);
		get_bh(bh);
		mapping = bh->b_assoc_map;
		__remove_assoc_queue(bh);
		/* Avoid race with mark_buffer_dirty_inode() which does
		 * a lockless check and we rely on seeing the dirty bit */
		smp_mb();
		if (buffer_dirty(bh)) {
			list_add(&bh->b_assoc_buffers,
				 &mapping->private_list);
			bh->b_assoc_map = mapping;
		}
		spin_unlock(lock);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			err = -EIO;
		brelse(bh);
		spin_lock(lock);
	}
	
	spin_unlock(lock);
	err2 = osync_buffers_list(lock, list);
	if (err)
		return err;
	else
		return err2;
}

/*
 * Invalidate any and all dirty buffers on a given inode.  We are
 * probably unmounting the fs, but that doesn't mean we have already
 * done a sync().  Just drop the buffers from the inode list.
 *
 * NOTE: we take the inode's blockdev's mapping's private_lock.  Which
 * assumes that all the buffers are against the blockdev.  Not true
 * for reiserfs.
 */
void invalidate_inode_buffers(struct inode *inode)
{
	if (inode_has_buffers(inode)) {
		struct address_space *mapping = &inode->i_data;
		struct list_head *list = &mapping->private_list;
		struct address_space *buffer_mapping = mapping->private_data;

		spin_lock(&buffer_mapping->private_lock);
		while (!list_empty(list))
			__remove_assoc_queue(BH_ENTRY(list->next));
		spin_unlock(&buffer_mapping->private_lock);
	}
}
EXPORT_SYMBOL(invalidate_inode_buffers);

/*
 * Remove any clean buffers from the inode's buffer list.  This is called
 * when we're trying to free the inode itself.  Those buffers can pin it.
 *
 * Returns true if all buffers were removed.
 */
int remove_inode_buffers(struct inode *inode)
{
	int ret = 1;

	if (inode_has_buffers(inode)) {
		struct address_space *mapping = &inode->i_data;
		struct list_head *list = &mapping->private_list;
		struct address_space *buffer_mapping = mapping->private_data;

		spin_lock(&buffer_mapping->private_lock);
		while (!list_empty(list)) {
			struct buffer_head *bh = BH_ENTRY(list->next);
			if (buffer_dirty(bh)) {
				ret = 0;
				break;
			}
			__remove_assoc_queue(bh);
		}
		spin_unlock(&buffer_mapping->private_lock);
	}
	return ret;
}

/*
 * Create the appropriate buffers when given a folio for data area and
 * the size of each buffer.. Use the bh->b_this_page linked list to
 * follow the buffers created.  Return NULL if unable to create more
 * buffers.
 *
 * The retry flag is used to differentiate async IO (paging, swapping)
 * which may not fail from ordinary buffer allocations.
 */
struct buffer_head *folio_alloc_buffers(struct folio *folio, unsigned long size,
					bool retry)
{
	struct buffer_head *bh, *head;
	gfp_t gfp = GFP_NOFS | __GFP_ACCOUNT;
	long offset;
	struct mem_cgroup *memcg, *old_memcg;

	if (retry)
		gfp |= __GFP_NOFAIL;

	/* The folio lock pins the memcg */
	memcg = folio_memcg(folio);
	old_memcg = set_active_memcg(memcg);

	head = NULL;
	offset = folio_size(folio);
	while ((offset -= size) >= 0) {
		bh = alloc_buffer_head(gfp);
		if (!bh)
			goto no_grow;

		bh->b_this_page = head;
		bh->b_blocknr = -1;
		head = bh;

		bh->b_size = size;

		/* Link the buffer to its folio */
		folio_set_bh(bh, folio, offset);
	}
out:
	set_active_memcg(old_memcg);
	return head;
/*
 * In case anything failed, we just free everything we got.
 */
no_grow:
	if (head) {
		do {
			bh = head;
			head = head->b_this_page;
			free_buffer_head(bh);
		} while (head);
	}

	goto out;
}
EXPORT_SYMBOL_GPL(folio_alloc_buffers);

struct buffer_head *alloc_page_buffers(struct page *page, unsigned long size,
				       bool retry)
{
	return folio_alloc_buffers(page_folio(page), size, retry);
}
EXPORT_SYMBOL_GPL(alloc_page_buffers);

static inline void link_dev_buffers(struct folio *folio,
		struct buffer_head *head)
{
	struct buffer_head *bh, *tail;

	bh = head;
	do {
		tail = bh;
		bh = bh->b_this_page;
	} while (bh);
	tail->b_this_page = head;
	folio_attach_private(folio, head);
}

static sector_t blkdev_max_block(struct block_device *bdev, unsigned int size)
{
	sector_t retval = ~((sector_t)0);
	loff_t sz = bdev_nr_bytes(bdev);

	if (sz) {
		unsigned int sizebits = blksize_bits(size);
		retval = (sz >> sizebits);
	}
	return retval;
}

/*
 * Initialise the state of a blockdev folio's buffers.
 */ 
static sector_t folio_init_buffers(struct folio *folio,
		struct block_device *bdev, sector_t block, int size)
{
	struct buffer_head *head = folio_buffers(folio);
	struct buffer_head *bh = head;
	bool uptodate = folio_test_uptodate(folio);
	sector_t end_block = blkdev_max_block(bdev, size);

	do {
		if (!buffer_mapped(bh)) {
			bh->b_end_io = NULL;
			bh->b_private = NULL;
			bh->b_bdev = bdev;
			bh->b_blocknr = block;
			if (uptodate)
				set_buffer_uptodate(bh);
			if (block < end_block)
				set_buffer_mapped(bh);
		}
		block++;
		bh = bh->b_this_page;
	} while (bh != head);

	/*
	 * Caller needs to validate requested block against end of device.
	 */
	return end_block;
}

/*
 * Create the page-cache page that contains the requested block.
 *
 * This is used purely for blockdev mappings.
 */
static int
grow_dev_page(struct block_device *bdev, sector_t block,
	      pgoff_t index, int size, int sizebits, gfp_t gfp)
{
	struct inode *inode = bdev->bd_inode;
	struct folio *folio;
	struct buffer_head *bh;
	sector_t end_block;
	int ret = 0;
	gfp_t gfp_mask;

	gfp_mask = mapping_gfp_constraint(inode->i_mapping, ~__GFP_FS) | gfp;

	/*
	 * XXX: __getblk_slow() can not really deal with failure and
	 * will endlessly loop on improvised global reclaim.  Prefer
	 * looping in the allocator rather than here, at least that
	 * code knows what it's doing.
	 */
	gfp_mask |= __GFP_NOFAIL;

	folio = __filemap_get_folio(inode->i_mapping, index,
			FGP_LOCK | FGP_ACCESSED | FGP_CREAT, gfp_mask);

	bh = folio_buffers(folio);
	if (bh) {
		if (bh->b_size == size) {
			end_block = folio_init_buffers(folio, bdev,
					(sector_t)index << sizebits, size);
			goto done;
		}
		if (!try_to_free_buffers(folio))
			goto failed;
	}

	bh = folio_alloc_buffers(folio, size, true);

	/*
	 * Link the folio to the buffers and initialise them.  Take the
	 * lock to be atomic wrt __find_get_block(), which does not
	 * run under the folio lock.
	 */
	spin_lock(&inode->i_mapping->private_lock);
	link_dev_buffers(folio, bh);
	end_block = folio_init_buffers(folio, bdev,
			(sector_t)index << sizebits, size);
	spin_unlock(&inode->i_mapping->private_lock);
done:
	ret = (block < end_block) ? 1 : -ENXIO;
failed:
	folio_unlock(folio);
	folio_put(folio);
	return ret;
}

/*
 * Create buffers for the specified block device block's page.  If
 * that page was dirty, the buffers are set dirty also.
 */
static int
grow_buffers(struct block_device *bdev, sector_t block, int size, gfp_t gfp)
{
	pgoff_t index;
	int sizebits;

	sizebits = PAGE_SHIFT - __ffs(size);
	index = block >> sizebits;

	/*
	 * Check for a block which wants to lie outside our maximum possible
	 * pagecache index.  (this comparison is done using sector_t types).
	 */
	if (unlikely(index != block >> sizebits)) {
		printk(KERN_ERR "%s: requested out-of-range block %llu for "
			"device %pg\n",
			__func__, (unsigned long long)block,
			bdev);
		return -EIO;
	}

	/* Create a page with the proper size buffers.. */
	return grow_dev_page(bdev, block, index, size, sizebits, gfp);
}

static struct buffer_head *
__getblk_slow(struct block_device *bdev, sector_t block,
	     unsigned size, gfp_t gfp)
{
	/* Size must be multiple of hard sectorsize */
	if (unlikely(size & (bdev_logical_block_size(bdev)-1) ||
			(size < 512 || size > PAGE_SIZE))) {
		printk(KERN_ERR "getblk(): invalid block size %d requested\n",
					size);
		printk(KERN_ERR "logical block size: %d\n",
					bdev_logical_block_size(bdev));

		dump_stack();
		return NULL;
	}

	for (;;) {
		struct buffer_head *bh;
		int ret;

		bh = __find_get_block(bdev, block, size);
		if (bh)
			return bh;

		ret = grow_buffers(bdev, block, size, gfp);
		if (ret < 0)
			return NULL;
	}
}

/*
 * The relationship between dirty buffers and dirty pages:
 *
 * Whenever a page has any dirty buffers, the page's dirty bit is set, and
 * the page is tagged dirty in the page cache.
 *
 * At all times, the dirtiness of the buffers represents the dirtiness of
 * subsections of the page.  If the page has buffers, the page dirty bit is
 * merely a hint about the true dirty state.
 *
 * When a page is set dirty in its entirety, all its buffers are marked dirty
 * (if the page has buffers).
 *
 * When a buffer is marked dirty, its page is dirtied, but the page's other
 * buffers are not.
 *
 * Also.  When blockdev buffers are explicitly read with bread(), they
 * individually become uptodate.  But their backing page remains not
 * uptodate - even if all of its buffers are uptodate.  A subsequent
 * block_read_full_folio() against that folio will discover all the uptodate
 * buffers, will set the folio uptodate and will perform no I/O.
 */

/**
 * mark_buffer_dirty - mark a buffer_head as needing writeout
 * @bh: the buffer_head to mark dirty
 *
 * mark_buffer_dirty() will set the dirty bit against the buffer, then set
 * its backing page dirty, then tag the page as dirty in the page cache
 * and then attach the address_space's inode to its superblock's dirty
 * inode list.
 *
 * mark_buffer_dirty() is atomic.  It takes bh->b_folio->mapping->private_lock,
 * i_pages lock and mapping->host->i_lock.
 */
void mark_buffer_dirty(struct buffer_head *bh)
{
	WARN_ON_ONCE(!buffer_uptodate(bh));

	trace_block_dirty_buffer(bh);

	/*
	 * Very *carefully* optimize the it-is-already-dirty case.
	 *
	 * Don't let the final "is it dirty" escape to before we
	 * perhaps modified the buffer.
	 */
	if (buffer_dirty(bh)) {
		smp_mb();
		if (buffer_dirty(bh))
			return;
	}

	if (!test_set_buffer_dirty(bh)) {
		struct folio *folio = bh->b_folio;
		struct address_space *mapping = NULL;

		folio_memcg_lock(folio);
		if (!folio_test_set_dirty(folio)) {
			mapping = folio->mapping;
			if (mapping)
				__folio_mark_dirty(folio, mapping, 0);
		}
		folio_memcg_unlock(folio);
		if (mapping)
			__mark_inode_dirty(mapping->host, I_DIRTY_PAGES);
	}
}
EXPORT_SYMBOL(mark_buffer_dirty);

void mark_buffer_write_io_error(struct buffer_head *bh)
{
	struct super_block *sb;

	set_buffer_write_io_error(bh);
	/* FIXME: do we need to set this in both places? */
	if (bh->b_folio && bh->b_folio->mapping)
		mapping_set_error(bh->b_folio->mapping, -EIO);
	if (bh->b_assoc_map)
		mapping_set_error(bh->b_assoc_map, -EIO);
	rcu_read_lock();
	sb = READ_ONCE(bh->b_bdev->bd_super);
	if (sb)
		errseq_set(&sb->s_wb_err, -EIO);
	rcu_read_unlock();
}
EXPORT_SYMBOL(mark_buffer_write_io_error);

/*
 * Decrement a buffer_head's reference count.  If all buffers against a page
 * have zero reference count, are clean and unlocked, and if the page is clean
 * and unlocked then try_to_free_buffers() may strip the buffers from the page
 * in preparation for freeing it (sometimes, rarely, buffers are removed from
 * a page but it ends up not being freed, and buffers may later be reattached).
 */
void __brelse(struct buffer_head * buf)
{
	if (atomic_read(&buf->b_count)) {
		put_bh(buf);
		return;
	}
	WARN(1, KERN_ERR "VFS: brelse: Trying to free free buffer\n");
}
EXPORT_SYMBOL(__brelse);

/*
 * bforget() is like brelse(), except it discards any
 * potentially dirty data.
 */
void __bforget(struct buffer_head *bh)
{
	clear_buffer_dirty(bh);
	if (bh->b_assoc_map) {
		struct address_space *buffer_mapping = bh->b_folio->mapping;

		spin_lock(&buffer_mapping->private_lock);
		list_del_init(&bh->b_assoc_buffers);
		bh->b_assoc_map = NULL;
		spin_unlock(&buffer_mapping->private_lock);
	}
	__brelse(bh);
}
EXPORT_SYMBOL(__bforget);

static struct buffer_head *__bread_slow(struct buffer_head *bh)
{
	lock_buffer(bh);
	if (buffer_uptodate(bh)) {
		unlock_buffer(bh);
		return bh;
	} else {
		get_bh(bh);
		bh->b_end_io = end_buffer_read_sync;
		submit_bh(REQ_OP_READ, bh);
		wait_on_buffer(bh);
		if (buffer_uptodate(bh))
			return bh;
	}
	brelse(bh);
	return NULL;
}

/*
 * Per-cpu buffer LRU implementation.  To reduce the cost of __find_get_block().
 * The bhs[] array is sorted - newest buffer is at bhs[0].  Buffers have their
 * refcount elevated by one when they're in an LRU.  A buffer can only appear
 * once in a particular CPU's LRU.  A single buffer can be present in multiple
 * CPU's LRUs at the same time.
 *
 * This is a transparent caching front-end to sb_bread(), sb_getblk() and
 * sb_find_get_block().
 *
 * The LRUs themselves only need locking against invalidate_bh_lrus.  We use
 * a local interrupt disable for that.
 */

#define BH_LRU_SIZE	16

struct bh_lru {
	struct buffer_head *bhs[BH_LRU_SIZE];
};

static DEFINE_PER_CPU(struct bh_lru, bh_lrus) = {{ NULL }};

#ifdef CONFIG_SMP
#define bh_lru_lock()	local_irq_disable()
#define bh_lru_unlock()	local_irq_enable()
#else
#define bh_lru_lock()	preempt_disable()
#define bh_lru_unlock()	preempt_enable()
#endif

static inline void check_irqs_on(void)
{
#ifdef irqs_disabled
	BUG_ON(irqs_disabled());
#endif
}

/*
 * Install a buffer_head into this cpu's LRU.  If not already in the LRU, it is
 * inserted at the front, and the buffer_head at the back if any is evicted.
 * Or, if already in the LRU it is moved to the front.
 */
static void bh_lru_install(struct buffer_head *bh)
{
	struct buffer_head *evictee = bh;
	struct bh_lru *b;
	int i;

	check_irqs_on();
	bh_lru_lock();

	/*
	 * the refcount of buffer_head in bh_lru prevents dropping the
	 * attached page(i.e., try_to_free_buffers) so it could cause
	 * failing page migration.
	 * Skip putting upcoming bh into bh_lru until migration is done.
	 */
	if (lru_cache_disabled()) {
		bh_lru_unlock();
		return;
	}

	b = this_cpu_ptr(&bh_lrus);
	for (i = 0; i < BH_LRU_SIZE; i++) {
		swap(evictee, b->bhs[i]);
		if (evictee == bh) {
			bh_lru_unlock();
			return;
		}
	}

	get_bh(bh);
	bh_lru_unlock();
	brelse(evictee);
}

/*
 * Look up the bh in this cpu's LRU.  If it's there, move it to the head.
 */
static struct buffer_head *
lookup_bh_lru(struct block_device *bdev, sector_t block, unsigned size)
{
	struct buffer_head *ret = NULL;
	unsigned int i;

	check_irqs_on();
	bh_lru_lock();
	for (i = 0; i < BH_LRU_SIZE; i++) {
		struct buffer_head *bh = __this_cpu_read(bh_lrus.bhs[i]);

		if (bh && bh->b_blocknr == block && bh->b_bdev == bdev &&
		    bh->b_size == size) {
			if (i) {
				while (i) {
					__this_cpu_write(bh_lrus.bhs[i],
						__this_cpu_read(bh_lrus.bhs[i - 1]));
					i--;
				}
				__this_cpu_write(bh_lrus.bhs[0], bh);
			}
			get_bh(bh);
			ret = bh;
			break;
		}
	}
	bh_lru_unlock();
	return ret;
}

/*
 * Perform a pagecache lookup for the matching buffer.  If it's there, refresh
 * it in the LRU and mark it as accessed.  If it is not present then return
 * NULL
 */
struct buffer_head *
__find_get_block(struct block_device *bdev, sector_t block, unsigned size)
{
	struct buffer_head *bh = lookup_bh_lru(bdev, block, size);

	if (bh == NULL) {
		/* __find_get_block_slow will mark the page accessed */
		bh = __find_get_block_slow(bdev, block);
		if (bh)
			bh_lru_install(bh);
	} else
		touch_buffer(bh);

	return bh;
}
EXPORT_SYMBOL(__find_get_block);

/*
 * __getblk_gfp() will locate (and, if necessary, create) the buffer_head
 * which corresponds to the passed block_device, block and size. The
 * returned buffer has its reference count incremented.
 *
 * __getblk_gfp() will lock up the machine if grow_dev_page's
 * try_to_free_buffers() attempt is failing.  FIXME, perhaps?
 */
struct buffer_head *
__getblk_gfp(struct block_device *bdev, sector_t block,
	     unsigned size, gfp_t gfp)
{
	struct buffer_head *bh = __find_get_block(bdev, block, size);

	might_sleep();
	if (bh == NULL)
		bh = __getblk_slow(bdev, block, size, gfp);
	return bh;
}
EXPORT_SYMBOL(__getblk_gfp);

/*
 * Do async read-ahead on a buffer..
 */
void __breadahead(struct block_device *bdev, sector_t block, unsigned size)
{
	struct buffer_head *bh = __getblk(bdev, block, size);
	if (likely(bh)) {
		bh_readahead(bh, REQ_RAHEAD);
		brelse(bh);
	}
}
EXPORT_SYMBOL(__breadahead);

/**
 *  __bread_gfp() - reads a specified block and returns the bh
 *  @bdev: the block_device to read from
 *  @block: number of block
 *  @size: size (in bytes) to read
 *  @gfp: page allocation flag
 *
 *  Reads a specified block, and returns buffer head that contains it.
 *  The page cache can be allocated from non-movable area
 *  not to prevent page migration if you set gfp to zero.
 *  It returns NULL if the block was unreadable.
 */
struct buffer_head *
__bread_gfp(struct block_device *bdev, sector_t block,
		   unsigned size, gfp_t gfp)
{
	struct buffer_head *bh = __getblk_gfp(bdev, block, size, gfp);

	if (likely(bh) && !buffer_uptodate(bh))
		bh = __bread_slow(bh);
	return bh;
}
EXPORT_SYMBOL(__bread_gfp);

static void __invalidate_bh_lrus(struct bh_lru *b)
{
	int i;

	for (i = 0; i < BH_LRU_SIZE; i++) {
		brelse(b->bhs[i]);
		b->bhs[i] = NULL;
	}
}
/*
 * invalidate_bh_lrus() is called rarely - but not only at unmount.
 * This doesn't race because it runs in each cpu either in irq
 * or with preempt disabled.
 */
static void invalidate_bh_lru(void *arg)
{
	struct bh_lru *b = &get_cpu_var(bh_lrus);

	__invalidate_bh_lrus(b);
	put_cpu_var(bh_lrus);
}

bool has_bh_in_lru(int cpu, void *dummy)
{
	struct bh_lru *b = per_cpu_ptr(&bh_lrus, cpu);
	int i;
	
	for (i = 0; i < BH_LRU_SIZE; i++) {
		if (b->bhs[i])
			return true;
	}

	return false;
}

void invalidate_bh_lrus(void)
{
	on_each_cpu_cond(has_bh_in_lru, invalidate_bh_lru, NULL, 1);
}
EXPORT_SYMBOL_GPL(invalidate_bh_lrus);

/*
 * It's called from workqueue context so we need a bh_lru_lock to close
 * the race with preemption/irq.
 */
void invalidate_bh_lrus_cpu(void)
{
	struct bh_lru *b;

	bh_lru_lock();
	b = this_cpu_ptr(&bh_lrus);
	__invalidate_bh_lrus(b);
	bh_lru_unlock();
}

void set_bh_page(struct buffer_head *bh,
		struct page *page, unsigned long offset)
{
	bh->b_page = page;
	BUG_ON(offset >= PAGE_SIZE);
	if (PageHighMem(page))
		/*
		 * This catches illegal uses and preserves the offset:
		 */
		bh->b_data = (char *)(0 + offset);
	else
		bh->b_data = page_address(page) + offset;
}
EXPORT_SYMBOL(set_bh_page);

void folio_set_bh(struct buffer_head *bh, struct folio *folio,
		  unsigned long offset)
{
	bh->b_folio = folio;
	BUG_ON(offset >= folio_size(folio));
	if (folio_test_highmem(folio))
		/*
		 * This catches illegal uses and preserves the offset:
		 */
		bh->b_data = (char *)(0 + offset);
	else
		bh->b_data = folio_address(folio) + offset;
}
EXPORT_SYMBOL(folio_set_bh);

/*
 * Called when truncating a buffer on a page completely.
 */

/* Bits that are cleared during an invalidate */
#define BUFFER_FLAGS_DISCARD \
	(1 << BH_Mapped | 1 << BH_New | 1 << BH_Req | \
	 1 << BH_Delay | 1 << BH_Unwritten)

static void discard_buffer(struct buffer_head * bh)
{
	unsigned long b_state;

	lock_buffer(bh);
	clear_buffer_dirty(bh);
	bh->b_bdev = NULL;
	b_state = READ_ONCE(bh->b_state);
	do {
	} while (!try_cmpxchg(&bh->b_state, &b_state,
			      b_state & ~BUFFER_FLAGS_DISCARD));
	unlock_buffer(bh);
}

/**
 * block_invalidate_folio - Invalidate part or all of a buffer-backed folio.
 * @folio: The folio which is affected.
 * @offset: start of the range to invalidate
 * @length: length of the range to invalidate
 *
 * block_invalidate_folio() is called when all or part of the folio has been
 * invalidated by a truncate operation.
 *
 * block_invalidate_folio() does not have to release all buffers, but it must
 * ensure that no dirty buffer is left outside @offset and that no I/O
 * is underway against any of the blocks which are outside the truncation
 * point.  Because the caller is about to free (and possibly reuse) those
 * blocks on-disk.
 */
void block_invalidate_folio(struct folio *folio, size_t offset, size_t length)
{
	struct buffer_head *head, *bh, *next;
	size_t curr_off = 0;
	size_t stop = length + offset;

	BUG_ON(!folio_test_locked(folio));

	/*
	 * Check for overflow
	 */
	BUG_ON(stop > folio_size(folio) || stop < length);

	head = folio_buffers(folio);
	if (!head)
		return;

	bh = head;
	do {
		size_t next_off = curr_off + bh->b_size;
		next = bh->b_this_page;

		/*
		 * Are we still fully in range ?
		 */
		if (next_off > stop)
			goto out;

		/*
		 * is this block fully invalidated?
		 */
		if (offset <= curr_off)
			discard_buffer(bh);
		curr_off = next_off;
		bh = next;
	} while (bh != head);

	/*
	 * We release buffers only if the entire folio is being invalidated.
	 * The get_block cached value has been unconditionally invalidated,
	 * so real IO is not possible anymore.
	 */
	if (length == folio_size(folio))
		filemap_release_folio(folio, 0);
out:
	return;
}
EXPORT_SYMBOL(block_invalidate_folio);

/*
 * We attach and possibly dirty the buffers atomically wrt
 * block_dirty_folio() via private_lock.  try_to_free_buffers
 * is already excluded via the folio lock.
 */
void folio_create_empty_buffers(struct folio *folio, unsigned long blocksize,
				unsigned long b_state)
{
	struct buffer_head *bh, *head, *tail;

	head = folio_alloc_buffers(folio, blocksize, true);
	bh = head;
	do {
		bh->b_state |= b_state;
		tail = bh;
		bh = bh->b_this_page;
	} while (bh);
	tail->b_this_page = head;

	spin_lock(&folio->mapping->private_lock);
	if (folio_test_uptodate(folio) || folio_test_dirty(folio)) {
		bh = head;
		do {
			if (folio_test_dirty(folio))
				set_buffer_dirty(bh);
			if (folio_test_uptodate(folio))
				set_buffer_uptodate(bh);
			bh = bh->b_this_page;
		} while (bh != head);
	}
	folio_attach_private(folio, head);
	spin_unlock(&folio->mapping->private_lock);
}
EXPORT_SYMBOL(folio_create_empty_buffers);

void create_empty_buffers(struct page *page,
			unsigned long blocksize, unsigned long b_state)
{
	folio_create_empty_buffers(page_folio(page), blocksize, b_state);
}
EXPORT_SYMBOL(create_empty_buffers);

/**
 * clean_bdev_aliases: clean a range of buffers in block device
 * @bdev: Block device to clean buffers in
 * @block: Start of a range of blocks to clean
 * @len: Number of blocks to clean
 *
 * We are taking a range of blocks for data and we don't want writeback of any
 * buffer-cache aliases starting from return from this function and until the
 * moment when something will explicitly mark the buffer dirty (hopefully that
 * will not happen until we will free that block ;-) We don't even need to mark
 * it not-uptodate - nobody can expect anything from a newly allocated buffer
 * anyway. We used to use unmap_buffer() for such invalidation, but that was
 * wrong. We definitely don't want to mark the alias unmapped, for example - it
 * would confuse anyone who might pick it with bread() afterwards...
 *
 * Also..  Note that bforget() doesn't lock the buffer.  So there can be
 * writeout I/O going on against recently-freed buffers.  We don't wait on that
 * I/O in bforget() - it's more efficient to wait on the I/O only if we really
 * need to.  That happens here.
 */
void clean_bdev_aliases(struct block_device *bdev, sector_t block, sector_t len)
{
	struct inode *bd_inode = bdev->bd_inode;
	struct address_space *bd_mapping = bd_inode->i_mapping;
	struct folio_batch fbatch;
	pgoff_t index = block >> (PAGE_SHIFT - bd_inode->i_blkbits);
	pgoff_t end;
	int i, count;
	struct buffer_head *bh;
	struct buffer_head *head;

	end = (block + len - 1) >> (PAGE_SHIFT - bd_inode->i_blkbits);
	folio_batch_init(&fbatch);
	while (filemap_get_folios(bd_mapping, &index, end, &fbatch)) {
		count = folio_batch_count(&fbatch);
		for (i = 0; i < count; i++) {
			struct folio *folio = fbatch.folios[i];

			if (!folio_buffers(folio))
				continue;
			/*
			 * We use folio lock instead of bd_mapping->private_lock
			 * to pin buffers here since we can afford to sleep and
			 * it scales better than a global spinlock lock.
			 */
			folio_lock(folio);
			/* Recheck when the folio is locked which pins bhs */
			head = folio_buffers(folio);
			if (!head)
				goto unlock_page;
			bh = head;
			do {
				if (!buffer_mapped(bh) || (bh->b_blocknr < block))
					goto next;
				if (bh->b_blocknr >= block + len)
					break;
				clear_buffer_dirty(bh);
				wait_on_buffer(bh);
				clear_buffer_req(bh);
next:
				bh = bh->b_this_page;
			} while (bh != head);
unlock_page:
			folio_unlock(folio);
		}
		folio_batch_release(&fbatch);
		cond_resched();
		/* End of range already reached? */
		if (index > end || !index)
			break;
	}
}
EXPORT_SYMBOL(clean_bdev_aliases);

/*
 * Size is a power-of-two in the range 512..PAGE_SIZE,
 * and the case we care about most is PAGE_SIZE.
 *
 * So this *could* possibly be written with those
 * constraints in mind (relevant mostly if some
 * architecture has a slow bit-scan instruction)
 */
static inline int block_size_bits(unsigned int blocksize)
{
	return ilog2(blocksize);
}

static struct buffer_head *folio_create_buffers(struct folio *folio,
						struct inode *inode,
						unsigned int b_state)
{
	BUG_ON(!folio_test_locked(folio));

	if (!folio_buffers(folio))
		folio_create_empty_buffers(folio,
					   1 << READ_ONCE(inode->i_blkbits),
					   b_state);
	return folio_buffers(folio);
}

/*
 * NOTE! All mapped/uptodate combinations are valid:
 *
 *	Mapped	Uptodate	Meaning
 *
 *	No	No		"unknown" - must do get_block()
 *	No	Yes		"hole" - zero-filled
 *	Yes	No		"allocated" - allocated on disk, not read in
 *	Yes	Yes		"valid" - allocated and up-to-date in memory.
 *
 * "Dirty" is valid only with the last case (mapped+uptodate).
 */

/*
 * While block_write_full_page is writing back the dirty buffers under
 * the page lock, whoever dirtied the buffers may decide to clean them
 * again at any time.  We handle that by only looking at the buffer
 * state inside lock_buffer().
 *
 * If block_write_full_page() is called for regular writeback
 * (wbc->sync_mode == WB_SYNC_NONE) then it will redirty a page which has a
 * locked buffer.   This only can happen if someone has written the buffer
 * directly, with submit_bh().  At the address_space level PageWriteback
 * prevents this contention from occurring.
 *
 * If block_write_full_page() is called with wbc->sync_mode ==
 * WB_SYNC_ALL, the writes are posted using REQ_SYNC; this
 * causes the writes to be flagged as synchronous writes.
 */
int __block_write_full_folio(struct inode *inode, struct folio *folio,
			get_block_t *get_block, struct writeback_control *wbc,
			bh_end_io_t *handler)
{
	int err;
	sector_t block;
	sector_t last_block;
	struct buffer_head *bh, *head;
	unsigned int blocksize, bbits;
	int nr_underway = 0;
	blk_opf_t write_flags = wbc_to_write_flags(wbc);

	head = folio_create_buffers(folio, inode,
				    (1 << BH_Dirty) | (1 << BH_Uptodate));

	/*
	 * Be very careful.  We have no exclusion from block_dirty_folio
	 * here, and the (potentially unmapped) buffers may become dirty at
	 * any time.  If a buffer becomes dirty here after we've inspected it
	 * then we just miss that fact, and the folio stays dirty.
	 *
	 * Buffers outside i_size may be dirtied by block_dirty_folio;
	 * handle that here by just cleaning them.
	 */

	bh = head;
	blocksize = bh->b_size;
	bbits = block_size_bits(blocksize);

	block = (sector_t)folio->index << (PAGE_SHIFT - bbits);
	last_block = (i_size_read(inode) - 1) >> bbits;

	/*
	 * Get all the dirty buffers mapped to disk addresses and
	 * handle any aliases from the underlying blockdev's mapping.
	 */
	do {
		if (block > last_block) {
			/*
			 * mapped buffers outside i_size will occur, because
			 * this folio can be outside i_size when there is a
			 * truncate in progress.
			 */
			/*
			 * The buffer was zeroed by block_write_full_page()
			 */
			clear_buffer_dirty(bh);
			set_buffer_uptodate(bh);
		} else if ((!buffer_mapped(bh) || buffer_delay(bh)) &&
			   buffer_dirty(bh)) {
			WARN_ON(bh->b_size != blocksize);
			err = get_block(inode, block, bh, 1);
			if (err)
				goto recover;
			clear_buffer_delay(bh);
			if (buffer_new(bh)) {
				/* blockdev mappings never come here */
				clear_buffer_new(bh);
				clean_bdev_bh_alias(bh);
			}
		}
		bh = bh->b_this_page;
		block++;
	} while (bh != head);

	do {
		if (!buffer_mapped(bh))
			continue;
		/*
		 * If it's a fully non-blocking write attempt and we cannot
		 * lock the buffer then redirty the folio.  Note that this can
		 * potentially cause a busy-wait loop from writeback threads
		 * and kswapd activity, but those code paths have their own
		 * higher-level throttling.
		 */
		if (wbc->sync_mode != WB_SYNC_NONE) {
			lock_buffer(bh);
		} else if (!trylock_buffer(bh)) {
			folio_redirty_for_writepage(wbc, folio);
			continue;
		}
		if (test_clear_buffer_dirty(bh)) {
			mark_buffer_async_write_endio(bh, handler);
		} else {
			unlock_buffer(bh);
		}
	} while ((bh = bh->b_this_page) != head);

	/*
	 * The folio and its buffers are protected by the writeback flag,
	 * so we can drop the bh refcounts early.
	 */
	BUG_ON(folio_test_writeback(folio));
	folio_start_writeback(folio);

	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			submit_bh_wbc(REQ_OP_WRITE | write_flags, bh, wbc);
			nr_underway++;
		}
		bh = next;
	} while (bh != head);
	folio_unlock(folio);

	err = 0;
done:
	if (nr_underway == 0) {
		/*
		 * The folio was marked dirty, but the buffers were
		 * clean.  Someone wrote them back by hand with
		 * write_dirty_buffer/submit_bh.  A rare case.
		 */
		folio_end_writeback(folio);

		/*
		 * The folio and buffer_heads can be released at any time from
		 * here on.
		 */
	}
	return err;

recover:
	/*
	 * ENOSPC, or some other error.  We may already have added some
	 * blocks to the file, so we need to write these out to avoid
	 * exposing stale data.
	 * The folio is currently locked and not marked for writeback
	 */
	bh = head;
	/* Recovery: lock and submit the mapped buffers */
	do {
		if (buffer_mapped(bh) && buffer_dirty(bh) &&
		    !buffer_delay(bh)) {
			lock_buffer(bh);
			mark_buffer_async_write_endio(bh, handler);
		} else {
			/*
			 * The buffer may have been set dirty during
			 * attachment to a dirty folio.
			 */
			clear_buffer_dirty(bh);
		}
	} while ((bh = bh->b_this_page) != head);
	folio_set_error(folio);
	BUG_ON(folio_test_writeback(folio));
	mapping_set_error(folio->mapping, err);
	folio_start_writeback(folio);
	do {
		struct buffer_head *next = bh->b_this_page;
		if (buffer_async_write(bh)) {
			clear_buffer_dirty(bh);
			submit_bh_wbc(REQ_OP_WRITE | write_flags, bh, wbc);
			nr_underway++;
		}
		bh = next;
	} while (bh != head);
	folio_unlock(folio);
	goto done;
}
EXPORT_SYMBOL(__block_write_full_folio);

/*
 * If a folio has any new buffers, zero them out here, and mark them uptodate
 * and dirty so they'll be written out (in order to prevent uninitialised
 * block data from leaking). And clear the new bit.
 */
void folio_zero_new_buffers(struct folio *folio, size_t from, size_t to)
{
	size_t block_start, block_end;
	struct buffer_head *head, *bh;

	BUG_ON(!folio_test_locked(folio));
	head = folio_buffers(folio);
	if (!head)
		return;

	bh = head;
	block_start = 0;
	do {
		block_end = block_start + bh->b_size;

		if (buffer_new(bh)) {
			if (block_end > from && block_start < to) {
				if (!folio_test_uptodate(folio)) {
					size_t start, xend;

					start = max(from, block_start);
					xend = min(to, block_end);

					folio_zero_segment(folio, start, xend);
					set_buffer_uptodate(bh);
				}

				clear_buffer_new(bh);
				mark_buffer_dirty(bh);
			}
		}

		block_start = block_end;
		bh = bh->b_this_page;
	} while (bh != head);
}
EXPORT_SYMBOL(folio_zero_new_buffers);

static void
iomap_to_bh(struct inode *inode, sector_t block, struct buffer_head *bh,
		const struct iomap *iomap)
{
	loff_t offset = block << inode->i_blkbits;

	bh->b_bdev = iomap->bdev;

	/*
	 * Block points to offset in file we need to map, iomap contains
	 * the offset at which the map starts. If the map ends before the
	 * current block, then do not map the buffer and let the caller
	 * handle it.
	 */
	BUG_ON(offset >= iomap->offset + iomap->length);

	switch (iomap->type) {
	case IOMAP_HOLE:
		/*
		 * If the buffer is not up to date or beyond the current EOF,
		 * we need to mark it as new to ensure sub-block zeroing is
		 * executed if necessary.
		 */
		if (!buffer_uptodate(bh) ||
		    (offset >= i_size_read(inode)))
			set_buffer_new(bh);
		break;
	case IOMAP_DELALLOC:
		if (!buffer_uptodate(bh) ||
		    (offset >= i_size_read(inode)))
			set_buffer_new(bh);
		set_buffer_uptodate(bh);
		set_buffer_mapped(bh);
		set_buffer_delay(bh);
		break;
	case IOMAP_UNWRITTEN:
		/*
		 * For unwritten regions, we always need to ensure that regions
		 * in the block we are not writing to are zeroed. Mark the
		 * buffer as new to ensure this.
		 */
		set_buffer_new(bh);
		set_buffer_unwritten(bh);
		fallthrough;
	case IOMAP_MAPPED:
		if ((iomap->flags & IOMAP_F_NEW) ||
		    offset >= i_size_read(inode))
			set_buffer_new(bh);
		bh->b_blocknr = (iomap->addr + offset - iomap->offset) >>
				inode->i_blkbits;
		set_buffer_mapped(bh);
		break;
	}
}

int __block_write_begin_int(struct folio *folio, loff_t pos, unsigned len,
		get_block_t *get_block, const struct iomap *iomap)
{
	unsigned from = pos & (PAGE_SIZE - 1);
	unsigned to = from + len;
	struct inode *inode = folio->mapping->host;
	unsigned block_start, block_end;
	sector_t block;
	int err = 0;
	unsigned blocksize, bbits;
	struct buffer_head *bh, *head, *wait[2], **wait_bh=wait;

	BUG_ON(!folio_test_locked(folio));
	BUG_ON(from > PAGE_SIZE);
	BUG_ON(to > PAGE_SIZE);
	BUG_ON(from > to);

	head = folio_create_buffers(folio, inode, 0);
	blocksize = head->b_size;
	bbits = block_size_bits(blocksize);

	block = (sector_t)folio->index << (PAGE_SHIFT - bbits);

	for(bh = head, block_start = 0; bh != head || !block_start;
	    block++, block_start=block_end, bh = bh->b_this_page) {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (folio_test_uptodate(folio)) {
				if (!buffer_uptodate(bh))
					set_buffer_uptodate(bh);
			}
			continue;
		}
		if (buffer_new(bh))
			clear_buffer_new(bh);
		if (!buffer_mapped(bh)) {
			WARN_ON(bh->b_size != blocksize);
			if (get_block) {
				err = get_block(inode, block, bh, 1);
				if (err)
					break;
			} else {
				iomap_to_bh(inode, block, bh, iomap);
			}

			if (buffer_new(bh)) {
				clean_bdev_bh_alias(bh);
				if (folio_test_uptodate(folio)) {
					clear_buffer_new(bh);
					set_buffer_uptodate(bh);
					mark_buffer_dirty(bh);
					continue;
				}
				if (block_end > to || block_start < from)
					folio_zero_segments(folio,
						to, block_end,
						block_start, from);
				continue;
			}
		}
		if (folio_test_uptodate(folio)) {
			if (!buffer_uptodate(bh))
				set_buffer_uptodate(bh);
			continue; 
		}
		if (!buffer_uptodate(bh) && !buffer_delay(bh) &&
		    !buffer_unwritten(bh) &&
		     (block_start < from || block_end > to)) {
			bh_read_nowait(bh, 0);
			*wait_bh++=bh;
		}
	}
	/*
	 * If we issued read requests - let them complete.
	 */
	while(wait_bh > wait) {
		wait_on_buffer(*--wait_bh);
		if (!buffer_uptodate(*wait_bh))
			err = -EIO;
	}
	if (unlikely(err))
		folio_zero_new_buffers(folio, from, to);
	return err;
}

int __block_write_begin(struct page *page, loff_t pos, unsigned len,
		get_block_t *get_block)
{
	return __block_write_begin_int(page_folio(page), pos, len, get_block,
				       NULL);
}
EXPORT_SYMBOL(__block_write_begin);

static int __block_commit_write(struct inode *inode, struct folio *folio,
		size_t from, size_t to)
{
	size_t block_start, block_end;
	bool partial = false;
	unsigned blocksize;
	struct buffer_head *bh, *head;

	bh = head = folio_buffers(folio);
	blocksize = bh->b_size;

	block_start = 0;
	do {
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = true;
		} else {
			set_buffer_uptodate(bh);
			mark_buffer_dirty(bh);
		}
		if (buffer_new(bh))
			clear_buffer_new(bh);

		block_start = block_end;
		bh = bh->b_this_page;
	} while (bh != head);

	/*
	 * If this is a partial write which happened to make all buffers
	 * uptodate then we can optimize away a bogus read_folio() for
	 * the next read(). Here we 'discover' whether the folio went
	 * uptodate as a result of this (potentially partial) write.
	 */
	if (!partial)
		folio_mark_uptodate(folio);
	return 0;
}

/*
 * block_write_begin takes care of the basic task of block allocation and
 * bringing partial write blocks uptodate first.
 *
 * The filesystem needs to handle block truncation upon failure.
 */
int block_write_begin(struct address_space *mapping, loff_t pos, unsigned len,
		struct page **pagep, get_block_t *get_block)
{
	pgoff_t index = pos >> PAGE_SHIFT;
	struct page *page;
	int status;

	page = grab_cache_page_write_begin(mapping, index);
	if (!page)
		return -ENOMEM;

	status = __block_write_begin(page, pos, len, get_block);
	if (unlikely(status)) {
		unlock_page(page);
		put_page(page);
		page = NULL;
	}

	*pagep = page;
	return status;
}
EXPORT_SYMBOL(block_write_begin);

int block_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct folio *folio = page_folio(page);
	struct inode *inode = mapping->host;
	size_t start = pos - folio_pos(folio);

	if (unlikely(copied < len)) {
		/*
		 * The buffers that were written will now be uptodate, so
		 * we don't have to worry about a read_folio reading them
		 * and overwriting a partial write. However if we have
		 * encountered a short write and only partially written
		 * into a buffer, it will not be marked uptodate, so a
		 * read_folio might come in and destroy our partial write.
		 *
		 * Do the simplest thing, and just treat any short write to a
		 * non uptodate folio as a zero-length write, and force the
		 * caller to redo the whole thing.
		 */
		if (!folio_test_uptodate(folio))
			copied = 0;

		folio_zero_new_buffers(folio, start+copied, start+len);
	}
	flush_dcache_folio(folio);

	/* This could be a short (even 0-length) commit */
	__block_commit_write(inode, folio, start, start + copied);

	return copied;
}
EXPORT_SYMBOL(block_write_end);

int generic_write_end(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len, unsigned copied,
			struct page *page, void *fsdata)
{
	struct inode *inode = mapping->host;
	loff_t old_size = inode->i_size;
	bool i_size_changed = false;

	copied = block_write_end(file, mapping, pos, len, copied, page, fsdata);

	/*
	 * No need to use i_size_read() here, the i_size cannot change under us
	 * because we hold i_rwsem.
	 *
	 * But it's important to update i_size while still holding page lock:
	 * page writeout could otherwise come in and zero beyond i_size.
	 */
	if (pos + copied > inode->i_size) {
		i_size_write(inode, pos + copied);
		i_size_changed = true;
	}

	unlock_page(page);
	put_page(page);

	if (old_size < pos)
		pagecache_isize_extended(inode, old_size, pos);
	/*
	 * Don't mark the inode dirty under page lock. First, it unnecessarily
	 * makes the holding time of page lock longer. Second, it forces lock
	 * ordering of page lock and transaction start for journaling
	 * filesystems.
	 */
	if (i_size_changed)
		mark_inode_dirty(inode);
	return copied;
}
EXPORT_SYMBOL(generic_write_end);

/*
 * block_is_partially_uptodate checks whether buffers within a folio are
 * uptodate or not.
 *
 * Returns true if all buffers which correspond to the specified part
 * of the folio are uptodate.
 */
bool block_is_partially_uptodate(struct folio *folio, size_t from, size_t count)
{
	unsigned block_start, block_end, blocksize;
	unsigned to;
	struct buffer_head *bh, *head;
	bool ret = true;

	head = folio_buffers(folio);
	if (!head)
		return false;
	blocksize = head->b_size;
	to = min_t(unsigned, folio_size(folio) - from, count);
	to = from + to;
	if (from < blocksize && to > folio_size(folio) - blocksize)
		return false;

	bh = head;
	block_start = 0;
	do {
		block_end = block_start + blocksize;
		if (block_end > from && block_start < to) {
			if (!buffer_uptodate(bh)) {
				ret = false;
				break;
			}
			if (block_end >= to)
				break;
		}
		block_start = block_end;
		bh = bh->b_this_page;
	} while (bh != head);

	return ret;
}
EXPORT_SYMBOL(block_is_partially_uptodate);

/*
 * Generic "read_folio" function for block devices that have the normal
 * get_block functionality. This is most of the block device filesystems.
 * Reads the folio asynchronously --- the unlock_buffer() and
 * set/clear_buffer_uptodate() functions propagate buffer state into the
 * folio once IO has completed.
 */
int block_read_full_folio(struct folio *folio, get_block_t *get_block)
{
	struct inode *inode = folio->mapping->host;
	sector_t iblock, lblock;
	struct buffer_head *bh, *head, *arr[MAX_BUF_PER_PAGE];
	unsigned int blocksize, bbits;
	int nr, i;
	int fully_mapped = 1;
	bool page_error = false;
	loff_t limit = i_size_read(inode);

	/* This is needed for ext4. */
	if (IS_ENABLED(CONFIG_FS_VERITY) && IS_VERITY(inode))
		limit = inode->i_sb->s_maxbytes;

	VM_BUG_ON_FOLIO(folio_test_large(folio), folio);

	head = folio_create_buffers(folio, inode, 0);
	blocksize = head->b_size;
	bbits = block_size_bits(blocksize);

	iblock = (sector_t)folio->index << (PAGE_SHIFT - bbits);
	lblock = (limit+blocksize-1) >> bbits;
	bh = head;
	nr = 0;
	i = 0;

	do {
		if (buffer_uptodate(bh))
			continue;

		if (!buffer_mapped(bh)) {
			int err = 0;

			fully_mapped = 0;
			if (iblock < lblock) {
				WARN_ON(bh->b_size != blocksize);
				err = get_block(inode, iblock, bh, 0);
				if (err) {
					folio_set_error(folio);
					page_error = true;
				}
			}
			if (!buffer_mapped(bh)) {
				folio_zero_range(folio, i * blocksize,
						blocksize);
				if (!err)
					set_buffer_uptodate(bh);
				continue;
			}
			/*
			 * get_block() might have updated the buffer
			 * synchronously
			 */
			if (buffer_uptodate(bh))
				continue;
		}
		arr[nr++] = bh;
	} while (i++, iblock++, (bh = bh->b_this_page) != head);

	if (fully_mapped)
		folio_set_mappedtodisk(folio);

	if (!nr) {
		/*
		 * All buffers are uptodate - we can set the folio uptodate
		 * as well. But not if get_block() returned an error.
		 */
		if (!page_error)
			folio_mark_uptodate(folio);
		folio_unlock(folio);
		return 0;
	}

	/* Stage two: lock the buffers */
	for (i = 0; i < nr; i++) {
		bh = arr[i];
		lock_buffer(bh);
		mark_buffer_async_read(bh);
	}

	/*
	 * Stage 3: start the IO.  Check for uptodateness
	 * inside the buffer lock in case another process reading
	 * the underlying blockdev brought it uptodate (the sct fix).
	 */
	for (i = 0; i < nr; i++) {
		bh = arr[i];
		if (buffer_uptodate(bh))
			end_buffer_async_read(bh, 1);
		else
			submit_bh(REQ_OP_READ, bh);
	}
	return 0;
}
EXPORT_SYMBOL(block_read_full_folio);

/* utility function for filesystems that need to do work on expanding
 * truncates.  Uses filesystem pagecache writes to allow the filesystem to
 * deal with the hole.  
 */
int generic_cont_expand_simple(struct inode *inode, loff_t size)
{
	struct address_space *mapping = inode->i_mapping;
	const struct address_space_operations *aops = mapping->a_ops;
	struct page *page;
	void *fsdata = NULL;
	int err;

	err = inode_newsize_ok(inode, size);
	if (err)
		goto out;

	err = aops->write_begin(NULL, mapping, size, 0, &page, &fsdata);
	if (err)
		goto out;

	err = aops->write_end(NULL, mapping, size, 0, 0, page, fsdata);
	BUG_ON(err > 0);

out:
	return err;
}
EXPORT_SYMBOL(generic_cont_expand_simple);

static int cont_expand_zero(struct file *file, struct address_space *mapping,
			    loff_t pos, loff_t *bytes)
{
	struct inode *inode = mapping->host;
	const struct address_space_operations *aops = mapping->a_ops;
	unsigned int blocksize = i_blocksize(inode);
	struct page *page;
	void *fsdata = NULL;
	pgoff_t index, curidx;
	loff_t curpos;
	unsigned zerofrom, offset, len;
	int err = 0;

	index = pos >> PAGE_SHIFT;
	offset = pos & ~PAGE_MASK;

	while (index > (curidx = (curpos = *bytes)>>PAGE_SHIFT)) {
		zerofrom = curpos & ~PAGE_MASK;
		if (zerofrom & (blocksize-1)) {
			*bytes |= (blocksize-1);
			(*bytes)++;
		}
		len = PAGE_SIZE - zerofrom;

		err = aops->write_begin(file, mapping, curpos, len,
					    &page, &fsdata);
		if (err)
			goto out;
		zero_user(page, zerofrom, len);
		err = aops->write_end(file, mapping, curpos, len, len,
						page, fsdata);
		if (err < 0)
			goto out;
		BUG_ON(err != len);
		err = 0;

		balance_dirty_pages_ratelimited(mapping);

		if (fatal_signal_pending(current)) {
			err = -EINTR;
			goto out;
		}
	}

	/* page covers the boundary, find the boundary offset */
	if (index == curidx) {
		zerofrom = curpos & ~PAGE_MASK;
		/* if we will expand the thing last block will be filled */
		if (offset <= zerofrom) {
			goto out;
		}
		if (zerofrom & (blocksize-1)) {
			*bytes |= (blocksize-1);
			(*bytes)++;
		}
		len = offset - zerofrom;

		err = aops->write_begin(file, mapping, curpos, len,
					    &page, &fsdata);
		if (err)
			goto out;
		zero_user(page, zerofrom, len);
		err = aops->write_end(file, mapping, curpos, len, len,
						page, fsdata);
		if (err < 0)
			goto out;
		BUG_ON(err != len);
		err = 0;
	}
out:
	return err;
}

/*
 * For moronic filesystems that do not allow holes in file.
 * We may have to extend the file.
 */
int cont_write_begin(struct file *file, struct address_space *mapping,
			loff_t pos, unsigned len,
			struct page **pagep, void **fsdata,
			get_block_t *get_block, loff_t *bytes)
{
	struct inode *inode = mapping->host;
	unsigned int blocksize = i_blocksize(inode);
	unsigned int zerofrom;
	int err;

	err = cont_expand_zero(file, mapping, pos, bytes);
	if (err)
		return err;

	zerofrom = *bytes & ~PAGE_MASK;
	if (pos+len > *bytes && zerofrom & (blocksize-1)) {
		*bytes |= (blocksize-1);
		(*bytes)++;
	}

	return block_write_begin(mapping, pos, len, pagep, get_block);
}
EXPORT_SYMBOL(cont_write_begin);

int block_commit_write(struct page *page, unsigned from, unsigned to)
{
	struct folio *folio = page_folio(page);
	struct inode *inode = folio->mapping->host;
	__block_commit_write(inode, folio, from, to);
	return 0;
}
EXPORT_SYMBOL(block_commit_write);

/*
 * block_page_mkwrite() is not allowed to change the file size as it gets
 * called from a page fault handler when a page is first dirtied. Hence we must
 * be careful to check for EOF conditions here. We set the page up correctly
 * for a written page which means we get ENOSPC checking when writing into
 * holes and correct delalloc and unwritten extent mapping on filesystems that
 * support these features.
 *
 * We are not allowed to take the i_mutex here so we have to play games to
 * protect against truncate races as the page could now be beyond EOF.  Because
 * truncate writes the inode size before removing pages, once we have the
 * page lock we can determine safely if the page is beyond EOF. If it is not
 * beyond EOF, then the page is guaranteed safe against truncation until we
 * unlock the page.
 *
 * Direct callers of this function should protect against filesystem freezing
 * using sb_start_pagefault() - sb_end_pagefault() functions.
 */
int block_page_mkwrite(struct vm_area_struct *vma, struct vm_fault *vmf,
			 get_block_t get_block)
{
	struct folio *folio = page_folio(vmf->page);
	struct inode *inode = file_inode(vma->vm_file);
	unsigned long end;
	loff_t size;
	int ret;

	folio_lock(folio);
	size = i_size_read(inode);
	if ((folio->mapping != inode->i_mapping) ||
	    (folio_pos(folio) >= size)) {
		/* We overload EFAULT to mean page got truncated */
		ret = -EFAULT;
		goto out_unlock;
	}

	end = folio_size(folio);
	/* folio is wholly or partially inside EOF */
	if (folio_pos(folio) + end > size)
		end = size - folio_pos(folio);

	ret = __block_write_begin_int(folio, 0, end, get_block, NULL);
	if (!ret)
		ret = __block_commit_write(inode, folio, 0, end);

	if (unlikely(ret < 0))
		goto out_unlock;
	folio_mark_dirty(folio);
	folio_wait_stable(folio);
	return 0;
out_unlock:
	folio_unlock(folio);
	return ret;
}
EXPORT_SYMBOL(block_page_mkwrite);

int block_truncate_page(struct address_space *mapping,
			loff_t from, get_block_t *get_block)
{
	pgoff_t index = from >> PAGE_SHIFT;
	unsigned blocksize;
	sector_t iblock;
	size_t offset, length, pos;
	struct inode *inode = mapping->host;
	struct folio *folio;
	struct buffer_head *bh;
	int err = 0;

	blocksize = i_blocksize(inode);
	length = from & (blocksize - 1);

	/* Block boundary? Nothing to do */
	if (!length)
		return 0;

	length = blocksize - length;
	iblock = (sector_t)index << (PAGE_SHIFT - inode->i_blkbits);
	
	folio = filemap_grab_folio(mapping, index);
	if (IS_ERR(folio))
		return PTR_ERR(folio);

	bh = folio_buffers(folio);
	if (!bh) {
		folio_create_empty_buffers(folio, blocksize, 0);
		bh = folio_buffers(folio);
	}

	/* Find the buffer that contains "offset" */
	offset = offset_in_folio(folio, from);
	pos = blocksize;
	while (offset >= pos) {
		bh = bh->b_this_page;
		iblock++;
		pos += blocksize;
	}

	if (!buffer_mapped(bh)) {
		WARN_ON(bh->b_size != blocksize);
		err = get_block(inode, iblock, bh, 0);
		if (err)
			goto unlock;
		/* unmapped? It's a hole - nothing to do */
		if (!buffer_mapped(bh))
			goto unlock;
	}

	/* Ok, it's mapped. Make sure it's up-to-date */
	if (folio_test_uptodate(folio))
		set_buffer_uptodate(bh);

	if (!buffer_uptodate(bh) && !buffer_delay(bh) && !buffer_unwritten(bh)) {
		err = bh_read(bh, 0);
		/* Uhhuh. Read error. Complain and punt. */
		if (err < 0)
			goto unlock;
	}

	folio_zero_range(folio, offset, length);
	mark_buffer_dirty(bh);

unlock:
	folio_unlock(folio);
	folio_put(folio);

	return err;
}
EXPORT_SYMBOL(block_truncate_page);

/*
 * The generic ->writepage function for buffer-backed address_spaces
 */
int block_write_full_page(struct page *page, get_block_t *get_block,
			struct writeback_control *wbc)
{
	struct folio *folio = page_folio(page);
	struct inode * const inode = folio->mapping->host;
	loff_t i_size = i_size_read(inode);

	/* Is the folio fully inside i_size? */
	if (folio_pos(folio) + folio_size(folio) <= i_size)
		return __block_write_full_folio(inode, folio, get_block, wbc,
					       end_buffer_async_write);

	/* Is the folio fully outside i_size? (truncate in progress) */
	if (folio_pos(folio) >= i_size) {
		folio_unlock(folio);
		return 0; /* don't care */
	}

	/*
	 * The folio straddles i_size.  It must be zeroed out on each and every
	 * writepage invocation because it may be mmapped.  "A file is mapped
	 * in multiples of the page size.  For a file that is not a multiple of
	 * the page size, the remaining memory is zeroed when mapped, and
	 * writes to that region are not written out to the file."
	 */
	folio_zero_segment(folio, offset_in_folio(folio, i_size),
			folio_size(folio));
	return __block_write_full_folio(inode, folio, get_block, wbc,
			end_buffer_async_write);
}
EXPORT_SYMBOL(block_write_full_page);

sector_t generic_block_bmap(struct address_space *mapping, sector_t block,
			    get_block_t *get_block)
{
	struct inode *inode = mapping->host;
	struct buffer_head tmp = {
		.b_size = i_blocksize(inode),
	};

	get_block(inode, block, &tmp, 0);
	return tmp.b_blocknr;
}
EXPORT_SYMBOL(generic_block_bmap);

static void end_bio_bh_io_sync(struct bio *bio)
{
	struct buffer_head *bh = bio->bi_private;

	if (unlikely(bio_flagged(bio, BIO_QUIET)))
		set_bit(BH_Quiet, &bh->b_state);

	bh->b_end_io(bh, !bio->bi_status);
	bio_put(bio);
}

static void submit_bh_wbc(blk_opf_t opf, struct buffer_head *bh,
			  struct writeback_control *wbc)
{
	const enum req_op op = opf & REQ_OP_MASK;
	struct bio *bio;

	BUG_ON(!buffer_locked(bh));
	BUG_ON(!buffer_mapped(bh));
	BUG_ON(!bh->b_end_io);
	BUG_ON(buffer_delay(bh));
	BUG_ON(buffer_unwritten(bh));

	/*
	 * Only clear out a write error when rewriting
	 */
	if (test_set_buffer_req(bh) && (op == REQ_OP_WRITE))
		clear_buffer_write_io_error(bh);

	if (buffer_meta(bh))
		opf |= REQ_META;
	if (buffer_prio(bh))
		opf |= REQ_PRIO;

	bio = bio_alloc(bh->b_bdev, 1, opf, GFP_NOIO);

	fscrypt_set_bio_crypt_ctx_bh(bio, bh, GFP_NOIO);

	bio->bi_iter.bi_sector = bh->b_blocknr * (bh->b_size >> 9);

	__bio_add_page(bio, bh->b_page, bh->b_size, bh_offset(bh));

	bio->bi_end_io = end_bio_bh_io_sync;
	bio->bi_private = bh;

	/* Take care of bh's that straddle the end of the device */
	guard_bio_eod(bio);

	if (wbc) {
		wbc_init_bio(wbc, bio);
		wbc_account_cgroup_owner(wbc, bh->b_page, bh->b_size);
	}

	submit_bio(bio);
}

void submit_bh(blk_opf_t opf, struct buffer_head *bh)
{
	submit_bh_wbc(opf, bh, NULL);
}
EXPORT_SYMBOL(submit_bh);

void write_dirty_buffer(struct buffer_head *bh, blk_opf_t op_flags)
{
	lock_buffer(bh);
	if (!test_clear_buffer_dirty(bh)) {
		unlock_buffer(bh);
		return;
	}
	bh->b_end_io = end_buffer_write_sync;
	get_bh(bh);
	submit_bh(REQ_OP_WRITE | op_flags, bh);
}
EXPORT_SYMBOL(write_dirty_buffer);

/*
 * For a data-integrity writeout, we need to wait upon any in-progress I/O
 * and then start new I/O and then wait upon it.  The caller must have a ref on
 * the buffer_head.
 */
int __sync_dirty_buffer(struct buffer_head *bh, blk_opf_t op_flags)
{
	WARN_ON(atomic_read(&bh->b_count) < 1);
	lock_buffer(bh);
	if (test_clear_buffer_dirty(bh)) {
		/*
		 * The bh should be mapped, but it might not be if the
		 * device was hot-removed. Not much we can do but fail the I/O.
		 */
		if (!buffer_mapped(bh)) {
			unlock_buffer(bh);
			return -EIO;
		}

		get_bh(bh);
		bh->b_end_io = end_buffer_write_sync;
		submit_bh(REQ_OP_WRITE | op_flags, bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			return -EIO;
	} else {
		unlock_buffer(bh);
	}
	return 0;
}
EXPORT_SYMBOL(__sync_dirty_buffer);

int sync_dirty_buffer(struct buffer_head *bh)
{
	return __sync_dirty_buffer(bh, REQ_SYNC);
}
EXPORT_SYMBOL(sync_dirty_buffer);

/*
 * try_to_free_buffers() checks if all the buffers on this particular folio
 * are unused, and releases them if so.
 *
 * Exclusion against try_to_free_buffers may be obtained by either
 * locking the folio or by holding its mapping's private_lock.
 *
 * If the folio is dirty but all the buffers are clean then we need to
 * be sure to mark the folio clean as well.  This is because the folio
 * may be against a block device, and a later reattachment of buffers
 * to a dirty folio will set *all* buffers dirty.  Which would corrupt
 * filesystem data on the same device.
 *
 * The same applies to regular filesystem folios: if all the buffers are
 * clean then we set the folio clean and proceed.  To do that, we require
 * total exclusion from block_dirty_folio().  That is obtained with
 * private_lock.
 *
 * try_to_free_buffers() is non-blocking.
 */
static inline int buffer_busy(struct buffer_head *bh)
{
	return atomic_read(&bh->b_count) |
		(bh->b_state & ((1 << BH_Dirty) | (1 << BH_Lock)));
}

static bool
drop_buffers(struct folio *folio, struct buffer_head **buffers_to_free)
{
	struct buffer_head *head = folio_buffers(folio);
	struct buffer_head *bh;

	bh = head;
	do {
		if (buffer_busy(bh))
			goto failed;
		bh = bh->b_this_page;
	} while (bh != head);

	do {
		struct buffer_head *next = bh->b_this_page;

		if (bh->b_assoc_map)
			__remove_assoc_queue(bh);
		bh = next;
	} while (bh != head);
	*buffers_to_free = head;
	folio_detach_private(folio);
	return true;
failed:
	return false;
}

bool try_to_free_buffers(struct folio *folio)
{
	struct address_space * const mapping = folio->mapping;
	struct buffer_head *buffers_to_free = NULL;
	bool ret = 0;

	BUG_ON(!folio_test_locked(folio));
	if (folio_test_writeback(folio))
		return false;

	if (mapping == NULL) {		/* can this still happen? */
		ret = drop_buffers(folio, &buffers_to_free);
		goto out;
	}

	spin_lock(&mapping->private_lock);
	ret = drop_buffers(folio, &buffers_to_free);

	/*
	 * If the filesystem writes its buffers by hand (eg ext3)
	 * then we can have clean buffers against a dirty folio.  We
	 * clean the folio here; otherwise the VM will never notice
	 * that the filesystem did any IO at all.
	 *
	 * Also, during truncate, discard_buffer will have marked all
	 * the folio's buffers clean.  We discover that here and clean
	 * the folio also.
	 *
	 * private_lock must be held over this entire operation in order
	 * to synchronise against block_dirty_folio and prevent the
	 * dirty bit from being lost.
	 */
	if (ret)
		folio_cancel_dirty(folio);
	spin_unlock(&mapping->private_lock);
out:
	if (buffers_to_free) {
		struct buffer_head *bh = buffers_to_free;

		do {
			struct buffer_head *next = bh->b_this_page;
			free_buffer_head(bh);
			bh = next;
		} while (bh != buffers_to_free);
	}
	return ret;
}
EXPORT_SYMBOL(try_to_free_buffers);

/*
 * Buffer-head allocation
 */
static struct kmem_cache *bh_cachep __read_mostly;

/*
 * Once the number of bh's in the machine exceeds this level, we start
 * stripping them in writeback.
 */
static unsigned long max_buffer_heads;

int buffer_heads_over_limit;

struct bh_accounting {
	int nr;			/* Number of live bh's */
	int ratelimit;		/* Limit cacheline bouncing */
};

static DEFINE_PER_CPU(struct bh_accounting, bh_accounting) = {0, 0};

static void recalc_bh_state(void)
{
	int i;
	int tot = 0;

	if (__this_cpu_inc_return(bh_accounting.ratelimit) - 1 < 4096)
		return;
	__this_cpu_write(bh_accounting.ratelimit, 0);
	for_each_online_cpu(i)
		tot += per_cpu(bh_accounting, i).nr;
	buffer_heads_over_limit = (tot > max_buffer_heads);
}

struct buffer_head *alloc_buffer_head(gfp_t gfp_flags)
{
	struct buffer_head *ret = kmem_cache_zalloc(bh_cachep, gfp_flags);
	if (ret) {
		INIT_LIST_HEAD(&ret->b_assoc_buffers);
		spin_lock_init(&ret->b_uptodate_lock);
		preempt_disable();
		__this_cpu_inc(bh_accounting.nr);
		recalc_bh_state();
		preempt_enable();
	}
	return ret;
}
EXPORT_SYMBOL(alloc_buffer_head);

void free_buffer_head(struct buffer_head *bh)
{
	BUG_ON(!list_empty(&bh->b_assoc_buffers));
	kmem_cache_free(bh_cachep, bh);
	preempt_disable();
	__this_cpu_dec(bh_accounting.nr);
	recalc_bh_state();
	preempt_enable();
}
EXPORT_SYMBOL(free_buffer_head);

static int buffer_exit_cpu_dead(unsigned int cpu)
{
	int i;
	struct bh_lru *b = &per_cpu(bh_lrus, cpu);

	for (i = 0; i < BH_LRU_SIZE; i++) {
		brelse(b->bhs[i]);
		b->bhs[i] = NULL;
	}
	this_cpu_add(bh_accounting.nr, per_cpu(bh_accounting, cpu).nr);
	per_cpu(bh_accounting, cpu).nr = 0;
	return 0;
}

/**
 * bh_uptodate_or_lock - Test whether the buffer is uptodate
 * @bh: struct buffer_head
 *
 * Return true if the buffer is up-to-date and false,
 * with the buffer locked, if not.
 */
int bh_uptodate_or_lock(struct buffer_head *bh)
{
	if (!buffer_uptodate(bh)) {
		lock_buffer(bh);
		if (!buffer_uptodate(bh))
			return 0;
		unlock_buffer(bh);
	}
	return 1;
}
EXPORT_SYMBOL(bh_uptodate_or_lock);

/**
 * __bh_read - Submit read for a locked buffer
 * @bh: struct buffer_head
 * @op_flags: appending REQ_OP_* flags besides REQ_OP_READ
 * @wait: wait until reading finish
 *
 * Returns zero on success or don't wait, and -EIO on error.
 */
int __bh_read(struct buffer_head *bh, blk_opf_t op_flags, bool wait)
{
	int ret = 0;

	BUG_ON(!buffer_locked(bh));

	get_bh(bh);
	bh->b_end_io = end_buffer_read_sync;
	submit_bh(REQ_OP_READ | op_flags, bh);
	if (wait) {
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			ret = -EIO;
	}
	return ret;
}
EXPORT_SYMBOL(__bh_read);

/**
 * __bh_read_batch - Submit read for a batch of unlocked buffers
 * @nr: entry number of the buffer batch
 * @bhs: a batch of struct buffer_head
 * @op_flags: appending REQ_OP_* flags besides REQ_OP_READ
 * @force_lock: force to get a lock on the buffer if set, otherwise drops any
 *              buffer that cannot lock.
 *
 * Returns zero on success or don't wait, and -EIO on error.
 */
void __bh_read_batch(int nr, struct buffer_head *bhs[],
		     blk_opf_t op_flags, bool force_lock)
{
	int i;

	for (i = 0; i < nr; i++) {
		struct buffer_head *bh = bhs[i];

		if (buffer_uptodate(bh))
			continue;

		if (force_lock)
			lock_buffer(bh);
		else
			if (!trylock_buffer(bh))
				continue;

		if (buffer_uptodate(bh)) {
			unlock_buffer(bh);
			continue;
		}

		bh->b_end_io = end_buffer_read_sync;
		get_bh(bh);
		submit_bh(REQ_OP_READ | op_flags, bh);
	}
}
EXPORT_SYMBOL(__bh_read_batch);

void __init buffer_init(void)
{
	unsigned long nrpages;
	int ret;

	bh_cachep = kmem_cache_create("buffer_head",
			sizeof(struct buffer_head), 0,
				(SLAB_RECLAIM_ACCOUNT|SLAB_PANIC|
				SLAB_MEM_SPREAD),
				NULL);

	/*
	 * Limit the bh occupancy to 10% of ZONE_NORMAL
	 */
	nrpages = (nr_free_buffer_pages() * 10) / 100;
	max_buffer_heads = nrpages * (PAGE_SIZE / sizeof(struct buffer_head));
	ret = cpuhp_setup_state_nocalls(CPUHP_FS_BUFF_DEAD, "fs/buffer:dead",
					NULL, buffer_exit_cpu_dead);
	WARN_ON(ret < 0);
}
