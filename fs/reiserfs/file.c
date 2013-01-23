/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/time.h>
#include "reiserfs.h"
#include "acl.h"
#include "xattr.h"
#include <asm/uaccess.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/quotaops.h>

/*
** We pack the tails of files on file close, not at the time they are written.
** This implies an unnecessary copy of the tail and an unnecessary indirect item
** insertion/balancing, for files that are written in one write.
** It avoids unnecessary tail packings (balances) for files that are written in
** multiple writes and are small enough to have tails.
**
** file_release is called by the VFS layer when the file is closed.  If
** this is the last open file descriptor, and the file
** small enough to have a tail, and the tail is currently in an
** unformatted node, the tail is converted back into a direct item.
**
** We use reiserfs_truncate_file to pack the tail, since it already has
** all the conditions coded.
*/
static int reiserfs_file_release(struct inode *inode, struct file *filp)
{

	struct reiserfs_transaction_handle th;
	int err;
	int jbegin_failure = 0;

	BUG_ON(!S_ISREG(inode->i_mode));

        if (atomic_add_unless(&REISERFS_I(inode)->openers, -1, 1))
		return 0;

	mutex_lock(&(REISERFS_I(inode)->tailpack));

        if (!atomic_dec_and_test(&REISERFS_I(inode)->openers)) {
		mutex_unlock(&(REISERFS_I(inode)->tailpack));
		return 0;
	}

	/* fast out for when nothing needs to be done */
	if ((!(REISERFS_I(inode)->i_flags & i_pack_on_close_mask) ||
	     !tail_has_to_be_packed(inode)) &&
	    REISERFS_I(inode)->i_prealloc_count <= 0) {
		mutex_unlock(&(REISERFS_I(inode)->tailpack));
		return 0;
	}

	reiserfs_write_lock(inode->i_sb);
	/* freeing preallocation only involves relogging blocks that
	 * are already in the current transaction.  preallocation gets
	 * freed at the end of each transaction, so it is impossible for
	 * us to log any additional blocks (including quota blocks)
	 */
	err = journal_begin(&th, inode->i_sb, 1);
	if (err) {
		/* uh oh, we can't allow the inode to go away while there
		 * is still preallocation blocks pending.  Try to join the
		 * aborted transaction
		 */
		jbegin_failure = err;
		err = journal_join_abort(&th, inode->i_sb, 1);

		if (err) {
			/* hmpf, our choices here aren't good.  We can pin the inode
			 * which will disallow unmount from every happening, we can
			 * do nothing, which will corrupt random memory on unmount,
			 * or we can forcibly remove the file from the preallocation
			 * list, which will leak blocks on disk.  Lets pin the inode
			 * and let the admin know what is going on.
			 */
			igrab(inode);
			reiserfs_warning(inode->i_sb, "clm-9001",
					 "pinning inode %lu because the "
					 "preallocation can't be freed",
					 inode->i_ino);
			goto out;
		}
	}
	reiserfs_update_inode_transaction(inode);

#ifdef REISERFS_PREALLOCATE
	reiserfs_discard_prealloc(&th, inode);
#endif
	err = journal_end(&th, inode->i_sb, 1);

	/* copy back the error code from journal_begin */
	if (!err)
		err = jbegin_failure;

	if (!err &&
	    (REISERFS_I(inode)->i_flags & i_pack_on_close_mask) &&
	    tail_has_to_be_packed(inode)) {

		/* if regular file is released by last holder and it has been
		   appended (we append by unformatted node only) or its direct
		   item(s) had to be converted, then it may have to be
		   indirect2direct converted */
		err = reiserfs_truncate_file(inode, 0);
	}
      out:
	reiserfs_write_unlock(inode->i_sb);
	mutex_unlock(&(REISERFS_I(inode)->tailpack));
	return err;
}

static int reiserfs_file_open(struct inode *inode, struct file *file)
{
	int err = dquot_file_open(inode, file);
        if (!atomic_inc_not_zero(&REISERFS_I(inode)->openers)) {
		/* somebody might be tailpacking on final close; wait for it */
		mutex_lock(&(REISERFS_I(inode)->tailpack));
		atomic_inc(&REISERFS_I(inode)->openers);
		mutex_unlock(&(REISERFS_I(inode)->tailpack));
	}
	return err;
}

void reiserfs_vfs_truncate_file(struct inode *inode)
{
	mutex_lock(&(REISERFS_I(inode)->tailpack));
	reiserfs_truncate_file(inode, 1);
	mutex_unlock(&(REISERFS_I(inode)->tailpack));
}

/* Sync a reiserfs file. */

/*
 * FIXME: sync_mapping_buffers() never has anything to sync.  Can
 * be removed...
 */

static int reiserfs_sync_file(struct file *filp, loff_t start, loff_t end,
			      int datasync)
{
	struct inode *inode = filp->f_mapping->host;
	int err;
	int barrier_done;

	err = filemap_write_and_wait_range(inode->i_mapping, start, end);
	if (err)
		return err;

	mutex_lock(&inode->i_mutex);
	BUG_ON(!S_ISREG(inode->i_mode));
	err = sync_mapping_buffers(inode->i_mapping);
	reiserfs_write_lock(inode->i_sb);
	barrier_done = reiserfs_commit_for_inode(inode);
	reiserfs_write_unlock(inode->i_sb);
	if (barrier_done != 1 && reiserfs_barrier_flush(inode->i_sb))
		blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);
	mutex_unlock(&inode->i_mutex);
	if (barrier_done < 0)
		return barrier_done;
	return (err < 0) ? -EIO : 0;
}

/* taken fs/buffer.c:__block_commit_write */
int reiserfs_commit_page(struct inode *inode, struct page *page,
			 unsigned from, unsigned to)
{
	unsigned block_start, block_end;
	int partial = 0;
	unsigned blocksize;
	struct buffer_head *bh, *head;
	unsigned long i_size_index = inode->i_size >> PAGE_CACHE_SHIFT;
	int new;
	int logit = reiserfs_file_data_log(inode);
	struct super_block *s = inode->i_sb;
	int bh_per_page = PAGE_CACHE_SIZE / s->s_blocksize;
	struct reiserfs_transaction_handle th;
	int ret = 0;

	th.t_trans_id = 0;
	blocksize = 1 << inode->i_blkbits;

	if (logit) {
		reiserfs_write_lock(s);
		ret = journal_begin(&th, s, bh_per_page + 1);
		if (ret)
			goto drop_write_lock;
		reiserfs_update_inode_transaction(inode);
	}
	for (bh = head = page_buffers(page), block_start = 0;
	     bh != head || !block_start;
	     block_start = block_end, bh = bh->b_this_page) {

		new = buffer_new(bh);
		clear_buffer_new(bh);
		block_end = block_start + blocksize;
		if (block_end <= from || block_start >= to) {
			if (!buffer_uptodate(bh))
				partial = 1;
		} else {
			set_buffer_uptodate(bh);
			if (logit) {
				reiserfs_prepare_for_journal(s, bh, 1);
				journal_mark_dirty(&th, s, bh);
			} else if (!buffer_dirty(bh)) {
				mark_buffer_dirty(bh);
				/* do data=ordered on any page past the end
				 * of file and any buffer marked BH_New.
				 */
				if (reiserfs_data_ordered(inode->i_sb) &&
				    (new || page->index >= i_size_index)) {
					reiserfs_add_ordered_list(inode, bh);
				}
			}
		}
	}
	if (logit) {
		ret = journal_end(&th, s, bh_per_page + 1);
	      drop_write_lock:
		reiserfs_write_unlock(s);
	}
	/*
	 * If this is a partial write which happened to make all buffers
	 * uptodate then we can optimize away a bogus readpage() for
	 * the next read(). Here we 'discover' whether the page went
	 * uptodate as a result of this (potentially partial) write.
	 */
	if (!partial)
		SetPageUptodate(page);
	return ret;
}

/* Write @count bytes at position @ppos in a file indicated by @file
   from the buffer @buf.

   generic_file_write() is only appropriate for filesystems that are not seeking to optimize performance and want
   something simple that works.  It is not for serious use by general purpose filesystems, excepting the one that it was
   written for (ext2/3).  This is for several reasons:

   * It has no understanding of any filesystem specific optimizations.

   * It enters the filesystem repeatedly for each page that is written.

   * It depends on reiserfs_get_block() function which if implemented by reiserfs performs costly search_by_key
   * operation for each page it is supplied with. By contrast reiserfs_file_write() feeds as much as possible at a time
   * to reiserfs which allows for fewer tree traversals.

   * Each indirect pointer insertion takes a lot of cpu, because it involves memory moves inside of blocks.

   * Asking the block allocation code for blocks one at a time is slightly less efficient.

   All of these reasons for not using only generic file write were understood back when reiserfs was first miscoded to
   use it, but we were in a hurry to make code freeze, and so it couldn't be revised then.  This new code should make
   things right finally.

   Future Features: providing search_by_key with hints.

*/
static ssize_t reiserfs_file_write(struct file *file,	/* the file we are going to write into */
				   const char __user * buf,	/*  pointer to user supplied data
								   (in userspace) */
				   size_t count,	/* amount of bytes to write */
				   loff_t * ppos	/* pointer to position in file that we start writing at. Should be updated to
							 * new current position before returning. */
				   )
{
	struct inode *inode = file->f_path.dentry->d_inode;	// Inode of the file that we are writing to.
	/* To simplify coding at this time, we store
	   locked pages in array for now */
	struct reiserfs_transaction_handle th;
	th.t_trans_id = 0;

	/* If a filesystem is converted from 3.5 to 3.6, we'll have v3.5 items
	* lying around (most of the disk, in fact). Despite the filesystem
	* now being a v3.6 format, the old items still can't support large
	* file sizes. Catch this case here, as the rest of the VFS layer is
	* oblivious to the different limitations between old and new items.
	* reiserfs_setattr catches this for truncates. This chunk is lifted
	* from generic_write_checks. */
	if (get_inode_item_key_version (inode) == KEY_FORMAT_3_5 &&
	    *ppos + count > MAX_NON_LFS) {
		if (*ppos >= MAX_NON_LFS) {
			return -EFBIG;
		}
		if (count > MAX_NON_LFS - (unsigned long)*ppos)
			count = MAX_NON_LFS - (unsigned long)*ppos;
	}

	return do_sync_write(file, buf, count, ppos);
}

const struct file_operations reiserfs_file_operations = {
	.read = do_sync_read,
	.write = reiserfs_file_write,
	.unlocked_ioctl = reiserfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = reiserfs_compat_ioctl,
#endif
	.mmap = generic_file_mmap,
	.open = reiserfs_file_open,
	.release = reiserfs_file_release,
	.fsync = reiserfs_sync_file,
	.aio_read = generic_file_aio_read,
	.aio_write = generic_file_aio_write,
	.splice_read = generic_file_splice_read,
	.splice_write = generic_file_splice_write,
	.llseek = generic_file_llseek,
};

const struct inode_operations reiserfs_file_inode_operations = {
	.setattr = reiserfs_setattr,
	.setxattr = reiserfs_setxattr,
	.getxattr = reiserfs_getxattr,
	.listxattr = reiserfs_listxattr,
	.removexattr = reiserfs_removexattr,
	.permission = reiserfs_permission,
	.get_acl = reiserfs_get_acl,
};
