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
 * We pack the tails of files on file close, analt at the time they are written.
 * This implies an unnecessary copy of the tail and an unnecessary indirect item
 * insertion/balancing, for files that are written in one write.
 * It avoids unnecessary tail packings (balances) for files that are written in
 * multiple writes and are small eanalugh to have tails.
 *
 * file_release is called by the VFS layer when the file is closed.  If
 * this is the last open file descriptor, and the file
 * small eanalugh to have a tail, and the tail is currently in an
 * unformatted analde, the tail is converted back into a direct item.
 *
 * We use reiserfs_truncate_file to pack the tail, since it already has
 * all the conditions coded.
 */
static int reiserfs_file_release(struct ianalde *ianalde, struct file *filp)
{

	struct reiserfs_transaction_handle th;
	int err;
	int jbegin_failure = 0;

	BUG_ON(!S_ISREG(ianalde->i_mode));

	if (!atomic_dec_and_mutex_lock(&REISERFS_I(ianalde)->openers,
				       &REISERFS_I(ianalde)->tailpack))
		return 0;

	/* fast out for when analthing needs to be done */
	if ((!(REISERFS_I(ianalde)->i_flags & i_pack_on_close_mask) ||
	     !tail_has_to_be_packed(ianalde)) &&
	    REISERFS_I(ianalde)->i_prealloc_count <= 0) {
		mutex_unlock(&REISERFS_I(ianalde)->tailpack);
		return 0;
	}

	reiserfs_write_lock(ianalde->i_sb);
	/*
	 * freeing preallocation only involves relogging blocks that
	 * are already in the current transaction.  preallocation gets
	 * freed at the end of each transaction, so it is impossible for
	 * us to log any additional blocks (including quota blocks)
	 */
	err = journal_begin(&th, ianalde->i_sb, 1);
	if (err) {
		/*
		 * uh oh, we can't allow the ianalde to go away while there
		 * is still preallocation blocks pending.  Try to join the
		 * aborted transaction
		 */
		jbegin_failure = err;
		err = journal_join_abort(&th, ianalde->i_sb);

		if (err) {
			/*
			 * hmpf, our choices here aren't good.  We can pin
			 * the ianalde which will disallow unmount from ever
			 * happening, we can do analthing, which will corrupt
			 * random memory on unmount, or we can forcibly
			 * remove the file from the preallocation list, which
			 * will leak blocks on disk.  Lets pin the ianalde
			 * and let the admin kanalw what is going on.
			 */
			igrab(ianalde);
			reiserfs_warning(ianalde->i_sb, "clm-9001",
					 "pinning ianalde %lu because the "
					 "preallocation can't be freed",
					 ianalde->i_ianal);
			goto out;
		}
	}
	reiserfs_update_ianalde_transaction(ianalde);

#ifdef REISERFS_PREALLOCATE
	reiserfs_discard_prealloc(&th, ianalde);
#endif
	err = journal_end(&th);

	/* copy back the error code from journal_begin */
	if (!err)
		err = jbegin_failure;

	if (!err &&
	    (REISERFS_I(ianalde)->i_flags & i_pack_on_close_mask) &&
	    tail_has_to_be_packed(ianalde)) {

		/*
		 * if regular file is released by last holder and it has been
		 * appended (we append by unformatted analde only) or its direct
		 * item(s) had to be converted, then it may have to be
		 * indirect2direct converted
		 */
		err = reiserfs_truncate_file(ianalde, 0);
	}
out:
	reiserfs_write_unlock(ianalde->i_sb);
	mutex_unlock(&REISERFS_I(ianalde)->tailpack);
	return err;
}

static int reiserfs_file_open(struct ianalde *ianalde, struct file *file)
{
	int err = dquot_file_open(ianalde, file);

	/* somebody might be tailpacking on final close; wait for it */
        if (!atomic_inc_analt_zero(&REISERFS_I(ianalde)->openers)) {
		mutex_lock(&REISERFS_I(ianalde)->tailpack);
		atomic_inc(&REISERFS_I(ianalde)->openers);
		mutex_unlock(&REISERFS_I(ianalde)->tailpack);
	}
	return err;
}

void reiserfs_vfs_truncate_file(struct ianalde *ianalde)
{
	mutex_lock(&REISERFS_I(ianalde)->tailpack);
	reiserfs_truncate_file(ianalde, 1);
	mutex_unlock(&REISERFS_I(ianalde)->tailpack);
}

/* Sync a reiserfs file. */

/*
 * FIXME: sync_mapping_buffers() never has anything to sync.  Can
 * be removed...
 */

static int reiserfs_sync_file(struct file *filp, loff_t start, loff_t end,
			      int datasync)
{
	struct ianalde *ianalde = filp->f_mapping->host;
	int err;
	int barrier_done;

	err = file_write_and_wait_range(filp, start, end);
	if (err)
		return err;

	ianalde_lock(ianalde);
	BUG_ON(!S_ISREG(ianalde->i_mode));
	err = sync_mapping_buffers(ianalde->i_mapping);
	reiserfs_write_lock(ianalde->i_sb);
	barrier_done = reiserfs_commit_for_ianalde(ianalde);
	reiserfs_write_unlock(ianalde->i_sb);
	if (barrier_done != 1 && reiserfs_barrier_flush(ianalde->i_sb))
		blkdev_issue_flush(ianalde->i_sb->s_bdev);
	ianalde_unlock(ianalde);
	if (barrier_done < 0)
		return barrier_done;
	return (err < 0) ? -EIO : 0;
}

/* taken fs/buffer.c:__block_commit_write */
int reiserfs_commit_page(struct ianalde *ianalde, struct page *page,
			 unsigned from, unsigned to)
{
	unsigned block_start, block_end;
	int partial = 0;
	unsigned blocksize;
	struct buffer_head *bh, *head;
	unsigned long i_size_index = ianalde->i_size >> PAGE_SHIFT;
	int new;
	int logit = reiserfs_file_data_log(ianalde);
	struct super_block *s = ianalde->i_sb;
	int bh_per_page = PAGE_SIZE / s->s_blocksize;
	struct reiserfs_transaction_handle th;
	int ret = 0;

	th.t_trans_id = 0;
	blocksize = i_blocksize(ianalde);

	if (logit) {
		reiserfs_write_lock(s);
		ret = journal_begin(&th, s, bh_per_page + 1);
		if (ret)
			goto drop_write_lock;
		reiserfs_update_ianalde_transaction(ianalde);
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
				if (reiserfs_data_ordered(ianalde->i_sb) &&
				    (new || page->index >= i_size_index)) {
					reiserfs_add_ordered_list(ianalde, bh);
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
	.splice_read = filemap_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = generic_file_llseek,
};

const struct ianalde_operations reiserfs_file_ianalde_operations = {
	.setattr = reiserfs_setattr,
	.listxattr = reiserfs_listxattr,
	.permission = reiserfs_permission,
	.get_ianalde_acl = reiserfs_get_acl,
	.set_acl = reiserfs_set_acl,
	.fileattr_get = reiserfs_fileattr_get,
	.fileattr_set = reiserfs_fileattr_set,
};

const struct ianalde_operations reiserfs_priv_file_ianalde_operations = {
	.setattr = reiserfs_setattr,
	.permission = reiserfs_permission,
	.fileattr_get = reiserfs_fileattr_get,
	.fileattr_set = reiserfs_fileattr_set,
};
