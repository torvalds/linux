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
#include <linux/jbd2.h>
#include <linux/blkdev.h>

#include "ext4.h"
#include "ext4_jbd2.h"

#include <trace/events/ext4.h>

static void dump_completed_IO(struct inode * inode)
{
#ifdef	EXT4_DEBUG
	struct list_head *cur, *before, *after;
	ext4_io_end_t *io, *io0, *io1;
	unsigned long flags;

	if (list_empty(&EXT4_I(inode)->i_completed_io_list)){
		ext4_debug("inode %lu completed_io list is empty\n", inode->i_ino);
		return;
	}

	ext4_debug("Dump inode %lu completed_io list \n", inode->i_ino);
	spin_lock_irqsave(&EXT4_I(inode)->i_completed_io_lock, flags);
	list_for_each_entry(io, &EXT4_I(inode)->i_completed_io_list, list){
		cur = &io->list;
		before = cur->prev;
		io0 = container_of(before, ext4_io_end_t, list);
		after = cur->next;
		io1 = container_of(after, ext4_io_end_t, list);

		ext4_debug("io 0x%p from inode %lu,prev 0x%p,next 0x%p\n",
			    io, inode->i_ino, io0, io1);
	}
	spin_unlock_irqrestore(&EXT4_I(inode)->i_completed_io_lock, flags);
#endif
}

/*
 * This function is called from ext4_sync_file().
 *
 * When IO is completed, the work to convert unwritten extents to
 * written is queued on workqueue but may not get immediately
 * scheduled. When fsync is called, we need to ensure the
 * conversion is complete before fsync returns.
 * The inode keeps track of a list of pending/completed IO that
 * might needs to do the conversion. This function walks through
 * the list and convert the related unwritten extents for completed IO
 * to written.
 * The function return the number of pending IOs on success.
 */
static int flush_completed_IO(struct inode *inode)
{
	ext4_io_end_t *io;
	struct ext4_inode_info *ei = EXT4_I(inode);
	unsigned long flags;
	int ret = 0;
	int ret2 = 0;

	if (list_empty(&ei->i_completed_io_list))
		return ret;

	dump_completed_IO(inode);
	spin_lock_irqsave(&ei->i_completed_io_lock, flags);
	while (!list_empty(&ei->i_completed_io_list)){
		io = list_entry(ei->i_completed_io_list.next,
				ext4_io_end_t, list);
		/*
		 * Calling ext4_end_io_nolock() to convert completed
		 * IO to written.
		 *
		 * When ext4_sync_file() is called, run_queue() may already
		 * about to flush the work corresponding to this io structure.
		 * It will be upset if it founds the io structure related
		 * to the work-to-be schedule is freed.
		 *
		 * Thus we need to keep the io structure still valid here after
		 * convertion finished. The io structure has a flag to
		 * avoid double converting from both fsync and background work
		 * queue work.
		 */
		spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);
		ret = ext4_end_io_nolock(io);
		spin_lock_irqsave(&ei->i_completed_io_lock, flags);
		if (ret < 0)
			ret2 = ret;
		else
			list_del_init(&io->list);
	}
	spin_unlock_irqrestore(&ei->i_completed_io_lock, flags);
	return (ret2 < 0) ? ret2 : 0;
}

/*
 * If we're not journaling and this is a just-created file, we have to
 * sync our parent directory (if it was freshly created) since
 * otherwise it will only be written by writeback, leaving a huge
 * window during which a crash may lose the file.  This may apply for
 * the parent directory's parent as well, and so on recursively, if
 * they are also freshly created.
 */
static void ext4_sync_parent(struct inode *inode)
{
	struct dentry *dentry = NULL;

	while (inode && ext4_test_inode_state(inode, EXT4_STATE_NEWENTRY)) {
		ext4_clear_inode_state(inode, EXT4_STATE_NEWENTRY);
		dentry = list_entry(inode->i_dentry.next,
				    struct dentry, d_alias);
		if (!dentry || !dentry->d_parent || !dentry->d_parent->d_inode)
			break;
		inode = dentry->d_parent->d_inode;
		sync_mapping_buffers(inode->i_mapping);
	}
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
 *
 * i_mutex lock is held when entering and exiting this function
 */

int ext4_sync_file(struct file *file, int datasync)
{
	struct inode *inode = file->f_mapping->host;
	struct ext4_inode_info *ei = EXT4_I(inode);
	journal_t *journal = EXT4_SB(inode->i_sb)->s_journal;
	int ret;
	tid_t commit_tid;

	J_ASSERT(ext4_journal_current_handle() == NULL);

	trace_ext4_sync_file(file, datasync);

	if (inode->i_sb->s_flags & MS_RDONLY)
		return 0;

	ret = flush_completed_IO(inode);
	if (ret < 0)
		return ret;

	if (!journal) {
		ret = generic_file_fsync(file, datasync);
		if (!ret && !list_empty(&inode->i_dentry))
			ext4_sync_parent(inode);
		return ret;
	}

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
	if (ext4_should_journal_data(inode))
		return ext4_force_commit(inode->i_sb);

	commit_tid = datasync ? ei->i_datasync_tid : ei->i_sync_tid;
	if (jbd2_log_start_commit(journal, commit_tid)) {
		/*
		 * When the journal is on a different device than the
		 * fs data disk, we need to issue the barrier in
		 * writeback mode.  (In ordered mode, the jbd2 layer
		 * will take care of issuing the barrier.  In
		 * data=journal, all of the data blocks are written to
		 * the journal device.)
		 */
		if (ext4_should_writeback_data(inode) &&
		    (journal->j_fs_dev != journal->j_dev) &&
		    (journal->j_flags & JBD2_BARRIER))
			blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL,
					NULL);
		ret = jbd2_log_wait_commit(journal, commit_tid);
	} else if (journal->j_flags & JBD2_BARRIER)
		blkdev_issue_flush(inode->i_sb->s_bdev, GFP_KERNEL, NULL);
	return ret;
}
