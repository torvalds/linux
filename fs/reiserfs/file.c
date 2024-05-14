/*
 * Copyright 2000 by Hans Reiser, licensing governed by reiserfs/README
 */

#include <linux/time.h>
#include "reiserfs.h"
#include "acl.h"
#include "xattr.h"
#include <linux/uaccess.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/quotaops.h>

/*
 * We pack the tails of files on file close, not at the time they are written.
 * This implies an unnecessary copy of the tail and an unnecessary indirect item
 * insertion/balancing, for files that are written in one write.
 * It avoids unnecessary tail packings (balances) for files that are written in
 * multiple writes and are small enough to have tails.
 *
 * file_release is called by the VFS layer when the file is closed.  If
 * this is the last open file descriptor, and the file
 * small enough to have a tail, and the tail is currently in an
 * unformatted node, the tail is converted back into a direct item.
 *
 * We use reiserfs_truncate_file to pack the tail, since it already has
 * all the conditions coded.
 */
static int reiserfs_file_release(struct inode *inode, struct file *filp)
{

	struct reiserfs_transaction_handle th;
	int err;
	int jbegin_failure = 0;

	BUG_ON(!S_ISREG(inode->i_mode));

	if (!atomic_dec_and_mutex_lock(&REISERFS_I(inode)->openers,
				       &REISERFS_I(inode)->tailpack))
		return 0;

	/* fast out for when nothing needs to be done */
	if ((!(REISERFS_I(inode)->i_flags & i_pack_on_close_mask) ||
	     !tail_has_to_be_packed(inode)) &&
	    REISERFS_I(inode)->i_prealloc_count <= 0) {
		mutex_unlock(&REISERFS_I(inode)->tailpack);
		return 0;
	}

	reiserfs_write_lock(inode->i_sb);
	/*
	 * freeing preallocation only involves relogging blocks that
	 * are already in the current transaction.  preallocation gets
	 * freed at the end of each transaction, so it is impossible for
	 * us to log any additional blocks (including quota blocks)
	 */
	err = journal_begin(&th, inode->i_sb, 1);
	if (err) {
		/*
		 * uh oh, we can't allow the inode to go away while there
		 * is still preallocation blocks pending.  Try to join the
		 * aborted transaction
		 */
		jbegin_failure = err;
		err = journal_join_abort(&th, inode->i_sb);

		if (err) {
			/*
			 * hmpf, our choices here aren't good.  We can pin
			 * the inode which will disallow unmount from ever
			 * happening, we can do nothing, which will corrupt
			 * random memory on unmount, or we can forcibly
			 * remove the file from the preallocation list, which
			 * will leak blocks on disk.  Lets pin the inode
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
	err = journal_end(&th);

	/* copy back the error code from journal_begin */
	if (!err)
		err = jbegin_failure;

	if (!err &&
	    (REISERFS_I(inode)->i_flags & i_pack_on_close_mask) &&
	    tail_has_to_be_packed(inode)) {

		/*
		 * if regular file is released by last holder and it has been
		 * appended (we append by unformatted node only) or its direct
		 * item(s) had to be converted, then it may have to be
		 * indirect2direct converted
		 */
		err = reiserfs_truncate_file(inode, 0);
	}
out:
	reiserfs_write_unlock(inode->i_sb);
	mutex_unlock(&REISERFS_I(inode)->tailpack);
	return err;
}

static int reiserfs_file_open(struct inode *inode, struct file *file)
{
	int err = dquot_file_open(inode, file);

	/* somebody might be tailpacking on final close; wait for it */
        if (!atomic_inc_not_zero(&REISERFS_I(inode)->openers)) {
		mutex_lock(&REISERFS_I(inode)->tailpack);
		atomic_inc(&REISERFS_I(inode)->openers);
		mutex_unlock(&REISERFS_I(inode)->tailpack);
	}
	return err;
}

void reiserfs_vfs_truncate_file(struct inode *inode)
{
	mutex_lock(&REISERFS_I(inode)->tailpack);
	reiserfs_truncate_file(inode, 1);
	mutex_unlock(&REISERFS_I(inode)->tailpack);
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

	err = file_write_and_wait_range(filp, start, end);
	if (err)
		return err;

	inode_lock(inode);
	BUG_ON(!S_ISREG(inode->i_mode));
	err = sync_mapping_buffers(inode->i_mapping);
	reiserfs_write_lock(inode->i_sb);
	barrier_done = reiserfs_commit_for_inode(inode);
	reiserfs_write_unlock(inode->i_sb);
	if (barrier_done != 1 && reiserfs_barrier_flush(inode->i_sb))
		blkdev_issue_flush(inode->i_sb->s_bdev);
	inode_unlock(inode);
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
	unsigned long i_size_index = inode->i_size >> PAGE_SHIFT;
	int new;
	int logit = reiserfs_file_data_log(inode);
	struct super_block *s = inode->i_sb;
	int bh_per_page = PAGE_SIZE / s->s_blocksize;
	struct reiserfs_transaction_handle th;
	int ret = 0;

	th.t_trans_id = 0;
	blocksize = i_blocksize(inode);

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
				journal_mark_dirty(&th, bh);
			} else if (!buffer_dirty(bh)) {
				mark_buffer_dirty(bh);
				/*
				 * do data=ordered on any page past the end
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
		ret = journal_end(&th);
drop_write_lock:
		reiserfs_write_unlock(s);
	}
	/*
	 * If this is a partial write which happened to make all buffers
	 * uptodate then we can optimize away a bogus read_folio() for
	 * the next read(). Here we 'discover' whether the page went
	 * uptodate as a result of this (potentially partial) write.
	 */
	if (!partial)
		SetPageUptodate(page);
	return ret;
}

const struct file_operations reiserfs_file_operations = {
	.unlocked_ioctl = reiserfs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = reiserfs_compat_ioctl,
#endif
	.mmap = generic_file_mmap,
	.open = reiserfs_file_open,
	.release = reiserfs_file_release,
	.fsync = reiserfs_sync_file,
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.splice_read = generic_file_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = generic_file_llseek,
};

const struct inode_operations reiserfs_file_inode_operations = {
	.setattr = reiserfs_setattr,
	.listxattr = reiserfs_listxattr,
	.permission = reiserfs_permission,
	.get_acl = reiserfs_get_acl,
	.set_acl = reiserfs_set_acl,
	.fileattr_get = reiserfs_fileattr_get,
	.fileattr_set = reiserfs_fileattr_set,
};
