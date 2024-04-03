// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/fsync.c
 *
 *  Copyright (C) 1993  Stephen Tweedie (sct@redhat.com)
 *  from
 *  Copyright (C) 1992  Remy Card (card@masi.ibp.fr)
 *                      Laboratoire MASI - Institut Blaise Pascal
 *                      Universite Pierre et Marie Curie (Paris VI)
 *  from
 *  linux/fs/minix/truncate.c   Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext4fs fsync primitive
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *
 *  Removed unnecessary code duplication for little endian machines
 *  and excessive __inline__s.
 *        Andi Kleen, 1997
 *
 * Major simplications and cleanup - we only need to do the metadata, because
 * we can depend on generic_block_fdatasync() to sync the data blocks.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/writeback.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>

#include "ext4.h"
#include "ext4_jbd2.h"

#include <trace/events/ext4.h>

/*
 * If we're not journaling and this is a just-created file, we have to
 * sync our parent directory (if it was freshly created) since
 * otherwise it will only be written by writeback, leaving a huge
 * window during which a crash may lose the file.  This may apply for
 * the parent directory's parent as well, and so on recursively, if
 * they are also freshly created.
 */
static int ext4_sync_parent(struct inode *inode)
{
	struct dentry *dentry, *next;
	int ret = 0;

	if (!ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY))
		return 0;
	dentry = d_find_any_alias(inode);
	if (!dentry)
		return 0;
	while (ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) {
		ext4_clear_inode_state(inode, EXT4_STATE_NEWENTRY);

		next = dget_parent(dentry);
		dput(dentry);
		dentry = next;
		inode = dentry->d_inode;

		/*
		 * The directory inode may have gone through rmdir by now. But
		 * the inode itself and its blocks are still allocated (we hold
		 * a reference to the inode via its dentry), so it didn't go
		 * through ext4_evict_inode()) and so we are safe to flush
		 * metadata blocks and the inode.
		 */
		ret = sync_mapping_buffers(inode->i_mapping);
		if (ret)
			break;
		ret = sync_inode_metadata(inode, 1);
		if (ret)
			break;
	}
	dput(dentry);
	return ret;
}

static int ext4_fsync_nojournal(struct file *file, loff_t start, loff_t end,
				int datasync, bool *needs_barrier)
{
	struct inode *inode = file->f_inode;
	int ret;

	ret = generic_buffers_fsync_noflush(file, start, end, datasync);
	if (!ret)
		ret = ext4_sync_parent(inode);
	if (test_opt(inode->i_sb, BARRIER))
		*needs_barrier = true;

	return ret;
}

static int ext4_fsync_journal(struct inode *inode, bool datasync,
			     bool *needs_barrier)
{
	struct ext4_inode_info *ei = EXT4_I(inode);
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	tid_t commit_tid = datasync ? ei->i_datasync_tid : ei->i_sync_tid;

	/*
	 * Fastcommit does not really support fsync on directories or other
	 * special files. Force a full commit.
	 */
	if (!S_ISREG(inode->i_mode))
		return ext4_force_commit(inode->i_sb);

	if (journal->j_flags & JBD2_BARRIER &&
	    !jbd2_trans_will_send_data_barrier(journal, commit_tid))
		*needs_barrier = true;

	return ext4_fc_commit(journal, commit_tid);
}

/*
 * akpm: A new design for ext4_sync_file().
 *
 * This is only called from sys_fsync(), sys_fdatasync() and sys_msync().
 * There cannot be a transaction open by this task.
 * Another task could have dirtied this inode.  Its data can be in any
 * state in the journalling system.
 *
 * What we do is just kick off a commit and wait on it.  This will snapshot the
 * inode to disk.
 */
int ext4_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret = 0, err;
	bool needs_barrier = false;
	struct inode *inode = file->f_mapping->host;

	if (unlikely(ext4_forced_shutdown(inode->i_sb)))
		return -EIO;

	ASSERT(ext4_journal_current_handle() == NULL);

	trace_ext4_sync_file_enter(file, datasync);

	if (sb_rdonly(inode->i_sb)) {
		/* Make sure that we read updated s_ext4_flags value */
		smp_rmb();
		if (ext4_forced_shutdown(inode->i_sb))
			ret = -EROFS;
		goto out;
	}

	if (!EXT4_SB(inode->i_sb)->s_journal) {
		ret = ext4_fsync_nojournal(file, start, end, datasync,
					   &needs_barrier);
		if (needs_barrier)
			goto issue_flush;
		goto out;
	}

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		goto out;

	/*
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  Metadata is in the journal, we wait for proper transaction to
	 *  commit here.
	 */
	ret = ext4_fsync_journal(inode, datasync, &needs_barrier);

issue_flush:
	if (needs_barrier) {
		err = blkdev_issue_flush(inode->i_sb->s_bdev);
		if (!ret)
			ret = err;
	}
out:
	err = file_check_and_advance_wb_err(file);
	if (ret == 0)
		ret = err;
	trace_ext4_sync_file_exit(inode, ret);
	return ret;
}
