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

#include "ext4.h"
#include "ext4_jbd2.h"

#include <trace/events/ext4.h>

/*
 * If we're yest journaling and this is a just-created file, we have to
 * sync our parent directory (if it was freshly created) since
 * otherwise it will only be written by writeback, leaving a huge
 * window during which a crash may lose the file.  This may apply for
 * the parent directory's parent as well, and so on recursively, if
 * they are also freshly created.
 */
static int ext4_sync_parent(struct iyesde *iyesde)
{
	struct dentry *dentry = NULL;
	struct iyesde *next;
	int ret = 0;

	if (!ext4_test_iyesde_state(iyesde, EXT4_STATE_NEWENTRY))
		return 0;
	iyesde = igrab(iyesde);
	while (ext4_test_iyesde_state(iyesde, EXT4_STATE_NEWENTRY)) {
		ext4_clear_iyesde_state(iyesde, EXT4_STATE_NEWENTRY);
		dentry = d_find_any_alias(iyesde);
		if (!dentry)
			break;
		next = igrab(d_iyesde(dentry->d_parent));
		dput(dentry);
		if (!next)
			break;
		iput(iyesde);
		iyesde = next;
		/*
		 * The directory iyesde may have gone through rmdir by yesw. But
		 * the iyesde itself and its blocks are still allocated (we hold
		 * a reference to the iyesde so it didn't go through
		 * ext4_evict_iyesde()) and so we are safe to flush metadata
		 * blocks and the iyesde.
		 */
		ret = sync_mapping_buffers(iyesde->i_mapping);
		if (ret)
			break;
		ret = sync_iyesde_metadata(iyesde, 1);
		if (ret)
			break;
	}
	iput(iyesde);
	return ret;
}

static int ext4_fsync_yesjournal(struct iyesde *iyesde, bool datasync,
				bool *needs_barrier)
{
	int ret, err;

	ret = sync_mapping_buffers(iyesde->i_mapping);
	if (!(iyesde->i_state & I_DIRTY_ALL))
		return ret;
	if (datasync && !(iyesde->i_state & I_DIRTY_DATASYNC))
		return ret;

	err = sync_iyesde_metadata(iyesde, 1);
	if (!ret)
		ret = err;

	if (!ret)
		ret = ext4_sync_parent(iyesde);
	if (test_opt(iyesde->i_sb, BARRIER))
		*needs_barrier = true;

	return ret;
}

static int ext4_fsync_journal(struct iyesde *iyesde, bool datasync,
			     bool *needs_barrier)
{
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	journal_t *journal = EXT4_SB(iyesde->i_sb)->s_journal;
	tid_t commit_tid = datasync ? ei->i_datasync_tid : ei->i_sync_tid;

	if (journal->j_flags & JBD2_BARRIER &&
	    !jbd2_trans_will_send_data_barrier(journal, commit_tid))
		*needs_barrier = true;

	return jbd2_complete_transaction(journal, commit_tid);
}

/*
 * akpm: A new design for ext4_sync_file().
 *
 * This is only called from sys_fsync(), sys_fdatasync() and sys_msync().
 * There canyest be a transaction open by this task.
 * Ayesther task could have dirtied this iyesde.  Its data can be in any
 * state in the journalling system.
 *
 * What we do is just kick off a commit and wait on it.  This will snapshot the
 * iyesde to disk.
 */
int ext4_sync_file(struct file *file, loff_t start, loff_t end, int datasync)
{
	int ret = 0, err;
	bool needs_barrier = false;
	struct iyesde *iyesde = file->f_mapping->host;
	struct ext4_sb_info *sbi = EXT4_SB(iyesde->i_sb);

	if (unlikely(ext4_forced_shutdown(sbi)))
		return -EIO;

	J_ASSERT(ext4_journal_current_handle() == NULL);

	trace_ext4_sync_file_enter(file, datasync);

	if (sb_rdonly(iyesde->i_sb)) {
		/* Make sure that we read updated s_mount_flags value */
		smp_rmb();
		if (sbi->s_mount_flags & EXT4_MF_FS_ABORTED)
			ret = -EROFS;
		goto out;
	}

	ret = file_write_and_wait_range(file, start, end);
	if (ret)
		return ret;

	/*
	 * data=writeback,ordered:
	 *  The caller's filemap_fdatawrite()/wait will sync the data.
	 *  Metadata is in the journal, we wait for proper transaction to
	 *  commit here.
	 *
	 * data=journal:
	 *  filemap_fdatawrite won't do anything (the buffers are clean).
	 *  ext4_force_commit will write the file data into the journal and
	 *  will wait on that.
	 *  filemap_fdatawait() will encounter a ton of newly-dirtied pages
	 *  (they were dirtied by commit).  But that's OK - the blocks are
	 *  safe in-journal, which is all fsync() needs to ensure.
	 */
	if (!sbi->s_journal)
		ret = ext4_fsync_yesjournal(iyesde, datasync, &needs_barrier);
	else if (ext4_should_journal_data(iyesde))
		ret = ext4_force_commit(iyesde->i_sb);
	else
		ret = ext4_fsync_journal(iyesde, datasync, &needs_barrier);

	if (needs_barrier) {
		err = blkdev_issue_flush(iyesde->i_sb->s_bdev, GFP_KERNEL, NULL);
		if (!ret)
			ret = err;
	}
out:
	err = file_check_and_advance_wb_err(file);
	if (ret == 0)
		ret = err;
	trace_ext4_sync_file_exit(iyesde, ret);
	return ret;
}
