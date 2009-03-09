/*
 * linux/fs/jbd/journal.c
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1998
 *
 * Copyright 1998 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Generic filesystem journal-writing code; part of the ext2fs
 * journaling system.
 *
 * This file manages journals: areas of disk reserved for logging
 * transactional updates.  This includes the kernel journaling thread
 * which is responsible for scheduling updates to the log.
 *
 * We do not actually manage the physical storage of the journal in this
 * file: that is left to a per-journal policy function, which allows us
 * to store the journal within a filesystem-specified area for ext2
 * journaling (ext2 can use a reserved inode for storing the log).
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/freezer.h>
#include <linux/pagemap.h>
#include <linux/kthread.h>
#include <linux/poison.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>

#include <asm/uaccess.h>
#include <asm/page.h>

EXPORT_SYMBOL(journal_start);
EXPORT_SYMBOL(journal_restart);
EXPORT_SYMBOL(journal_extend);
EXPORT_SYMBOL(journal_stop);
EXPORT_SYMBOL(journal_lock_updates);
EXPORT_SYMBOL(journal_unlock_updates);
EXPORT_SYMBOL(journal_get_write_access);
EXPORT_SYMBOL(journal_get_create_access);
EXPORT_SYMBOL(journal_get_undo_access);
EXPORT_SYMBOL(journal_dirty_data);
EXPORT_SYMBOL(journal_dirty_metadata);
EXPORT_SYMBOL(journal_release_buffer);
EXPORT_SYMBOL(journal_forget);
#if 0
EXPORT_SYMBOL(journal_sync_buffer);
#endif
EXPORT_SYMBOL(journal_flush);
EXPORT_SYMBOL(journal_revoke);

EXPORT_SYMBOL(journal_init_dev);
EXPORT_SYMBOL(journal_init_inode);
EXPORT_SYMBOL(journal_update_format);
EXPORT_SYMBOL(journal_check_used_features);
EXPORT_SYMBOL(journal_check_available_features);
EXPORT_SYMBOL(journal_set_features);
EXPORT_SYMBOL(journal_create);
EXPORT_SYMBOL(journal_load);
EXPORT_SYMBOL(journal_destroy);
EXPORT_SYMBOL(journal_abort);
EXPORT_SYMBOL(journal_errno);
EXPORT_SYMBOL(journal_ack_err);
EXPORT_SYMBOL(journal_clear_err);
EXPORT_SYMBOL(log_wait_commit);
EXPORT_SYMBOL(journal_start_commit);
EXPORT_SYMBOL(journal_force_commit_nested);
EXPORT_SYMBOL(journal_wipe);
EXPORT_SYMBOL(journal_blocks_per_page);
EXPORT_SYMBOL(journal_invalidatepage);
EXPORT_SYMBOL(journal_try_to_free_buffers);
EXPORT_SYMBOL(journal_force_commit);

static int journal_convert_superblock_v1(journal_t *, journal_superblock_t *);
static void __journal_abort_soft (journal_t *journal, int errno);

/*
 * Helper function used to manage commit timeouts
 */

static void commit_timeout(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	wake_up_process(p);
}

/*
 * kjournald: The main thread function used to manage a logging device
 * journal.
 *
 * This kernel thread is responsible for two things:
 *
 * 1) COMMIT:  Every so often we need to commit the current state of the
 *    filesystem to disk.  The journal thread is responsible for writing
 *    all of the metadata buffers to disk.
 *
 * 2) CHECKPOINT: We cannot reuse a used section of the log file until all
 *    of the data in that part of the log has been rewritten elsewhere on
 *    the disk.  Flushing these old buffers to reclaim space in the log is
 *    known as checkpointing, and this thread is responsible for that job.
 */

static int kjournald(void *arg)
{
	journal_t *journal = arg;
	transaction_t *transaction;

	/*
	 * Set up an interval timer which can be used to trigger a commit wakeup
	 * after the commit interval expires
	 */
	setup_timer(&journal->j_commit_timer, commit_timeout,
			(unsigned long)current);

	/* Record that the journal thread is running */
	journal->j_task = current;
	wake_up(&journal->j_wait_done_commit);

	printk(KERN_INFO "kjournald starting.  Commit interval %ld seconds\n",
			journal->j_commit_interval / HZ);

	/*
	 * And now, wait forever for commit wakeup events.
	 */
	spin_lock(&journal->j_state_lock);

loop:
	if (journal->j_flags & JFS_UNMOUNT)
		goto end_loop;

	jbd_debug(1, "commit_sequence=%d, commit_request=%d\n",
		journal->j_commit_sequence, journal->j_commit_request);

	if (journal->j_commit_sequence != journal->j_commit_request) {
		jbd_debug(1, "OK, requests differ\n");
		spin_unlock(&journal->j_state_lock);
		del_timer_sync(&journal->j_commit_timer);
		journal_commit_transaction(journal);
		spin_lock(&journal->j_state_lock);
		goto loop;
	}

	wake_up(&journal->j_wait_done_commit);
	if (freezing(current)) {
		/*
		 * The simpler the better. Flushing journal isn't a
		 * good idea, because that depends on threads that may
		 * be already stopped.
		 */
		jbd_debug(1, "Now suspending kjournald\n");
		spin_unlock(&journal->j_state_lock);
		refrigerator();
		spin_lock(&journal->j_state_lock);
	} else {
		/*
		 * We assume on resume that commits are already there,
		 * so we don't sleep
		 */
		DEFINE_WAIT(wait);
		int should_sleep = 1;

		prepare_to_wait(&journal->j_wait_commit, &wait,
				TASK_INTERRUPTIBLE);
		if (journal->j_commit_sequence != journal->j_commit_request)
			should_sleep = 0;
		transaction = journal->j_running_transaction;
		if (transaction && time_after_eq(jiffies,
						transaction->t_expires))
			should_sleep = 0;
		if (journal->j_flags & JFS_UNMOUNT)
			should_sleep = 0;
		if (should_sleep) {
			spin_unlock(&journal->j_state_lock);
			schedule();
			spin_lock(&journal->j_state_lock);
		}
		finish_wait(&journal->j_wait_commit, &wait);
	}

	jbd_debug(1, "kjournald wakes\n");

	/*
	 * Were we woken up by a commit wakeup event?
	 */
	transaction = journal->j_running_transaction;
	if (transaction && time_after_eq(jiffies, transaction->t_expires)) {
		journal->j_commit_request = transaction->t_tid;
		jbd_debug(1, "woke because of timeout\n");
	}
	goto loop;

end_loop:
	spin_unlock(&journal->j_state_lock);
	del_timer_sync(&journal->j_commit_timer);
	journal->j_task = NULL;
	wake_up(&journal->j_wait_done_commit);
	jbd_debug(1, "Journal thread exiting.\n");
	return 0;
}

static int journal_start_thread(journal_t *journal)
{
	struct task_struct *t;

	t = kthread_run(kjournald, journal, "kjournald");
	if (IS_ERR(t))
		return PTR_ERR(t);

	wait_event(journal->j_wait_done_commit, journal->j_task != NULL);
	return 0;
}

static void journal_kill_thread(journal_t *journal)
{
	spin_lock(&journal->j_state_lock);
	journal->j_flags |= JFS_UNMOUNT;

	while (journal->j_task) {
		wake_up(&journal->j_wait_commit);
		spin_unlock(&journal->j_state_lock);
		wait_event(journal->j_wait_done_commit,
				journal->j_task == NULL);
		spin_lock(&journal->j_state_lock);
	}
	spin_unlock(&journal->j_state_lock);
}

/*
 * journal_write_metadata_buffer: write a metadata buffer to the journal.
 *
 * Writes a metadata buffer to a given disk block.  The actual IO is not
 * performed but a new buffer_head is constructed which labels the data
 * to be written with the correct destination disk block.
 *
 * Any magic-number escaping which needs to be done will cause a
 * copy-out here.  If the buffer happens to start with the
 * JFS_MAGIC_NUMBER, then we can't write it to the log directly: the
 * magic number is only written to the log for descripter blocks.  In
 * this case, we copy the data and replace the first word with 0, and we
 * return a result code which indicates that this buffer needs to be
 * marked as an escaped buffer in the corresponding log descriptor
 * block.  The missing word can then be restored when the block is read
 * during recovery.
 *
 * If the source buffer has already been modified by a new transaction
 * since we took the last commit snapshot, we use the frozen copy of
 * that data for IO.  If we end up using the existing buffer_head's data
 * for the write, then we *have* to lock the buffer to prevent anyone
 * else from using and possibly modifying it while the IO is in
 * progress.
 *
 * The function returns a pointer to the buffer_heads to be used for IO.
 *
 * We assume that the journal has already been locked in this function.
 *
 * Return value:
 *  <0: Error
 * >=0: Finished OK
 *
 * On success:
 * Bit 0 set == escape performed on the data
 * Bit 1 set == buffer copy-out performed (kfree the data after IO)
 */

int journal_write_metadata_buffer(transaction_t *transaction,
				  struct journal_head  *jh_in,
				  struct journal_head **jh_out,
				  unsigned long blocknr)
{
	int need_copy_out = 0;
	int done_copy_out = 0;
	int do_escape = 0;
	char *mapped_data;
	struct buffer_head *new_bh;
	struct journal_head *new_jh;
	struct page *new_page;
	unsigned int new_offset;
	struct buffer_head *bh_in = jh2bh(jh_in);

	/*
	 * The buffer really shouldn't be locked: only the current committing
	 * transaction is allowed to write it, so nobody else is allowed
	 * to do any IO.
	 *
	 * akpm: except if we're journalling data, and write() output is
	 * also part of a shared mapping, and another thread has
	 * decided to launch a writepage() against this buffer.
	 */
	J_ASSERT_BH(bh_in, buffer_jbddirty(bh_in));

	new_bh = alloc_buffer_head(GFP_NOFS|__GFP_NOFAIL);

	/*
	 * If a new transaction has already done a buffer copy-out, then
	 * we use that version of the data for the commit.
	 */
	jbd_lock_bh_state(bh_in);
repeat:
	if (jh_in->b_frozen_data) {
		done_copy_out = 1;
		new_page = virt_to_page(jh_in->b_frozen_data);
		new_offset = offset_in_page(jh_in->b_frozen_data);
	} else {
		new_page = jh2bh(jh_in)->b_page;
		new_offset = offset_in_page(jh2bh(jh_in)->b_data);
	}

	mapped_data = kmap_atomic(new_page, KM_USER0);
	/*
	 * Check for escaping
	 */
	if (*((__be32 *)(mapped_data + new_offset)) ==
				cpu_to_be32(JFS_MAGIC_NUMBER)) {
		need_copy_out = 1;
		do_escape = 1;
	}
	kunmap_atomic(mapped_data, KM_USER0);

	/*
	 * Do we need to do a data copy?
	 */
	if (need_copy_out && !done_copy_out) {
		char *tmp;

		jbd_unlock_bh_state(bh_in);
		tmp = jbd_alloc(bh_in->b_size, GFP_NOFS);
		jbd_lock_bh_state(bh_in);
		if (jh_in->b_frozen_data) {
			jbd_free(tmp, bh_in->b_size);
			goto repeat;
		}

		jh_in->b_frozen_data = tmp;
		mapped_data = kmap_atomic(new_page, KM_USER0);
		memcpy(tmp, mapped_data + new_offset, jh2bh(jh_in)->b_size);
		kunmap_atomic(mapped_data, KM_USER0);

		new_page = virt_to_page(tmp);
		new_offset = offset_in_page(tmp);
		done_copy_out = 1;
	}

	/*
	 * Did we need to do an escaping?  Now we've done all the
	 * copying, we can finally do so.
	 */
	if (do_escape) {
		mapped_data = kmap_atomic(new_page, KM_USER0);
		*((unsigned int *)(mapped_data + new_offset)) = 0;
		kunmap_atomic(mapped_data, KM_USER0);
	}

	/* keep subsequent assertions sane */
	new_bh->b_state = 0;
	init_buffer(new_bh, NULL, NULL);
	atomic_set(&new_bh->b_count, 1);
	jbd_unlock_bh_state(bh_in);

	new_jh = journal_add_journal_head(new_bh);	/* This sleeps */

	set_bh_page(new_bh, new_page, new_offset);
	new_jh->b_transaction = NULL;
	new_bh->b_size = jh2bh(jh_in)->b_size;
	new_bh->b_bdev = transaction->t_journal->j_dev;
	new_bh->b_blocknr = blocknr;
	set_buffer_mapped(new_bh);
	set_buffer_dirty(new_bh);

	*jh_out = new_jh;

	/*
	 * The to-be-written buffer needs to get moved to the io queue,
	 * and the original buffer whose contents we are shadowing or
	 * copying is moved to the transaction's shadow queue.
	 */
	JBUFFER_TRACE(jh_in, "file as BJ_Shadow");
	journal_file_buffer(jh_in, transaction, BJ_Shadow);
	JBUFFER_TRACE(new_jh, "file as BJ_IO");
	journal_file_buffer(new_jh, transaction, BJ_IO);

	return do_escape | (done_copy_out << 1);
}

/*
 * Allocation code for the journal file.  Manage the space left in the
 * journal, so that we can begin checkpointing when appropriate.
 */

/*
 * __log_space_left: Return the number of free blocks left in the journal.
 *
 * Called with the journal already locked.
 *
 * Called under j_state_lock
 */

int __log_space_left(journal_t *journal)
{
	int left = journal->j_free;

	assert_spin_locked(&journal->j_state_lock);

	/*
	 * Be pessimistic here about the number of those free blocks which
	 * might be required for log descriptor control blocks.
	 */

#define MIN_LOG_RESERVED_BLOCKS 32 /* Allow for rounding errors */

	left -= MIN_LOG_RESERVED_BLOCKS;

	if (left <= 0)
		return 0;
	left -= (left >> 3);
	return left;
}

/*
 * Called under j_state_lock.  Returns true if a transaction commit was started.
 */
int __log_start_commit(journal_t *journal, tid_t target)
{
	/*
	 * Are we already doing a recent enough commit?
	 */
	if (!tid_geq(journal->j_commit_request, target)) {
		/*
		 * We want a new commit: OK, mark the request and wakup the
		 * commit thread.  We do _not_ do the commit ourselves.
		 */

		journal->j_commit_request = target;
		jbd_debug(1, "JBD: requesting commit %d/%d\n",
			  journal->j_commit_request,
			  journal->j_commit_sequence);
		wake_up(&journal->j_wait_commit);
		return 1;
	}
	return 0;
}

int log_start_commit(journal_t *journal, tid_t tid)
{
	int ret;

	spin_lock(&journal->j_state_lock);
	ret = __log_start_commit(journal, tid);
	spin_unlock(&journal->j_state_lock);
	return ret;
}

/*
 * Force and wait upon a commit if the calling process is not within
 * transaction.  This is used for forcing out undo-protected data which contains
 * bitmaps, when the fs is running out of space.
 *
 * We can only force the running transaction if we don't have an active handle;
 * otherwise, we will deadlock.
 *
 * Returns true if a transaction was started.
 */
int journal_force_commit_nested(journal_t *journal)
{
	transaction_t *transaction = NULL;
	tid_t tid;

	spin_lock(&journal->j_state_lock);
	if (journal->j_running_transaction && !current->journal_info) {
		transaction = journal->j_running_transaction;
		__log_start_commit(journal, transaction->t_tid);
	} else if (journal->j_committing_transaction)
		transaction = journal->j_committing_transaction;

	if (!transaction) {
		spin_unlock(&journal->j_state_lock);
		return 0;	/* Nothing to retry */
	}

	tid = transaction->t_tid;
	spin_unlock(&journal->j_state_lock);
	log_wait_commit(journal, tid);
	return 1;
}

/*
 * Start a commit of the current running transaction (if any).  Returns true
 * if a transaction is going to be committed (or is currently already
 * committing), and fills its tid in at *ptid
 */
int journal_start_commit(journal_t *journal, tid_t *ptid)
{
	int ret = 0;

	spin_lock(&journal->j_state_lock);
	if (journal->j_running_transaction) {
		tid_t tid = journal->j_running_transaction->t_tid;

		__log_start_commit(journal, tid);
		/* There's a running transaction and we've just made sure
		 * it's commit has been scheduled. */
		if (ptid)
			*ptid = tid;
		ret = 1;
	} else if (journal->j_committing_transaction) {
		/*
		 * If ext3_write_super() recently started a commit, then we
		 * have to wait for completion of that transaction
		 */
		if (ptid)
			*ptid = journal->j_committing_transaction->t_tid;
		ret = 1;
	}
	spin_unlock(&journal->j_state_lock);
	return ret;
}

/*
 * Wait for a specified commit to complete.
 * The caller may not hold the journal lock.
 */
int log_wait_commit(journal_t *journal, tid_t tid)
{
	int err = 0;

#ifdef CONFIG_JBD_DEBUG
	spin_lock(&journal->j_state_lock);
	if (!tid_geq(journal->j_commit_request, tid)) {
		printk(KERN_EMERG
		       "%s: error: j_commit_request=%d, tid=%d\n",
		       __func__, journal->j_commit_request, tid);
	}
	spin_unlock(&journal->j_state_lock);
#endif
	spin_lock(&journal->j_state_lock);
	while (tid_gt(tid, journal->j_commit_sequence)) {
		jbd_debug(1, "JBD: want %d, j_commit_sequence=%d\n",
				  tid, journal->j_commit_sequence);
		wake_up(&journal->j_wait_commit);
		spin_unlock(&journal->j_state_lock);
		wait_event(journal->j_wait_done_commit,
				!tid_gt(tid, journal->j_commit_sequence));
		spin_lock(&journal->j_state_lock);
	}
	spin_unlock(&journal->j_state_lock);

	if (unlikely(is_journal_aborted(journal))) {
		printk(KERN_EMERG "journal commit I/O error\n");
		err = -EIO;
	}
	return err;
}

/*
 * Log buffer allocation routines:
 */

int journal_next_log_block(journal_t *journal, unsigned long *retp)
{
	unsigned long blocknr;

	spin_lock(&journal->j_state_lock);
	J_ASSERT(journal->j_free > 1);

	blocknr = journal->j_head;
	journal->j_head++;
	journal->j_free--;
	if (journal->j_head == journal->j_last)
		journal->j_head = journal->j_first;
	spin_unlock(&journal->j_state_lock);
	return journal_bmap(journal, blocknr, retp);
}

/*
 * Conversion of logical to physical block numbers for the journal
 *
 * On external journals the journal blocks are identity-mapped, so
 * this is a no-op.  If needed, we can use j_blk_offset - everything is
 * ready.
 */
int journal_bmap(journal_t *journal, unsigned long blocknr,
		 unsigned long *retp)
{
	int err = 0;
	unsigned long ret;

	if (journal->j_inode) {
		ret = bmap(journal->j_inode, blocknr);
		if (ret)
			*retp = ret;
		else {
			char b[BDEVNAME_SIZE];

			printk(KERN_ALERT "%s: journal block not found "
					"at offset %lu on %s\n",
				__func__,
				blocknr,
				bdevname(journal->j_dev, b));
			err = -EIO;
			__journal_abort_soft(journal, err);
		}
	} else {
		*retp = blocknr; /* +journal->j_blk_offset */
	}
	return err;
}

/*
 * We play buffer_head aliasing tricks to write data/metadata blocks to
 * the journal without copying their contents, but for journal
 * descriptor blocks we do need to generate bona fide buffers.
 *
 * After the caller of journal_get_descriptor_buffer() has finished modifying
 * the buffer's contents they really should run flush_dcache_page(bh->b_page).
 * But we don't bother doing that, so there will be coherency problems with
 * mmaps of blockdevs which hold live JBD-controlled filesystems.
 */
struct journal_head *journal_get_descriptor_buffer(journal_t *journal)
{
	struct buffer_head *bh;
	unsigned long blocknr;
	int err;

	err = journal_next_log_block(journal, &blocknr);

	if (err)
		return NULL;

	bh = __getblk(journal->j_dev, blocknr, journal->j_blocksize);
	lock_buffer(bh);
	memset(bh->b_data, 0, journal->j_blocksize);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	BUFFER_TRACE(bh, "return this buffer");
	return journal_add_journal_head(bh);
}

/*
 * Management for journal control blocks: functions to create and
 * destroy journal_t structures, and to initialise and read existing
 * journal blocks from disk.  */

/* First: create and setup a journal_t object in memory.  We initialise
 * very few fields yet: that has to wait until we have created the
 * journal structures from from scratch, or loaded them from disk. */

static journal_t * journal_init_common (void)
{
	journal_t *journal;
	int err;

	journal = kzalloc(sizeof(*journal), GFP_KERNEL);
	if (!journal)
		goto fail;

	init_waitqueue_head(&journal->j_wait_transaction_locked);
	init_waitqueue_head(&journal->j_wait_logspace);
	init_waitqueue_head(&journal->j_wait_done_commit);
	init_waitqueue_head(&journal->j_wait_checkpoint);
	init_waitqueue_head(&journal->j_wait_commit);
	init_waitqueue_head(&journal->j_wait_updates);
	mutex_init(&journal->j_barrier);
	mutex_init(&journal->j_checkpoint_mutex);
	spin_lock_init(&journal->j_revoke_lock);
	spin_lock_init(&journal->j_list_lock);
	spin_lock_init(&journal->j_state_lock);

	journal->j_commit_interval = (HZ * JBD_DEFAULT_MAX_COMMIT_AGE);

	/* The journal is marked for error until we succeed with recovery! */
	journal->j_flags = JFS_ABORT;

	/* Set up a default-sized revoke table for the new mount. */
	err = journal_init_revoke(journal, JOURNAL_REVOKE_DEFAULT_HASH);
	if (err) {
		kfree(journal);
		goto fail;
	}
	return journal;
fail:
	return NULL;
}

/* journal_init_dev and journal_init_inode:
 *
 * Create a journal structure assigned some fixed set of disk blocks to
 * the journal.  We don't actually touch those disk blocks yet, but we
 * need to set up all of the mapping information to tell the journaling
 * system where the journal blocks are.
 *
 */

/**
 *  journal_t * journal_init_dev() - creates and initialises a journal structure
 *  @bdev: Block device on which to create the journal
 *  @fs_dev: Device which hold journalled filesystem for this journal.
 *  @start: Block nr Start of journal.
 *  @len:  Length of the journal in blocks.
 *  @blocksize: blocksize of journalling device
 *
 *  Returns: a newly created journal_t *
 *
 *  journal_init_dev creates a journal which maps a fixed contiguous
 *  range of blocks on an arbitrary block device.
 *
 */
journal_t * journal_init_dev(struct block_device *bdev,
			struct block_device *fs_dev,
			int start, int len, int blocksize)
{
	journal_t *journal = journal_init_common();
	struct buffer_head *bh;
	int n;

	if (!journal)
		return NULL;

	/* journal descriptor can store up to n blocks -bzzz */
	journal->j_blocksize = blocksize;
	n = journal->j_blocksize / sizeof(journal_block_tag_t);
	journal->j_wbufsize = n;
	journal->j_wbuf = kmalloc(n * sizeof(struct buffer_head*), GFP_KERNEL);
	if (!journal->j_wbuf) {
		printk(KERN_ERR "%s: Cant allocate bhs for commit thread\n",
			__func__);
		kfree(journal);
		journal = NULL;
		goto out;
	}
	journal->j_dev = bdev;
	journal->j_fs_dev = fs_dev;
	journal->j_blk_offset = start;
	journal->j_maxlen = len;

	bh = __getblk(journal->j_dev, start, journal->j_blocksize);
	J_ASSERT(bh != NULL);
	journal->j_sb_buffer = bh;
	journal->j_superblock = (journal_superblock_t *)bh->b_data;
out:
	return journal;
}

/**
 *  journal_t * journal_init_inode () - creates a journal which maps to a inode.
 *  @inode: An inode to create the journal in
 *
 * journal_init_inode creates a journal which maps an on-disk inode as
 * the journal.  The inode must exist already, must support bmap() and
 * must have all data blocks preallocated.
 */
journal_t * journal_init_inode (struct inode *inode)
{
	struct buffer_head *bh;
	journal_t *journal = journal_init_common();
	int err;
	int n;
	unsigned long blocknr;

	if (!journal)
		return NULL;

	journal->j_dev = journal->j_fs_dev = inode->i_sb->s_bdev;
	journal->j_inode = inode;
	jbd_debug(1,
		  "journal %p: inode %s/%ld, size %Ld, bits %d, blksize %ld\n",
		  journal, inode->i_sb->s_id, inode->i_ino,
		  (long long) inode->i_size,
		  inode->i_sb->s_blocksize_bits, inode->i_sb->s_blocksize);

	journal->j_maxlen = inode->i_size >> inode->i_sb->s_blocksize_bits;
	journal->j_blocksize = inode->i_sb->s_blocksize;

	/* journal descriptor can store up to n blocks -bzzz */
	n = journal->j_blocksize / sizeof(journal_block_tag_t);
	journal->j_wbufsize = n;
	journal->j_wbuf = kmalloc(n * sizeof(struct buffer_head*), GFP_KERNEL);
	if (!journal->j_wbuf) {
		printk(KERN_ERR "%s: Cant allocate bhs for commit thread\n",
			__func__);
		kfree(journal);
		return NULL;
	}

	err = journal_bmap(journal, 0, &blocknr);
	/* If that failed, give up */
	if (err) {
		printk(KERN_ERR "%s: Cannnot locate journal superblock\n",
		       __func__);
		kfree(journal);
		return NULL;
	}

	bh = __getblk(journal->j_dev, blocknr, journal->j_blocksize);
	J_ASSERT(bh != NULL);
	journal->j_sb_buffer = bh;
	journal->j_superblock = (journal_superblock_t *)bh->b_data;

	return journal;
}

/*
 * If the journal init or create aborts, we need to mark the journal
 * superblock as being NULL to prevent the journal destroy from writing
 * back a bogus superblock.
 */
static void journal_fail_superblock (journal_t *journal)
{
	struct buffer_head *bh = journal->j_sb_buffer;
	brelse(bh);
	journal->j_sb_buffer = NULL;
}

/*
 * Given a journal_t structure, initialise the various fields for
 * startup of a new journaling session.  We use this both when creating
 * a journal, and after recovering an old journal to reset it for
 * subsequent use.
 */

static int journal_reset(journal_t *journal)
{
	journal_superblock_t *sb = journal->j_superblock;
	unsigned long first, last;

	first = be32_to_cpu(sb->s_first);
	last = be32_to_cpu(sb->s_maxlen);

	journal->j_first = first;
	journal->j_last = last;

	journal->j_head = first;
	journal->j_tail = first;
	journal->j_free = last - first;

	journal->j_tail_sequence = journal->j_transaction_sequence;
	journal->j_commit_sequence = journal->j_transaction_sequence - 1;
	journal->j_commit_request = journal->j_commit_sequence;

	journal->j_max_transaction_buffers = journal->j_maxlen / 4;

	/* Add the dynamic fields and write it to disk. */
	journal_update_superblock(journal, 1);
	return journal_start_thread(journal);
}

/**
 * int journal_create() - Initialise the new journal file
 * @journal: Journal to create. This structure must have been initialised
 *
 * Given a journal_t structure which tells us which disk blocks we can
 * use, create a new journal superblock and initialise all of the
 * journal fields from scratch.
 **/
int journal_create(journal_t *journal)
{
	unsigned long blocknr;
	struct buffer_head *bh;
	journal_superblock_t *sb;
	int i, err;

	if (journal->j_maxlen < JFS_MIN_JOURNAL_BLOCKS) {
		printk (KERN_ERR "Journal length (%d blocks) too short.\n",
			journal->j_maxlen);
		journal_fail_superblock(journal);
		return -EINVAL;
	}

	if (journal->j_inode == NULL) {
		/*
		 * We don't know what block to start at!
		 */
		printk(KERN_EMERG
		       "%s: creation of journal on external device!\n",
		       __func__);
		BUG();
	}

	/* Zero out the entire journal on disk.  We cannot afford to
	   have any blocks on disk beginning with JFS_MAGIC_NUMBER. */
	jbd_debug(1, "JBD: Zeroing out journal blocks...\n");
	for (i = 0; i < journal->j_maxlen; i++) {
		err = journal_bmap(journal, i, &blocknr);
		if (err)
			return err;
		bh = __getblk(journal->j_dev, blocknr, journal->j_blocksize);
		lock_buffer(bh);
		memset (bh->b_data, 0, journal->j_blocksize);
		BUFFER_TRACE(bh, "marking dirty");
		mark_buffer_dirty(bh);
		BUFFER_TRACE(bh, "marking uptodate");
		set_buffer_uptodate(bh);
		unlock_buffer(bh);
		__brelse(bh);
	}

	sync_blockdev(journal->j_dev);
	jbd_debug(1, "JBD: journal cleared.\n");

	/* OK, fill in the initial static fields in the new superblock */
	sb = journal->j_superblock;

	sb->s_header.h_magic	 = cpu_to_be32(JFS_MAGIC_NUMBER);
	sb->s_header.h_blocktype = cpu_to_be32(JFS_SUPERBLOCK_V2);

	sb->s_blocksize	= cpu_to_be32(journal->j_blocksize);
	sb->s_maxlen	= cpu_to_be32(journal->j_maxlen);
	sb->s_first	= cpu_to_be32(1);

	journal->j_transaction_sequence = 1;

	journal->j_flags &= ~JFS_ABORT;
	journal->j_format_version = 2;

	return journal_reset(journal);
}

/**
 * void journal_update_superblock() - Update journal sb on disk.
 * @journal: The journal to update.
 * @wait: Set to '0' if you don't want to wait for IO completion.
 *
 * Update a journal's dynamic superblock fields and write it to disk,
 * optionally waiting for the IO to complete.
 */
void journal_update_superblock(journal_t *journal, int wait)
{
	journal_superblock_t *sb = journal->j_superblock;
	struct buffer_head *bh = journal->j_sb_buffer;

	/*
	 * As a special case, if the on-disk copy is already marked as needing
	 * no recovery (s_start == 0) and there are no outstanding transactions
	 * in the filesystem, then we can safely defer the superblock update
	 * until the next commit by setting JFS_FLUSHED.  This avoids
	 * attempting a write to a potential-readonly device.
	 */
	if (sb->s_start == 0 && journal->j_tail_sequence ==
				journal->j_transaction_sequence) {
		jbd_debug(1,"JBD: Skipping superblock update on recovered sb "
			"(start %ld, seq %d, errno %d)\n",
			journal->j_tail, journal->j_tail_sequence,
			journal->j_errno);
		goto out;
	}

	spin_lock(&journal->j_state_lock);
	jbd_debug(1,"JBD: updating superblock (start %ld, seq %d, errno %d)\n",
		  journal->j_tail, journal->j_tail_sequence, journal->j_errno);

	sb->s_sequence = cpu_to_be32(journal->j_tail_sequence);
	sb->s_start    = cpu_to_be32(journal->j_tail);
	sb->s_errno    = cpu_to_be32(journal->j_errno);
	spin_unlock(&journal->j_state_lock);

	BUFFER_TRACE(bh, "marking dirty");
	mark_buffer_dirty(bh);
	if (wait)
		sync_dirty_buffer(bh);
	else
		ll_rw_block(SWRITE, 1, &bh);

out:
	/* If we have just flushed the log (by marking s_start==0), then
	 * any future commit will have to be careful to update the
	 * superblock again to re-record the true start of the log. */

	spin_lock(&journal->j_state_lock);
	if (sb->s_start)
		journal->j_flags &= ~JFS_FLUSHED;
	else
		journal->j_flags |= JFS_FLUSHED;
	spin_unlock(&journal->j_state_lock);
}

/*
 * Read the superblock for a given journal, performing initial
 * validation of the format.
 */

static int journal_get_superblock(journal_t *journal)
{
	struct buffer_head *bh;
	journal_superblock_t *sb;
	int err = -EIO;

	bh = journal->j_sb_buffer;

	J_ASSERT(bh != NULL);
	if (!buffer_uptodate(bh)) {
		ll_rw_block(READ, 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			printk (KERN_ERR
				"JBD: IO error reading journal superblock\n");
			goto out;
		}
	}

	sb = journal->j_superblock;

	err = -EINVAL;

	if (sb->s_header.h_magic != cpu_to_be32(JFS_MAGIC_NUMBER) ||
	    sb->s_blocksize != cpu_to_be32(journal->j_blocksize)) {
		printk(KERN_WARNING "JBD: no valid journal superblock found\n");
		goto out;
	}

	switch(be32_to_cpu(sb->s_header.h_blocktype)) {
	case JFS_SUPERBLOCK_V1:
		journal->j_format_version = 1;
		break;
	case JFS_SUPERBLOCK_V2:
		journal->j_format_version = 2;
		break;
	default:
		printk(KERN_WARNING "JBD: unrecognised superblock format ID\n");
		goto out;
	}

	if (be32_to_cpu(sb->s_maxlen) < journal->j_maxlen)
		journal->j_maxlen = be32_to_cpu(sb->s_maxlen);
	else if (be32_to_cpu(sb->s_maxlen) > journal->j_maxlen) {
		printk (KERN_WARNING "JBD: journal file too short\n");
		goto out;
	}

	return 0;

out:
	journal_fail_superblock(journal);
	return err;
}

/*
 * Load the on-disk journal superblock and read the key fields into the
 * journal_t.
 */

static int load_superblock(journal_t *journal)
{
	int err;
	journal_superblock_t *sb;

	err = journal_get_superblock(journal);
	if (err)
		return err;

	sb = journal->j_superblock;

	journal->j_tail_sequence = be32_to_cpu(sb->s_sequence);
	journal->j_tail = be32_to_cpu(sb->s_start);
	journal->j_first = be32_to_cpu(sb->s_first);
	journal->j_last = be32_to_cpu(sb->s_maxlen);
	journal->j_errno = be32_to_cpu(sb->s_errno);

	return 0;
}


/**
 * int journal_load() - Read journal from disk.
 * @journal: Journal to act on.
 *
 * Given a journal_t structure which tells us which disk blocks contain
 * a journal, read the journal from disk to initialise the in-memory
 * structures.
 */
int journal_load(journal_t *journal)
{
	int err;
	journal_superblock_t *sb;

	err = load_superblock(journal);
	if (err)
		return err;

	sb = journal->j_superblock;
	/* If this is a V2 superblock, then we have to check the
	 * features flags on it. */

	if (journal->j_format_version >= 2) {
		if ((sb->s_feature_ro_compat &
		     ~cpu_to_be32(JFS_KNOWN_ROCOMPAT_FEATURES)) ||
		    (sb->s_feature_incompat &
		     ~cpu_to_be32(JFS_KNOWN_INCOMPAT_FEATURES))) {
			printk (KERN_WARNING
				"JBD: Unrecognised features on journal\n");
			return -EINVAL;
		}
	}

	/* Let the recovery code check whether it needs to recover any
	 * data from the journal. */
	if (journal_recover(journal))
		goto recovery_error;

	/* OK, we've finished with the dynamic journal bits:
	 * reinitialise the dynamic contents of the superblock in memory
	 * and reset them on disk. */
	if (journal_reset(journal))
		goto recovery_error;

	journal->j_flags &= ~JFS_ABORT;
	journal->j_flags |= JFS_LOADED;
	return 0;

recovery_error:
	printk (KERN_WARNING "JBD: recovery failed\n");
	return -EIO;
}

/**
 * void journal_destroy() - Release a journal_t structure.
 * @journal: Journal to act on.
 *
 * Release a journal_t structure once it is no longer in use by the
 * journaled object.
 * Return <0 if we couldn't clean up the journal.
 */
int journal_destroy(journal_t *journal)
{
	int err = 0;

	/* Wait for the commit thread to wake up and die. */
	journal_kill_thread(journal);

	/* Force a final log commit */
	if (journal->j_running_transaction)
		journal_commit_transaction(journal);

	/* Force any old transactions to disk */

	/* Totally anal locking here... */
	spin_lock(&journal->j_list_lock);
	while (journal->j_checkpoint_transactions != NULL) {
		spin_unlock(&journal->j_list_lock);
		log_do_checkpoint(journal);
		spin_lock(&journal->j_list_lock);
	}

	J_ASSERT(journal->j_running_transaction == NULL);
	J_ASSERT(journal->j_committing_transaction == NULL);
	J_ASSERT(journal->j_checkpoint_transactions == NULL);
	spin_unlock(&journal->j_list_lock);

	if (journal->j_sb_buffer) {
		if (!is_journal_aborted(journal)) {
			/* We can now mark the journal as empty. */
			journal->j_tail = 0;
			journal->j_tail_sequence =
				++journal->j_transaction_sequence;
			journal_update_superblock(journal, 1);
		} else {
			err = -EIO;
		}
		brelse(journal->j_sb_buffer);
	}

	if (journal->j_inode)
		iput(journal->j_inode);
	if (journal->j_revoke)
		journal_destroy_revoke(journal);
	kfree(journal->j_wbuf);
	kfree(journal);

	return err;
}


/**
 *int journal_check_used_features () - Check if features specified are used.
 * @journal: Journal to check.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Check whether the journal uses all of a given set of
 * features.  Return true (non-zero) if it does.
 **/

int journal_check_used_features (journal_t *journal, unsigned long compat,
				 unsigned long ro, unsigned long incompat)
{
	journal_superblock_t *sb;

	if (!compat && !ro && !incompat)
		return 1;
	if (journal->j_format_version == 1)
		return 0;

	sb = journal->j_superblock;

	if (((be32_to_cpu(sb->s_feature_compat) & compat) == compat) &&
	    ((be32_to_cpu(sb->s_feature_ro_compat) & ro) == ro) &&
	    ((be32_to_cpu(sb->s_feature_incompat) & incompat) == incompat))
		return 1;

	return 0;
}

/**
 * int journal_check_available_features() - Check feature set in journalling layer
 * @journal: Journal to check.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Check whether the journaling code supports the use of
 * all of a given set of features on this journal.  Return true
 * (non-zero) if it can. */

int journal_check_available_features (journal_t *journal, unsigned long compat,
				      unsigned long ro, unsigned long incompat)
{
	journal_superblock_t *sb;

	if (!compat && !ro && !incompat)
		return 1;

	sb = journal->j_superblock;

	/* We can support any known requested features iff the
	 * superblock is in version 2.  Otherwise we fail to support any
	 * extended sb features. */

	if (journal->j_format_version != 2)
		return 0;

	if ((compat   & JFS_KNOWN_COMPAT_FEATURES) == compat &&
	    (ro       & JFS_KNOWN_ROCOMPAT_FEATURES) == ro &&
	    (incompat & JFS_KNOWN_INCOMPAT_FEATURES) == incompat)
		return 1;

	return 0;
}

/**
 * int journal_set_features () - Mark a given journal feature in the superblock
 * @journal: Journal to act on.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Mark a given journal feature as present on the
 * superblock.  Returns true if the requested features could be set.
 *
 */

int journal_set_features (journal_t *journal, unsigned long compat,
			  unsigned long ro, unsigned long incompat)
{
	journal_superblock_t *sb;

	if (journal_check_used_features(journal, compat, ro, incompat))
		return 1;

	if (!journal_check_available_features(journal, compat, ro, incompat))
		return 0;

	jbd_debug(1, "Setting new features 0x%lx/0x%lx/0x%lx\n",
		  compat, ro, incompat);

	sb = journal->j_superblock;

	sb->s_feature_compat    |= cpu_to_be32(compat);
	sb->s_feature_ro_compat |= cpu_to_be32(ro);
	sb->s_feature_incompat  |= cpu_to_be32(incompat);

	return 1;
}


/**
 * int journal_update_format () - Update on-disk journal structure.
 * @journal: Journal to act on.
 *
 * Given an initialised but unloaded journal struct, poke about in the
 * on-disk structure to update it to the most recent supported version.
 */
int journal_update_format (journal_t *journal)
{
	journal_superblock_t *sb;
	int err;

	err = journal_get_superblock(journal);
	if (err)
		return err;

	sb = journal->j_superblock;

	switch (be32_to_cpu(sb->s_header.h_blocktype)) {
	case JFS_SUPERBLOCK_V2:
		return 0;
	case JFS_SUPERBLOCK_V1:
		return journal_convert_superblock_v1(journal, sb);
	default:
		break;
	}
	return -EINVAL;
}

static int journal_convert_superblock_v1(journal_t *journal,
					 journal_superblock_t *sb)
{
	int offset, blocksize;
	struct buffer_head *bh;

	printk(KERN_WARNING
		"JBD: Converting superblock from version 1 to 2.\n");

	/* Pre-initialise new fields to zero */
	offset = ((char *) &(sb->s_feature_compat)) - ((char *) sb);
	blocksize = be32_to_cpu(sb->s_blocksize);
	memset(&sb->s_feature_compat, 0, blocksize-offset);

	sb->s_nr_users = cpu_to_be32(1);
	sb->s_header.h_blocktype = cpu_to_be32(JFS_SUPERBLOCK_V2);
	journal->j_format_version = 2;

	bh = journal->j_sb_buffer;
	BUFFER_TRACE(bh, "marking dirty");
	mark_buffer_dirty(bh);
	sync_dirty_buffer(bh);
	return 0;
}


/**
 * int journal_flush () - Flush journal
 * @journal: Journal to act on.
 *
 * Flush all data for a given journal to disk and empty the journal.
 * Filesystems can use this when remounting readonly to ensure that
 * recovery does not need to happen on remount.
 */

int journal_flush(journal_t *journal)
{
	int err = 0;
	transaction_t *transaction = NULL;
	unsigned long old_tail;

	spin_lock(&journal->j_state_lock);

	/* Force everything buffered to the log... */
	if (journal->j_running_transaction) {
		transaction = journal->j_running_transaction;
		__log_start_commit(journal, transaction->t_tid);
	} else if (journal->j_committing_transaction)
		transaction = journal->j_committing_transaction;

	/* Wait for the log commit to complete... */
	if (transaction) {
		tid_t tid = transaction->t_tid;

		spin_unlock(&journal->j_state_lock);
		log_wait_commit(journal, tid);
	} else {
		spin_unlock(&journal->j_state_lock);
	}

	/* ...and flush everything in the log out to disk. */
	spin_lock(&journal->j_list_lock);
	while (!err && journal->j_checkpoint_transactions != NULL) {
		spin_unlock(&journal->j_list_lock);
		mutex_lock(&journal->j_checkpoint_mutex);
		err = log_do_checkpoint(journal);
		mutex_unlock(&journal->j_checkpoint_mutex);
		spin_lock(&journal->j_list_lock);
	}
	spin_unlock(&journal->j_list_lock);

	if (is_journal_aborted(journal))
		return -EIO;

	cleanup_journal_tail(journal);

	/* Finally, mark the journal as really needing no recovery.
	 * This sets s_start==0 in the underlying superblock, which is
	 * the magic code for a fully-recovered superblock.  Any future
	 * commits of data to the journal will restore the current
	 * s_start value. */
	spin_lock(&journal->j_state_lock);
	old_tail = journal->j_tail;
	journal->j_tail = 0;
	spin_unlock(&journal->j_state_lock);
	journal_update_superblock(journal, 1);
	spin_lock(&journal->j_state_lock);
	journal->j_tail = old_tail;

	J_ASSERT(!journal->j_running_transaction);
	J_ASSERT(!journal->j_committing_transaction);
	J_ASSERT(!journal->j_checkpoint_transactions);
	J_ASSERT(journal->j_head == journal->j_tail);
	J_ASSERT(journal->j_tail_sequence == journal->j_transaction_sequence);
	spin_unlock(&journal->j_state_lock);
	return 0;
}

/**
 * int journal_wipe() - Wipe journal contents
 * @journal: Journal to act on.
 * @write: flag (see below)
 *
 * Wipe out all of the contents of a journal, safely.  This will produce
 * a warning if the journal contains any valid recovery information.
 * Must be called between journal_init_*() and journal_load().
 *
 * If 'write' is non-zero, then we wipe out the journal on disk; otherwise
 * we merely suppress recovery.
 */

int journal_wipe(journal_t *journal, int write)
{
	journal_superblock_t *sb;
	int err = 0;

	J_ASSERT (!(journal->j_flags & JFS_LOADED));

	err = load_superblock(journal);
	if (err)
		return err;

	sb = journal->j_superblock;

	if (!journal->j_tail)
		goto no_recovery;

	printk (KERN_WARNING "JBD: %s recovery information on journal\n",
		write ? "Clearing" : "Ignoring");

	err = journal_skip_recovery(journal);
	if (write)
		journal_update_superblock(journal, 1);

 no_recovery:
	return err;
}

/*
 * journal_dev_name: format a character string to describe on what
 * device this journal is present.
 */

static const char *journal_dev_name(journal_t *journal, char *buffer)
{
	struct block_device *bdev;

	if (journal->j_inode)
		bdev = journal->j_inode->i_sb->s_bdev;
	else
		bdev = journal->j_dev;

	return bdevname(bdev, buffer);
}

/*
 * Journal abort has very specific semantics, which we describe
 * for journal abort.
 *
 * Two internal function, which provide abort to te jbd layer
 * itself are here.
 */

/*
 * Quick version for internal journal use (doesn't lock the journal).
 * Aborts hard --- we mark the abort as occurred, but do _nothing_ else,
 * and don't attempt to make any other journal updates.
 */
static void __journal_abort_hard(journal_t *journal)
{
	transaction_t *transaction;
	char b[BDEVNAME_SIZE];

	if (journal->j_flags & JFS_ABORT)
		return;

	printk(KERN_ERR "Aborting journal on device %s.\n",
		journal_dev_name(journal, b));

	spin_lock(&journal->j_state_lock);
	journal->j_flags |= JFS_ABORT;
	transaction = journal->j_running_transaction;
	if (transaction)
		__log_start_commit(journal, transaction->t_tid);
	spin_unlock(&journal->j_state_lock);
}

/* Soft abort: record the abort error status in the journal superblock,
 * but don't do any other IO. */
static void __journal_abort_soft (journal_t *journal, int errno)
{
	if (journal->j_flags & JFS_ABORT)
		return;

	if (!journal->j_errno)
		journal->j_errno = errno;

	__journal_abort_hard(journal);

	if (errno)
		journal_update_superblock(journal, 1);
}

/**
 * void journal_abort () - Shutdown the journal immediately.
 * @journal: the journal to shutdown.
 * @errno:   an error number to record in the journal indicating
 *           the reason for the shutdown.
 *
 * Perform a complete, immediate shutdown of the ENTIRE
 * journal (not of a single transaction).  This operation cannot be
 * undone without closing and reopening the journal.
 *
 * The journal_abort function is intended to support higher level error
 * recovery mechanisms such as the ext2/ext3 remount-readonly error
 * mode.
 *
 * Journal abort has very specific semantics.  Any existing dirty,
 * unjournaled buffers in the main filesystem will still be written to
 * disk by bdflush, but the journaling mechanism will be suspended
 * immediately and no further transaction commits will be honoured.
 *
 * Any dirty, journaled buffers will be written back to disk without
 * hitting the journal.  Atomicity cannot be guaranteed on an aborted
 * filesystem, but we _do_ attempt to leave as much data as possible
 * behind for fsck to use for cleanup.
 *
 * Any attempt to get a new transaction handle on a journal which is in
 * ABORT state will just result in an -EROFS error return.  A
 * journal_stop on an existing handle will return -EIO if we have
 * entered abort state during the update.
 *
 * Recursive transactions are not disturbed by journal abort until the
 * final journal_stop, which will receive the -EIO error.
 *
 * Finally, the journal_abort call allows the caller to supply an errno
 * which will be recorded (if possible) in the journal superblock.  This
 * allows a client to record failure conditions in the middle of a
 * transaction without having to complete the transaction to record the
 * failure to disk.  ext3_error, for example, now uses this
 * functionality.
 *
 * Errors which originate from within the journaling layer will NOT
 * supply an errno; a null errno implies that absolutely no further
 * writes are done to the journal (unless there are any already in
 * progress).
 *
 */

void journal_abort(journal_t *journal, int errno)
{
	__journal_abort_soft(journal, errno);
}

/**
 * int journal_errno () - returns the journal's error state.
 * @journal: journal to examine.
 *
 * This is the errno numbet set with journal_abort(), the last
 * time the journal was mounted - if the journal was stopped
 * without calling abort this will be 0.
 *
 * If the journal has been aborted on this mount time -EROFS will
 * be returned.
 */
int journal_errno(journal_t *journal)
{
	int err;

	spin_lock(&journal->j_state_lock);
	if (journal->j_flags & JFS_ABORT)
		err = -EROFS;
	else
		err = journal->j_errno;
	spin_unlock(&journal->j_state_lock);
	return err;
}

/**
 * int journal_clear_err () - clears the journal's error state
 * @journal: journal to act on.
 *
 * An error must be cleared or Acked to take a FS out of readonly
 * mode.
 */
int journal_clear_err(journal_t *journal)
{
	int err = 0;

	spin_lock(&journal->j_state_lock);
	if (journal->j_flags & JFS_ABORT)
		err = -EROFS;
	else
		journal->j_errno = 0;
	spin_unlock(&journal->j_state_lock);
	return err;
}

/**
 * void journal_ack_err() - Ack journal err.
 * @journal: journal to act on.
 *
 * An error must be cleared or Acked to take a FS out of readonly
 * mode.
 */
void journal_ack_err(journal_t *journal)
{
	spin_lock(&journal->j_state_lock);
	if (journal->j_errno)
		journal->j_flags |= JFS_ACK_ERR;
	spin_unlock(&journal->j_state_lock);
}

int journal_blocks_per_page(struct inode *inode)
{
	return 1 << (PAGE_CACHE_SHIFT - inode->i_sb->s_blocksize_bits);
}

/*
 * Journal_head storage management
 */
static struct kmem_cache *journal_head_cache;
#ifdef CONFIG_JBD_DEBUG
static atomic_t nr_journal_heads = ATOMIC_INIT(0);
#endif

static int journal_init_journal_head_cache(void)
{
	int retval;

	J_ASSERT(journal_head_cache == NULL);
	journal_head_cache = kmem_cache_create("journal_head",
				sizeof(struct journal_head),
				0,		/* offset */
				SLAB_TEMPORARY,	/* flags */
				NULL);		/* ctor */
	retval = 0;
	if (!journal_head_cache) {
		retval = -ENOMEM;
		printk(KERN_EMERG "JBD: no memory for journal_head cache\n");
	}
	return retval;
}

static void journal_destroy_journal_head_cache(void)
{
	if (journal_head_cache) {
		kmem_cache_destroy(journal_head_cache);
		journal_head_cache = NULL;
	}
}

/*
 * journal_head splicing and dicing
 */
static struct journal_head *journal_alloc_journal_head(void)
{
	struct journal_head *ret;
	static unsigned long last_warning;

#ifdef CONFIG_JBD_DEBUG
	atomic_inc(&nr_journal_heads);
#endif
	ret = kmem_cache_alloc(journal_head_cache, GFP_NOFS);
	if (ret == NULL) {
		jbd_debug(1, "out of memory for journal_head\n");
		if (time_after(jiffies, last_warning + 5*HZ)) {
			printk(KERN_NOTICE "ENOMEM in %s, retrying.\n",
			       __func__);
			last_warning = jiffies;
		}
		while (ret == NULL) {
			yield();
			ret = kmem_cache_alloc(journal_head_cache, GFP_NOFS);
		}
	}
	return ret;
}

static void journal_free_journal_head(struct journal_head *jh)
{
#ifdef CONFIG_JBD_DEBUG
	atomic_dec(&nr_journal_heads);
	memset(jh, JBD_POISON_FREE, sizeof(*jh));
#endif
	kmem_cache_free(journal_head_cache, jh);
}

/*
 * A journal_head is attached to a buffer_head whenever JBD has an
 * interest in the buffer.
 *
 * Whenever a buffer has an attached journal_head, its ->b_state:BH_JBD bit
 * is set.  This bit is tested in core kernel code where we need to take
 * JBD-specific actions.  Testing the zeroness of ->b_private is not reliable
 * there.
 *
 * When a buffer has its BH_JBD bit set, its ->b_count is elevated by one.
 *
 * When a buffer has its BH_JBD bit set it is immune from being released by
 * core kernel code, mainly via ->b_count.
 *
 * A journal_head may be detached from its buffer_head when the journal_head's
 * b_transaction, b_cp_transaction and b_next_transaction pointers are NULL.
 * Various places in JBD call journal_remove_journal_head() to indicate that the
 * journal_head can be dropped if needed.
 *
 * Various places in the kernel want to attach a journal_head to a buffer_head
 * _before_ attaching the journal_head to a transaction.  To protect the
 * journal_head in this situation, journal_add_journal_head elevates the
 * journal_head's b_jcount refcount by one.  The caller must call
 * journal_put_journal_head() to undo this.
 *
 * So the typical usage would be:
 *
 *	(Attach a journal_head if needed.  Increments b_jcount)
 *	struct journal_head *jh = journal_add_journal_head(bh);
 *	...
 *	jh->b_transaction = xxx;
 *	journal_put_journal_head(jh);
 *
 * Now, the journal_head's b_jcount is zero, but it is safe from being released
 * because it has a non-zero b_transaction.
 */

/*
 * Give a buffer_head a journal_head.
 *
 * Doesn't need the journal lock.
 * May sleep.
 */
struct journal_head *journal_add_journal_head(struct buffer_head *bh)
{
	struct journal_head *jh;
	struct journal_head *new_jh = NULL;

repeat:
	if (!buffer_jbd(bh)) {
		new_jh = journal_alloc_journal_head();
		memset(new_jh, 0, sizeof(*new_jh));
	}

	jbd_lock_bh_journal_head(bh);
	if (buffer_jbd(bh)) {
		jh = bh2jh(bh);
	} else {
		J_ASSERT_BH(bh,
			(atomic_read(&bh->b_count) > 0) ||
			(bh->b_page && bh->b_page->mapping));

		if (!new_jh) {
			jbd_unlock_bh_journal_head(bh);
			goto repeat;
		}

		jh = new_jh;
		new_jh = NULL;		/* We consumed it */
		set_buffer_jbd(bh);
		bh->b_private = jh;
		jh->b_bh = bh;
		get_bh(bh);
		BUFFER_TRACE(bh, "added journal_head");
	}
	jh->b_jcount++;
	jbd_unlock_bh_journal_head(bh);
	if (new_jh)
		journal_free_journal_head(new_jh);
	return bh->b_private;
}

/*
 * Grab a ref against this buffer_head's journal_head.  If it ended up not
 * having a journal_head, return NULL
 */
struct journal_head *journal_grab_journal_head(struct buffer_head *bh)
{
	struct journal_head *jh = NULL;

	jbd_lock_bh_journal_head(bh);
	if (buffer_jbd(bh)) {
		jh = bh2jh(bh);
		jh->b_jcount++;
	}
	jbd_unlock_bh_journal_head(bh);
	return jh;
}

static void __journal_remove_journal_head(struct buffer_head *bh)
{
	struct journal_head *jh = bh2jh(bh);

	J_ASSERT_JH(jh, jh->b_jcount >= 0);

	get_bh(bh);
	if (jh->b_jcount == 0) {
		if (jh->b_transaction == NULL &&
				jh->b_next_transaction == NULL &&
				jh->b_cp_transaction == NULL) {
			J_ASSERT_JH(jh, jh->b_jlist == BJ_None);
			J_ASSERT_BH(bh, buffer_jbd(bh));
			J_ASSERT_BH(bh, jh2bh(jh) == bh);
			BUFFER_TRACE(bh, "remove journal_head");
			if (jh->b_frozen_data) {
				printk(KERN_WARNING "%s: freeing "
						"b_frozen_data\n",
						__func__);
				jbd_free(jh->b_frozen_data, bh->b_size);
			}
			if (jh->b_committed_data) {
				printk(KERN_WARNING "%s: freeing "
						"b_committed_data\n",
						__func__);
				jbd_free(jh->b_committed_data, bh->b_size);
			}
			bh->b_private = NULL;
			jh->b_bh = NULL;	/* debug, really */
			clear_buffer_jbd(bh);
			__brelse(bh);
			journal_free_journal_head(jh);
		} else {
			BUFFER_TRACE(bh, "journal_head was locked");
		}
	}
}

/*
 * journal_remove_journal_head(): if the buffer isn't attached to a transaction
 * and has a zero b_jcount then remove and release its journal_head.   If we did
 * see that the buffer is not used by any transaction we also "logically"
 * decrement ->b_count.
 *
 * We in fact take an additional increment on ->b_count as a convenience,
 * because the caller usually wants to do additional things with the bh
 * after calling here.
 * The caller of journal_remove_journal_head() *must* run __brelse(bh) at some
 * time.  Once the caller has run __brelse(), the buffer is eligible for
 * reaping by try_to_free_buffers().
 */
void journal_remove_journal_head(struct buffer_head *bh)
{
	jbd_lock_bh_journal_head(bh);
	__journal_remove_journal_head(bh);
	jbd_unlock_bh_journal_head(bh);
}

/*
 * Drop a reference on the passed journal_head.  If it fell to zero then try to
 * release the journal_head from the buffer_head.
 */
void journal_put_journal_head(struct journal_head *jh)
{
	struct buffer_head *bh = jh2bh(jh);

	jbd_lock_bh_journal_head(bh);
	J_ASSERT_JH(jh, jh->b_jcount > 0);
	--jh->b_jcount;
	if (!jh->b_jcount && !jh->b_transaction) {
		__journal_remove_journal_head(bh);
		__brelse(bh);
	}
	jbd_unlock_bh_journal_head(bh);
}

/*
 * debugfs tunables
 */
#ifdef CONFIG_JBD_DEBUG

u8 journal_enable_debug __read_mostly;
EXPORT_SYMBOL(journal_enable_debug);

static struct dentry *jbd_debugfs_dir;
static struct dentry *jbd_debug;

static void __init jbd_create_debugfs_entry(void)
{
	jbd_debugfs_dir = debugfs_create_dir("jbd", NULL);
	if (jbd_debugfs_dir)
		jbd_debug = debugfs_create_u8("jbd-debug", S_IRUGO,
					       jbd_debugfs_dir,
					       &journal_enable_debug);
}

static void __exit jbd_remove_debugfs_entry(void)
{
	debugfs_remove(jbd_debug);
	debugfs_remove(jbd_debugfs_dir);
}

#else

static inline void jbd_create_debugfs_entry(void)
{
}

static inline void jbd_remove_debugfs_entry(void)
{
}

#endif

struct kmem_cache *jbd_handle_cache;

static int __init journal_init_handle_cache(void)
{
	jbd_handle_cache = kmem_cache_create("journal_handle",
				sizeof(handle_t),
				0,		/* offset */
				SLAB_TEMPORARY,	/* flags */
				NULL);		/* ctor */
	if (jbd_handle_cache == NULL) {
		printk(KERN_EMERG "JBD: failed to create handle cache\n");
		return -ENOMEM;
	}
	return 0;
}

static void journal_destroy_handle_cache(void)
{
	if (jbd_handle_cache)
		kmem_cache_destroy(jbd_handle_cache);
}

/*
 * Module startup and shutdown
 */

static int __init journal_init_caches(void)
{
	int ret;

	ret = journal_init_revoke_caches();
	if (ret == 0)
		ret = journal_init_journal_head_cache();
	if (ret == 0)
		ret = journal_init_handle_cache();
	return ret;
}

static void journal_destroy_caches(void)
{
	journal_destroy_revoke_caches();
	journal_destroy_journal_head_cache();
	journal_destroy_handle_cache();
}

static int __init journal_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct journal_superblock_s) != 1024);

	ret = journal_init_caches();
	if (ret != 0)
		journal_destroy_caches();
	jbd_create_debugfs_entry();
	return ret;
}

static void __exit journal_exit(void)
{
#ifdef CONFIG_JBD_DEBUG
	int n = atomic_read(&nr_journal_heads);
	if (n)
		printk(KERN_EMERG "JBD: leaked %d journal_heads!\n", n);
#endif
	jbd_remove_debugfs_entry();
	journal_destroy_caches();
}

MODULE_LICENSE("GPL");
module_init(journal_init);
module_exit(journal_exit);

