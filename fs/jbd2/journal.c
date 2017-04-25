/*
 * linux/fs/jbd2/journal.c
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
#include <linux/jbd2.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/freezer.h>
#include <linux/pagemap.h>
#include <linux/kthread.h>
#include <linux/poison.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/math64.h>
#include <linux/hash.h>
#include <linux/log2.h>
#include <linux/vmalloc.h>
#include <linux/backing-dev.h>
#include <linux/bitops.h>
#include <linux/ratelimit.h>

#define CREATE_TRACE_POINTS
#include <trace/events/jbd2.h>

#include <linux/uaccess.h>
#include <asm/page.h>

#ifdef CONFIG_JBD2_DEBUG
ushort jbd2_journal_enable_debug __read_mostly;
EXPORT_SYMBOL(jbd2_journal_enable_debug);

module_param_named(jbd2_debug, jbd2_journal_enable_debug, ushort, 0644);
MODULE_PARM_DESC(jbd2_debug, "Debugging level for jbd2");
#endif

EXPORT_SYMBOL(jbd2_journal_extend);
EXPORT_SYMBOL(jbd2_journal_stop);
EXPORT_SYMBOL(jbd2_journal_lock_updates);
EXPORT_SYMBOL(jbd2_journal_unlock_updates);
EXPORT_SYMBOL(jbd2_journal_get_write_access);
EXPORT_SYMBOL(jbd2_journal_get_create_access);
EXPORT_SYMBOL(jbd2_journal_get_undo_access);
EXPORT_SYMBOL(jbd2_journal_set_triggers);
EXPORT_SYMBOL(jbd2_journal_dirty_metadata);
EXPORT_SYMBOL(jbd2_journal_forget);
#if 0
EXPORT_SYMBOL(journal_sync_buffer);
#endif
EXPORT_SYMBOL(jbd2_journal_flush);
EXPORT_SYMBOL(jbd2_journal_revoke);

EXPORT_SYMBOL(jbd2_journal_init_dev);
EXPORT_SYMBOL(jbd2_journal_init_inode);
EXPORT_SYMBOL(jbd2_journal_check_used_features);
EXPORT_SYMBOL(jbd2_journal_check_available_features);
EXPORT_SYMBOL(jbd2_journal_set_features);
EXPORT_SYMBOL(jbd2_journal_load);
EXPORT_SYMBOL(jbd2_journal_destroy);
EXPORT_SYMBOL(jbd2_journal_abort);
EXPORT_SYMBOL(jbd2_journal_errno);
EXPORT_SYMBOL(jbd2_journal_ack_err);
EXPORT_SYMBOL(jbd2_journal_clear_err);
EXPORT_SYMBOL(jbd2_log_wait_commit);
EXPORT_SYMBOL(jbd2_log_start_commit);
EXPORT_SYMBOL(jbd2_journal_start_commit);
EXPORT_SYMBOL(jbd2_journal_force_commit_nested);
EXPORT_SYMBOL(jbd2_journal_wipe);
EXPORT_SYMBOL(jbd2_journal_blocks_per_page);
EXPORT_SYMBOL(jbd2_journal_invalidatepage);
EXPORT_SYMBOL(jbd2_journal_try_to_free_buffers);
EXPORT_SYMBOL(jbd2_journal_force_commit);
EXPORT_SYMBOL(jbd2_journal_inode_add_write);
EXPORT_SYMBOL(jbd2_journal_inode_add_wait);
EXPORT_SYMBOL(jbd2_journal_init_jbd_inode);
EXPORT_SYMBOL(jbd2_journal_release_jbd_inode);
EXPORT_SYMBOL(jbd2_journal_begin_ordered_truncate);
EXPORT_SYMBOL(jbd2_inode_cache);

static void __journal_abort_soft (journal_t *journal, int errno);
static int jbd2_journal_create_slab(size_t slab_size);

#ifdef CONFIG_JBD2_DEBUG
void __jbd2_debug(int level, const char *file, const char *func,
		  unsigned int line, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (level > jbd2_journal_enable_debug)
		return;
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk(KERN_DEBUG "%s: (%s, %u): %pV\n", file, func, line, &vaf);
	va_end(args);
}
EXPORT_SYMBOL(__jbd2_debug);
#endif

/* Checksumming functions */
static int jbd2_verify_csum_type(journal_t *j, journal_superblock_t *sb)
{
	if (!jbd2_journal_has_csum_v2or3_feature(j))
		return 1;

	return sb->s_checksum_type == JBD2_CRC32C_CHKSUM;
}

static __be32 jbd2_superblock_csum(journal_t *j, journal_superblock_t *sb)
{
	__u32 csum;
	__be32 old_csum;

	old_csum = sb->s_checksum;
	sb->s_checksum = 0;
	csum = jbd2_chksum(j, ~0, (char *)sb, sizeof(journal_superblock_t));
	sb->s_checksum = old_csum;

	return cpu_to_be32(csum);
}

static int jbd2_superblock_csum_verify(journal_t *j, journal_superblock_t *sb)
{
	if (!jbd2_journal_has_csum_v2or3(j))
		return 1;

	return sb->s_checksum == jbd2_superblock_csum(j, sb);
}

static void jbd2_superblock_csum_set(journal_t *j, journal_superblock_t *sb)
{
	if (!jbd2_journal_has_csum_v2or3(j))
		return;

	sb->s_checksum = jbd2_superblock_csum(j, sb);
}

/*
 * Helper function used to manage commit timeouts
 */

static void commit_timeout(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	wake_up_process(p);
}

/*
 * kjournald2: The main thread function used to manage a logging device
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

static int kjournald2(void *arg)
{
	journal_t *journal = arg;
	transaction_t *transaction;

	/*
	 * Set up an interval timer which can be used to trigger a commit wakeup
	 * after the commit interval expires
	 */
	setup_timer(&journal->j_commit_timer, commit_timeout,
			(unsigned long)current);

	set_freezable();

	/* Record that the journal thread is running */
	journal->j_task = current;
	wake_up(&journal->j_wait_done_commit);

	/*
	 * And now, wait forever for commit wakeup events.
	 */
	write_lock(&journal->j_state_lock);

loop:
	if (journal->j_flags & JBD2_UNMOUNT)
		goto end_loop;

	jbd_debug(1, "commit_sequence=%d, commit_request=%d\n",
		journal->j_commit_sequence, journal->j_commit_request);

	if (journal->j_commit_sequence != journal->j_commit_request) {
		jbd_debug(1, "OK, requests differ\n");
		write_unlock(&journal->j_state_lock);
		del_timer_sync(&journal->j_commit_timer);
		jbd2_journal_commit_transaction(journal);
		write_lock(&journal->j_state_lock);
		goto loop;
	}

	wake_up(&journal->j_wait_done_commit);
	if (freezing(current)) {
		/*
		 * The simpler the better. Flushing journal isn't a
		 * good idea, because that depends on threads that may
		 * be already stopped.
		 */
		jbd_debug(1, "Now suspending kjournald2\n");
		write_unlock(&journal->j_state_lock);
		try_to_freeze();
		write_lock(&journal->j_state_lock);
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
		if (journal->j_flags & JBD2_UNMOUNT)
			should_sleep = 0;
		if (should_sleep) {
			write_unlock(&journal->j_state_lock);
			schedule();
			write_lock(&journal->j_state_lock);
		}
		finish_wait(&journal->j_wait_commit, &wait);
	}

	jbd_debug(1, "kjournald2 wakes\n");

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
	del_timer_sync(&journal->j_commit_timer);
	journal->j_task = NULL;
	wake_up(&journal->j_wait_done_commit);
	jbd_debug(1, "Journal thread exiting.\n");
	write_unlock(&journal->j_state_lock);
	return 0;
}

static int jbd2_journal_start_thread(journal_t *journal)
{
	struct task_struct *t;

	t = kthread_run(kjournald2, journal, "jbd2/%s",
			journal->j_devname);
	if (IS_ERR(t))
		return PTR_ERR(t);

	wait_event(journal->j_wait_done_commit, journal->j_task != NULL);
	return 0;
}

static void journal_kill_thread(journal_t *journal)
{
	write_lock(&journal->j_state_lock);
	journal->j_flags |= JBD2_UNMOUNT;

	while (journal->j_task) {
		write_unlock(&journal->j_state_lock);
		wake_up(&journal->j_wait_commit);
		wait_event(journal->j_wait_done_commit, journal->j_task == NULL);
		write_lock(&journal->j_state_lock);
	}
	write_unlock(&journal->j_state_lock);
}

/*
 * jbd2_journal_write_metadata_buffer: write a metadata buffer to the journal.
 *
 * Writes a metadata buffer to a given disk block.  The actual IO is not
 * performed but a new buffer_head is constructed which labels the data
 * to be written with the correct destination disk block.
 *
 * Any magic-number escaping which needs to be done will cause a
 * copy-out here.  If the buffer happens to start with the
 * JBD2_MAGIC_NUMBER, then we can't write it to the log directly: the
 * magic number is only written to the log for descripter blocks.  In
 * this case, we copy the data and replace the first word with 0, and we
 * return a result code which indicates that this buffer needs to be
 * marked as an escaped buffer in the corresponding log descriptor
 * block.  The missing word can then be restored when the block is read
 * during recovery.
 *
 * If the source buffer has already been modified by a new transaction
 * since we took the last commit snapshot, we use the frozen copy of
 * that data for IO. If we end up using the existing buffer_head's data
 * for the write, then we have to make sure nobody modifies it while the
 * IO is in progress. do_get_write_access() handles this.
 *
 * The function returns a pointer to the buffer_head to be used for IO.
 * 
 *
 * Return value:
 *  <0: Error
 * >=0: Finished OK
 *
 * On success:
 * Bit 0 set == escape performed on the data
 * Bit 1 set == buffer copy-out performed (kfree the data after IO)
 */

int jbd2_journal_write_metadata_buffer(transaction_t *transaction,
				  struct journal_head  *jh_in,
				  struct buffer_head **bh_out,
				  sector_t blocknr)
{
	int need_copy_out = 0;
	int done_copy_out = 0;
	int do_escape = 0;
	char *mapped_data;
	struct buffer_head *new_bh;
	struct page *new_page;
	unsigned int new_offset;
	struct buffer_head *bh_in = jh2bh(jh_in);
	journal_t *journal = transaction->t_journal;

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

	/* keep subsequent assertions sane */
	atomic_set(&new_bh->b_count, 1);

	jbd_lock_bh_state(bh_in);
repeat:
	/*
	 * If a new transaction has already done a buffer copy-out, then
	 * we use that version of the data for the commit.
	 */
	if (jh_in->b_frozen_data) {
		done_copy_out = 1;
		new_page = virt_to_page(jh_in->b_frozen_data);
		new_offset = offset_in_page(jh_in->b_frozen_data);
	} else {
		new_page = jh2bh(jh_in)->b_page;
		new_offset = offset_in_page(jh2bh(jh_in)->b_data);
	}

	mapped_data = kmap_atomic(new_page);
	/*
	 * Fire data frozen trigger if data already wasn't frozen.  Do this
	 * before checking for escaping, as the trigger may modify the magic
	 * offset.  If a copy-out happens afterwards, it will have the correct
	 * data in the buffer.
	 */
	if (!done_copy_out)
		jbd2_buffer_frozen_trigger(jh_in, mapped_data + new_offset,
					   jh_in->b_triggers);

	/*
	 * Check for escaping
	 */
	if (*((__be32 *)(mapped_data + new_offset)) ==
				cpu_to_be32(JBD2_MAGIC_NUMBER)) {
		need_copy_out = 1;
		do_escape = 1;
	}
	kunmap_atomic(mapped_data);

	/*
	 * Do we need to do a data copy?
	 */
	if (need_copy_out && !done_copy_out) {
		char *tmp;

		jbd_unlock_bh_state(bh_in);
		tmp = jbd2_alloc(bh_in->b_size, GFP_NOFS);
		if (!tmp) {
			brelse(new_bh);
			return -ENOMEM;
		}
		jbd_lock_bh_state(bh_in);
		if (jh_in->b_frozen_data) {
			jbd2_free(tmp, bh_in->b_size);
			goto repeat;
		}

		jh_in->b_frozen_data = tmp;
		mapped_data = kmap_atomic(new_page);
		memcpy(tmp, mapped_data + new_offset, bh_in->b_size);
		kunmap_atomic(mapped_data);

		new_page = virt_to_page(tmp);
		new_offset = offset_in_page(tmp);
		done_copy_out = 1;

		/*
		 * This isn't strictly necessary, as we're using frozen
		 * data for the escaping, but it keeps consistency with
		 * b_frozen_data usage.
		 */
		jh_in->b_frozen_triggers = jh_in->b_triggers;
	}

	/*
	 * Did we need to do an escaping?  Now we've done all the
	 * copying, we can finally do so.
	 */
	if (do_escape) {
		mapped_data = kmap_atomic(new_page);
		*((unsigned int *)(mapped_data + new_offset)) = 0;
		kunmap_atomic(mapped_data);
	}

	set_bh_page(new_bh, new_page, new_offset);
	new_bh->b_size = bh_in->b_size;
	new_bh->b_bdev = journal->j_dev;
	new_bh->b_blocknr = blocknr;
	new_bh->b_private = bh_in;
	set_buffer_mapped(new_bh);
	set_buffer_dirty(new_bh);

	*bh_out = new_bh;

	/*
	 * The to-be-written buffer needs to get moved to the io queue,
	 * and the original buffer whose contents we are shadowing or
	 * copying is moved to the transaction's shadow queue.
	 */
	JBUFFER_TRACE(jh_in, "file as BJ_Shadow");
	spin_lock(&journal->j_list_lock);
	__jbd2_journal_file_buffer(jh_in, transaction, BJ_Shadow);
	spin_unlock(&journal->j_list_lock);
	set_buffer_shadow(bh_in);
	jbd_unlock_bh_state(bh_in);

	return do_escape | (done_copy_out << 1);
}

/*
 * Allocation code for the journal file.  Manage the space left in the
 * journal, so that we can begin checkpointing when appropriate.
 */

/*
 * Called with j_state_lock locked for writing.
 * Returns true if a transaction commit was started.
 */
int __jbd2_log_start_commit(journal_t *journal, tid_t target)
{
	/* Return if the txn has already requested to be committed */
	if (journal->j_commit_request == target)
		return 0;

	/*
	 * The only transaction we can possibly wait upon is the
	 * currently running transaction (if it exists).  Otherwise,
	 * the target tid must be an old one.
	 */
	if (journal->j_running_transaction &&
	    journal->j_running_transaction->t_tid == target) {
		/*
		 * We want a new commit: OK, mark the request and wakeup the
		 * commit thread.  We do _not_ do the commit ourselves.
		 */

		journal->j_commit_request = target;
		jbd_debug(1, "JBD2: requesting commit %d/%d\n",
			  journal->j_commit_request,
			  journal->j_commit_sequence);
		journal->j_running_transaction->t_requested = jiffies;
		wake_up(&journal->j_wait_commit);
		return 1;
	} else if (!tid_geq(journal->j_commit_request, target))
		/* This should never happen, but if it does, preserve
		   the evidence before kjournald goes into a loop and
		   increments j_commit_sequence beyond all recognition. */
		WARN_ONCE(1, "JBD2: bad log_start_commit: %u %u %u %u\n",
			  journal->j_commit_request,
			  journal->j_commit_sequence,
			  target, journal->j_running_transaction ? 
			  journal->j_running_transaction->t_tid : 0);
	return 0;
}

int jbd2_log_start_commit(journal_t *journal, tid_t tid)
{
	int ret;

	write_lock(&journal->j_state_lock);
	ret = __jbd2_log_start_commit(journal, tid);
	write_unlock(&journal->j_state_lock);
	return ret;
}

/*
 * Force and wait any uncommitted transactions.  We can only force the running
 * transaction if we don't have an active handle, otherwise, we will deadlock.
 * Returns: <0 in case of error,
 *           0 if nothing to commit,
 *           1 if transaction was successfully committed.
 */
static int __jbd2_journal_force_commit(journal_t *journal)
{
	transaction_t *transaction = NULL;
	tid_t tid;
	int need_to_start = 0, ret = 0;

	read_lock(&journal->j_state_lock);
	if (journal->j_running_transaction && !current->journal_info) {
		transaction = journal->j_running_transaction;
		if (!tid_geq(journal->j_commit_request, transaction->t_tid))
			need_to_start = 1;
	} else if (journal->j_committing_transaction)
		transaction = journal->j_committing_transaction;

	if (!transaction) {
		/* Nothing to commit */
		read_unlock(&journal->j_state_lock);
		return 0;
	}
	tid = transaction->t_tid;
	read_unlock(&journal->j_state_lock);
	if (need_to_start)
		jbd2_log_start_commit(journal, tid);
	ret = jbd2_log_wait_commit(journal, tid);
	if (!ret)
		ret = 1;

	return ret;
}

/**
 * Force and wait upon a commit if the calling process is not within
 * transaction.  This is used for forcing out undo-protected data which contains
 * bitmaps, when the fs is running out of space.
 *
 * @journal: journal to force
 * Returns true if progress was made.
 */
int jbd2_journal_force_commit_nested(journal_t *journal)
{
	int ret;

	ret = __jbd2_journal_force_commit(journal);
	return ret > 0;
}

/**
 * int journal_force_commit() - force any uncommitted transactions
 * @journal: journal to force
 *
 * Caller want unconditional commit. We can only force the running transaction
 * if we don't have an active handle, otherwise, we will deadlock.
 */
int jbd2_journal_force_commit(journal_t *journal)
{
	int ret;

	J_ASSERT(!current->journal_info);
	ret = __jbd2_journal_force_commit(journal);
	if (ret > 0)
		ret = 0;
	return ret;
}

/*
 * Start a commit of the current running transaction (if any).  Returns true
 * if a transaction is going to be committed (or is currently already
 * committing), and fills its tid in at *ptid
 */
int jbd2_journal_start_commit(journal_t *journal, tid_t *ptid)
{
	int ret = 0;

	write_lock(&journal->j_state_lock);
	if (journal->j_running_transaction) {
		tid_t tid = journal->j_running_transaction->t_tid;

		__jbd2_log_start_commit(journal, tid);
		/* There's a running transaction and we've just made sure
		 * it's commit has been scheduled. */
		if (ptid)
			*ptid = tid;
		ret = 1;
	} else if (journal->j_committing_transaction) {
		/*
		 * If commit has been started, then we have to wait for
		 * completion of that transaction.
		 */
		if (ptid)
			*ptid = journal->j_committing_transaction->t_tid;
		ret = 1;
	}
	write_unlock(&journal->j_state_lock);
	return ret;
}

/*
 * Return 1 if a given transaction has not yet sent barrier request
 * connected with a transaction commit. If 0 is returned, transaction
 * may or may not have sent the barrier. Used to avoid sending barrier
 * twice in common cases.
 */
int jbd2_trans_will_send_data_barrier(journal_t *journal, tid_t tid)
{
	int ret = 0;
	transaction_t *commit_trans;

	if (!(journal->j_flags & JBD2_BARRIER))
		return 0;
	read_lock(&journal->j_state_lock);
	/* Transaction already committed? */
	if (tid_geq(journal->j_commit_sequence, tid))
		goto out;
	commit_trans = journal->j_committing_transaction;
	if (!commit_trans || commit_trans->t_tid != tid) {
		ret = 1;
		goto out;
	}
	/*
	 * Transaction is being committed and we already proceeded to
	 * submitting a flush to fs partition?
	 */
	if (journal->j_fs_dev != journal->j_dev) {
		if (!commit_trans->t_need_data_flush ||
		    commit_trans->t_state >= T_COMMIT_DFLUSH)
			goto out;
	} else {
		if (commit_trans->t_state >= T_COMMIT_JFLUSH)
			goto out;
	}
	ret = 1;
out:
	read_unlock(&journal->j_state_lock);
	return ret;
}
EXPORT_SYMBOL(jbd2_trans_will_send_data_barrier);

/*
 * Wait for a specified commit to complete.
 * The caller may not hold the journal lock.
 */
int jbd2_log_wait_commit(journal_t *journal, tid_t tid)
{
	int err = 0;

	jbd2_might_wait_for_commit(journal);
	read_lock(&journal->j_state_lock);
#ifdef CONFIG_JBD2_DEBUG
	if (!tid_geq(journal->j_commit_request, tid)) {
		printk(KERN_ERR
		       "%s: error: j_commit_request=%d, tid=%d\n",
		       __func__, journal->j_commit_request, tid);
	}
#endif
	while (tid_gt(tid, journal->j_commit_sequence)) {
		jbd_debug(1, "JBD2: want %d, j_commit_sequence=%d\n",
				  tid, journal->j_commit_sequence);
		read_unlock(&journal->j_state_lock);
		wake_up(&journal->j_wait_commit);
		wait_event(journal->j_wait_done_commit,
				!tid_gt(tid, journal->j_commit_sequence));
		read_lock(&journal->j_state_lock);
	}
	read_unlock(&journal->j_state_lock);

	if (unlikely(is_journal_aborted(journal)))
		err = -EIO;
	return err;
}

/*
 * When this function returns the transaction corresponding to tid
 * will be completed.  If the transaction has currently running, start
 * committing that transaction before waiting for it to complete.  If
 * the transaction id is stale, it is by definition already completed,
 * so just return SUCCESS.
 */
int jbd2_complete_transaction(journal_t *journal, tid_t tid)
{
	int	need_to_wait = 1;

	read_lock(&journal->j_state_lock);
	if (journal->j_running_transaction &&
	    journal->j_running_transaction->t_tid == tid) {
		if (journal->j_commit_request != tid) {
			/* transaction not yet started, so request it */
			read_unlock(&journal->j_state_lock);
			jbd2_log_start_commit(journal, tid);
			goto wait_commit;
		}
	} else if (!(journal->j_committing_transaction &&
		     journal->j_committing_transaction->t_tid == tid))
		need_to_wait = 0;
	read_unlock(&journal->j_state_lock);
	if (!need_to_wait)
		return 0;
wait_commit:
	return jbd2_log_wait_commit(journal, tid);
}
EXPORT_SYMBOL(jbd2_complete_transaction);

/*
 * Log buffer allocation routines:
 */

int jbd2_journal_next_log_block(journal_t *journal, unsigned long long *retp)
{
	unsigned long blocknr;

	write_lock(&journal->j_state_lock);
	J_ASSERT(journal->j_free > 1);

	blocknr = journal->j_head;
	journal->j_head++;
	journal->j_free--;
	if (journal->j_head == journal->j_last)
		journal->j_head = journal->j_first;
	write_unlock(&journal->j_state_lock);
	return jbd2_journal_bmap(journal, blocknr, retp);
}

/*
 * Conversion of logical to physical block numbers for the journal
 *
 * On external journals the journal blocks are identity-mapped, so
 * this is a no-op.  If needed, we can use j_blk_offset - everything is
 * ready.
 */
int jbd2_journal_bmap(journal_t *journal, unsigned long blocknr,
		 unsigned long long *retp)
{
	int err = 0;
	unsigned long long ret;

	if (journal->j_inode) {
		ret = bmap(journal->j_inode, blocknr);
		if (ret)
			*retp = ret;
		else {
			printk(KERN_ALERT "%s: journal block not found "
					"at offset %lu on %s\n",
			       __func__, blocknr, journal->j_devname);
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
 * After the caller of jbd2_journal_get_descriptor_buffer() has finished modifying
 * the buffer's contents they really should run flush_dcache_page(bh->b_page).
 * But we don't bother doing that, so there will be coherency problems with
 * mmaps of blockdevs which hold live JBD-controlled filesystems.
 */
struct buffer_head *
jbd2_journal_get_descriptor_buffer(transaction_t *transaction, int type)
{
	journal_t *journal = transaction->t_journal;
	struct buffer_head *bh;
	unsigned long long blocknr;
	journal_header_t *header;
	int err;

	err = jbd2_journal_next_log_block(journal, &blocknr);

	if (err)
		return NULL;

	bh = __getblk(journal->j_dev, blocknr, journal->j_blocksize);
	if (!bh)
		return NULL;
	lock_buffer(bh);
	memset(bh->b_data, 0, journal->j_blocksize);
	header = (journal_header_t *)bh->b_data;
	header->h_magic = cpu_to_be32(JBD2_MAGIC_NUMBER);
	header->h_blocktype = cpu_to_be32(type);
	header->h_sequence = cpu_to_be32(transaction->t_tid);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);
	BUFFER_TRACE(bh, "return this buffer");
	return bh;
}

void jbd2_descriptor_block_csum_set(journal_t *j, struct buffer_head *bh)
{
	struct jbd2_journal_block_tail *tail;
	__u32 csum;

	if (!jbd2_journal_has_csum_v2or3(j))
		return;

	tail = (struct jbd2_journal_block_tail *)(bh->b_data + j->j_blocksize -
			sizeof(struct jbd2_journal_block_tail));
	tail->t_checksum = 0;
	csum = jbd2_chksum(j, j->j_csum_seed, bh->b_data, j->j_blocksize);
	tail->t_checksum = cpu_to_be32(csum);
}

/*
 * Return tid of the oldest transaction in the journal and block in the journal
 * where the transaction starts.
 *
 * If the journal is now empty, return which will be the next transaction ID
 * we will write and where will that transaction start.
 *
 * The return value is 0 if journal tail cannot be pushed any further, 1 if
 * it can.
 */
int jbd2_journal_get_log_tail(journal_t *journal, tid_t *tid,
			      unsigned long *block)
{
	transaction_t *transaction;
	int ret;

	read_lock(&journal->j_state_lock);
	spin_lock(&journal->j_list_lock);
	transaction = journal->j_checkpoint_transactions;
	if (transaction) {
		*tid = transaction->t_tid;
		*block = transaction->t_log_start;
	} else if ((transaction = journal->j_committing_transaction) != NULL) {
		*tid = transaction->t_tid;
		*block = transaction->t_log_start;
	} else if ((transaction = journal->j_running_transaction) != NULL) {
		*tid = transaction->t_tid;
		*block = journal->j_head;
	} else {
		*tid = journal->j_transaction_sequence;
		*block = journal->j_head;
	}
	ret = tid_gt(*tid, journal->j_tail_sequence);
	spin_unlock(&journal->j_list_lock);
	read_unlock(&journal->j_state_lock);

	return ret;
}

/*
 * Update information in journal structure and in on disk journal superblock
 * about log tail. This function does not check whether information passed in
 * really pushes log tail further. It's responsibility of the caller to make
 * sure provided log tail information is valid (e.g. by holding
 * j_checkpoint_mutex all the time between computing log tail and calling this
 * function as is the case with jbd2_cleanup_journal_tail()).
 *
 * Requires j_checkpoint_mutex
 */
int __jbd2_update_log_tail(journal_t *journal, tid_t tid, unsigned long block)
{
	unsigned long freed;
	int ret;

	BUG_ON(!mutex_is_locked(&journal->j_checkpoint_mutex));

	/*
	 * We cannot afford for write to remain in drive's caches since as
	 * soon as we update j_tail, next transaction can start reusing journal
	 * space and if we lose sb update during power failure we'd replay
	 * old transaction with possibly newly overwritten data.
	 */
	ret = jbd2_journal_update_sb_log_tail(journal, tid, block, REQ_FUA);
	if (ret)
		goto out;

	write_lock(&journal->j_state_lock);
	freed = block - journal->j_tail;
	if (block < journal->j_tail)
		freed += journal->j_last - journal->j_first;

	trace_jbd2_update_log_tail(journal, tid, block, freed);
	jbd_debug(1,
		  "Cleaning journal tail from %d to %d (offset %lu), "
		  "freeing %lu\n",
		  journal->j_tail_sequence, tid, block, freed);

	journal->j_free += freed;
	journal->j_tail_sequence = tid;
	journal->j_tail = block;
	write_unlock(&journal->j_state_lock);

out:
	return ret;
}

/*
 * This is a variaon of __jbd2_update_log_tail which checks for validity of
 * provided log tail and locks j_checkpoint_mutex. So it is safe against races
 * with other threads updating log tail.
 */
void jbd2_update_log_tail(journal_t *journal, tid_t tid, unsigned long block)
{
	mutex_lock_io(&journal->j_checkpoint_mutex);
	if (tid_gt(tid, journal->j_tail_sequence))
		__jbd2_update_log_tail(journal, tid, block);
	mutex_unlock(&journal->j_checkpoint_mutex);
}

struct jbd2_stats_proc_session {
	journal_t *journal;
	struct transaction_stats_s *stats;
	int start;
	int max;
};

static void *jbd2_seq_info_start(struct seq_file *seq, loff_t *pos)
{
	return *pos ? NULL : SEQ_START_TOKEN;
}

static void *jbd2_seq_info_next(struct seq_file *seq, void *v, loff_t *pos)
{
	return NULL;
}

static int jbd2_seq_info_show(struct seq_file *seq, void *v)
{
	struct jbd2_stats_proc_session *s = seq->private;

	if (v != SEQ_START_TOKEN)
		return 0;
	seq_printf(seq, "%lu transactions (%lu requested), "
		   "each up to %u blocks\n",
		   s->stats->ts_tid, s->stats->ts_requested,
		   s->journal->j_max_transaction_buffers);
	if (s->stats->ts_tid == 0)
		return 0;
	seq_printf(seq, "average: \n  %ums waiting for transaction\n",
	    jiffies_to_msecs(s->stats->run.rs_wait / s->stats->ts_tid));
	seq_printf(seq, "  %ums request delay\n",
	    (s->stats->ts_requested == 0) ? 0 :
	    jiffies_to_msecs(s->stats->run.rs_request_delay /
			     s->stats->ts_requested));
	seq_printf(seq, "  %ums running transaction\n",
	    jiffies_to_msecs(s->stats->run.rs_running / s->stats->ts_tid));
	seq_printf(seq, "  %ums transaction was being locked\n",
	    jiffies_to_msecs(s->stats->run.rs_locked / s->stats->ts_tid));
	seq_printf(seq, "  %ums flushing data (in ordered mode)\n",
	    jiffies_to_msecs(s->stats->run.rs_flushing / s->stats->ts_tid));
	seq_printf(seq, "  %ums logging transaction\n",
	    jiffies_to_msecs(s->stats->run.rs_logging / s->stats->ts_tid));
	seq_printf(seq, "  %lluus average transaction commit time\n",
		   div_u64(s->journal->j_average_commit_time, 1000));
	seq_printf(seq, "  %lu handles per transaction\n",
	    s->stats->run.rs_handle_count / s->stats->ts_tid);
	seq_printf(seq, "  %lu blocks per transaction\n",
	    s->stats->run.rs_blocks / s->stats->ts_tid);
	seq_printf(seq, "  %lu logged blocks per transaction\n",
	    s->stats->run.rs_blocks_logged / s->stats->ts_tid);
	return 0;
}

static void jbd2_seq_info_stop(struct seq_file *seq, void *v)
{
}

static const struct seq_operations jbd2_seq_info_ops = {
	.start  = jbd2_seq_info_start,
	.next   = jbd2_seq_info_next,
	.stop   = jbd2_seq_info_stop,
	.show   = jbd2_seq_info_show,
};

static int jbd2_seq_info_open(struct inode *inode, struct file *file)
{
	journal_t *journal = PDE_DATA(inode);
	struct jbd2_stats_proc_session *s;
	int rc, size;

	s = kmalloc(sizeof(*s), GFP_KERNEL);
	if (s == NULL)
		return -ENOMEM;
	size = sizeof(struct transaction_stats_s);
	s->stats = kmalloc(size, GFP_KERNEL);
	if (s->stats == NULL) {
		kfree(s);
		return -ENOMEM;
	}
	spin_lock(&journal->j_history_lock);
	memcpy(s->stats, &journal->j_stats, size);
	s->journal = journal;
	spin_unlock(&journal->j_history_lock);

	rc = seq_open(file, &jbd2_seq_info_ops);
	if (rc == 0) {
		struct seq_file *m = file->private_data;
		m->private = s;
	} else {
		kfree(s->stats);
		kfree(s);
	}
	return rc;

}

static int jbd2_seq_info_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct jbd2_stats_proc_session *s = seq->private;
	kfree(s->stats);
	kfree(s);
	return seq_release(inode, file);
}

static const struct file_operations jbd2_seq_info_fops = {
	.owner		= THIS_MODULE,
	.open           = jbd2_seq_info_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = jbd2_seq_info_release,
};

static struct proc_dir_entry *proc_jbd2_stats;

static void jbd2_stats_proc_init(journal_t *journal)
{
	journal->j_proc_entry = proc_mkdir(journal->j_devname, proc_jbd2_stats);
	if (journal->j_proc_entry) {
		proc_create_data("info", S_IRUGO, journal->j_proc_entry,
				 &jbd2_seq_info_fops, journal);
	}
}

static void jbd2_stats_proc_exit(journal_t *journal)
{
	remove_proc_entry("info", journal->j_proc_entry);
	remove_proc_entry(journal->j_devname, proc_jbd2_stats);
}

/*
 * Management for journal control blocks: functions to create and
 * destroy journal_t structures, and to initialise and read existing
 * journal blocks from disk.  */

/* First: create and setup a journal_t object in memory.  We initialise
 * very few fields yet: that has to wait until we have created the
 * journal structures from from scratch, or loaded them from disk. */

static journal_t *journal_init_common(struct block_device *bdev,
			struct block_device *fs_dev,
			unsigned long long start, int len, int blocksize)
{
	static struct lock_class_key jbd2_trans_commit_key;
	journal_t *journal;
	int err;
	struct buffer_head *bh;
	int n;

	journal = kzalloc(sizeof(*journal), GFP_KERNEL);
	if (!journal)
		return NULL;

	init_waitqueue_head(&journal->j_wait_transaction_locked);
	init_waitqueue_head(&journal->j_wait_done_commit);
	init_waitqueue_head(&journal->j_wait_commit);
	init_waitqueue_head(&journal->j_wait_updates);
	init_waitqueue_head(&journal->j_wait_reserved);
	mutex_init(&journal->j_barrier);
	mutex_init(&journal->j_checkpoint_mutex);
	spin_lock_init(&journal->j_revoke_lock);
	spin_lock_init(&journal->j_list_lock);
	rwlock_init(&journal->j_state_lock);

	journal->j_commit_interval = (HZ * JBD2_DEFAULT_MAX_COMMIT_AGE);
	journal->j_min_batch_time = 0;
	journal->j_max_batch_time = 15000; /* 15ms */
	atomic_set(&journal->j_reserved_credits, 0);

	/* The journal is marked for error until we succeed with recovery! */
	journal->j_flags = JBD2_ABORT;

	/* Set up a default-sized revoke table for the new mount. */
	err = jbd2_journal_init_revoke(journal, JOURNAL_REVOKE_DEFAULT_HASH);
	if (err) {
		kfree(journal);
		return NULL;
	}

	spin_lock_init(&journal->j_history_lock);

	lockdep_init_map(&journal->j_trans_commit_map, "jbd2_handle",
			 &jbd2_trans_commit_key, 0);

	/* journal descriptor can store up to n blocks -bzzz */
	journal->j_blocksize = blocksize;
	journal->j_dev = bdev;
	journal->j_fs_dev = fs_dev;
	journal->j_blk_offset = start;
	journal->j_maxlen = len;
	n = journal->j_blocksize / sizeof(journal_block_tag_t);
	journal->j_wbufsize = n;
	journal->j_wbuf = kmalloc_array(n, sizeof(struct buffer_head *),
					GFP_KERNEL);
	if (!journal->j_wbuf) {
		kfree(journal);
		return NULL;
	}

	bh = getblk_unmovable(journal->j_dev, start, journal->j_blocksize);
	if (!bh) {
		pr_err("%s: Cannot get buffer for journal superblock\n",
			__func__);
		kfree(journal->j_wbuf);
		kfree(journal);
		return NULL;
	}
	journal->j_sb_buffer = bh;
	journal->j_superblock = (journal_superblock_t *)bh->b_data;

	return journal;
}

/* jbd2_journal_init_dev and jbd2_journal_init_inode:
 *
 * Create a journal structure assigned some fixed set of disk blocks to
 * the journal.  We don't actually touch those disk blocks yet, but we
 * need to set up all of the mapping information to tell the journaling
 * system where the journal blocks are.
 *
 */

/**
 *  journal_t * jbd2_journal_init_dev() - creates and initialises a journal structure
 *  @bdev: Block device on which to create the journal
 *  @fs_dev: Device which hold journalled filesystem for this journal.
 *  @start: Block nr Start of journal.
 *  @len:  Length of the journal in blocks.
 *  @blocksize: blocksize of journalling device
 *
 *  Returns: a newly created journal_t *
 *
 *  jbd2_journal_init_dev creates a journal which maps a fixed contiguous
 *  range of blocks on an arbitrary block device.
 *
 */
journal_t *jbd2_journal_init_dev(struct block_device *bdev,
			struct block_device *fs_dev,
			unsigned long long start, int len, int blocksize)
{
	journal_t *journal;

	journal = journal_init_common(bdev, fs_dev, start, len, blocksize);
	if (!journal)
		return NULL;

	bdevname(journal->j_dev, journal->j_devname);
	strreplace(journal->j_devname, '/', '!');
	jbd2_stats_proc_init(journal);

	return journal;
}

/**
 *  journal_t * jbd2_journal_init_inode () - creates a journal which maps to a inode.
 *  @inode: An inode to create the journal in
 *
 * jbd2_journal_init_inode creates a journal which maps an on-disk inode as
 * the journal.  The inode must exist already, must support bmap() and
 * must have all data blocks preallocated.
 */
journal_t *jbd2_journal_init_inode(struct inode *inode)
{
	journal_t *journal;
	char *p;
	unsigned long long blocknr;

	blocknr = bmap(inode, 0);
	if (!blocknr) {
		pr_err("%s: Cannot locate journal superblock\n",
			__func__);
		return NULL;
	}

	jbd_debug(1, "JBD2: inode %s/%ld, size %lld, bits %d, blksize %ld\n",
		  inode->i_sb->s_id, inode->i_ino, (long long) inode->i_size,
		  inode->i_sb->s_blocksize_bits, inode->i_sb->s_blocksize);

	journal = journal_init_common(inode->i_sb->s_bdev, inode->i_sb->s_bdev,
			blocknr, inode->i_size >> inode->i_sb->s_blocksize_bits,
			inode->i_sb->s_blocksize);
	if (!journal)
		return NULL;

	journal->j_inode = inode;
	bdevname(journal->j_dev, journal->j_devname);
	p = strreplace(journal->j_devname, '/', '!');
	sprintf(p, "-%lu", journal->j_inode->i_ino);
	jbd2_stats_proc_init(journal);

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
	unsigned long long first, last;

	first = be32_to_cpu(sb->s_first);
	last = be32_to_cpu(sb->s_maxlen);
	if (first + JBD2_MIN_JOURNAL_BLOCKS > last + 1) {
		printk(KERN_ERR "JBD2: Journal too short (blocks %llu-%llu).\n",
		       first, last);
		journal_fail_superblock(journal);
		return -EINVAL;
	}

	journal->j_first = first;
	journal->j_last = last;

	journal->j_head = first;
	journal->j_tail = first;
	journal->j_free = last - first;

	journal->j_tail_sequence = journal->j_transaction_sequence;
	journal->j_commit_sequence = journal->j_transaction_sequence - 1;
	journal->j_commit_request = journal->j_commit_sequence;

	journal->j_max_transaction_buffers = journal->j_maxlen / 4;

	/*
	 * As a special case, if the on-disk copy is already marked as needing
	 * no recovery (s_start == 0), then we can safely defer the superblock
	 * update until the next commit by setting JBD2_FLUSHED.  This avoids
	 * attempting a write to a potential-readonly device.
	 */
	if (sb->s_start == 0) {
		jbd_debug(1, "JBD2: Skipping superblock update on recovered sb "
			"(start %ld, seq %d, errno %d)\n",
			journal->j_tail, journal->j_tail_sequence,
			journal->j_errno);
		journal->j_flags |= JBD2_FLUSHED;
	} else {
		/* Lock here to make assertions happy... */
		mutex_lock_io(&journal->j_checkpoint_mutex);
		/*
		 * Update log tail information. We use REQ_FUA since new
		 * transaction will start reusing journal space and so we
		 * must make sure information about current log tail is on
		 * disk before that.
		 */
		jbd2_journal_update_sb_log_tail(journal,
						journal->j_tail_sequence,
						journal->j_tail,
						REQ_FUA);
		mutex_unlock(&journal->j_checkpoint_mutex);
	}
	return jbd2_journal_start_thread(journal);
}

static int jbd2_write_superblock(journal_t *journal, int write_flags)
{
	struct buffer_head *bh = journal->j_sb_buffer;
	journal_superblock_t *sb = journal->j_superblock;
	int ret;

	trace_jbd2_write_superblock(journal, write_flags);
	if (!(journal->j_flags & JBD2_BARRIER))
		write_flags &= ~(REQ_FUA | REQ_PREFLUSH);
	lock_buffer(bh);
	if (buffer_write_io_error(bh)) {
		/*
		 * Oh, dear.  A previous attempt to write the journal
		 * superblock failed.  This could happen because the
		 * USB device was yanked out.  Or it could happen to
		 * be a transient write error and maybe the block will
		 * be remapped.  Nothing we can do but to retry the
		 * write and hope for the best.
		 */
		printk(KERN_ERR "JBD2: previous I/O error detected "
		       "for journal superblock update for %s.\n",
		       journal->j_devname);
		clear_buffer_write_io_error(bh);
		set_buffer_uptodate(bh);
	}
	jbd2_superblock_csum_set(journal, sb);
	get_bh(bh);
	bh->b_end_io = end_buffer_write_sync;
	ret = submit_bh(REQ_OP_WRITE, write_flags, bh);
	wait_on_buffer(bh);
	if (buffer_write_io_error(bh)) {
		clear_buffer_write_io_error(bh);
		set_buffer_uptodate(bh);
		ret = -EIO;
	}
	if (ret) {
		printk(KERN_ERR "JBD2: Error %d detected when updating "
		       "journal superblock for %s.\n", ret,
		       journal->j_devname);
		jbd2_journal_abort(journal, ret);
	}

	return ret;
}

/**
 * jbd2_journal_update_sb_log_tail() - Update log tail in journal sb on disk.
 * @journal: The journal to update.
 * @tail_tid: TID of the new transaction at the tail of the log
 * @tail_block: The first block of the transaction at the tail of the log
 * @write_op: With which operation should we write the journal sb
 *
 * Update a journal's superblock information about log tail and write it to
 * disk, waiting for the IO to complete.
 */
int jbd2_journal_update_sb_log_tail(journal_t *journal, tid_t tail_tid,
				     unsigned long tail_block, int write_op)
{
	journal_superblock_t *sb = journal->j_superblock;
	int ret;

	BUG_ON(!mutex_is_locked(&journal->j_checkpoint_mutex));
	jbd_debug(1, "JBD2: updating superblock (start %lu, seq %u)\n",
		  tail_block, tail_tid);

	sb->s_sequence = cpu_to_be32(tail_tid);
	sb->s_start    = cpu_to_be32(tail_block);

	ret = jbd2_write_superblock(journal, write_op);
	if (ret)
		goto out;

	/* Log is no longer empty */
	write_lock(&journal->j_state_lock);
	WARN_ON(!sb->s_sequence);
	journal->j_flags &= ~JBD2_FLUSHED;
	write_unlock(&journal->j_state_lock);

out:
	return ret;
}

/**
 * jbd2_mark_journal_empty() - Mark on disk journal as empty.
 * @journal: The journal to update.
 * @write_op: With which operation should we write the journal sb
 *
 * Update a journal's dynamic superblock fields to show that journal is empty.
 * Write updated superblock to disk waiting for IO to complete.
 */
static void jbd2_mark_journal_empty(journal_t *journal, int write_op)
{
	journal_superblock_t *sb = journal->j_superblock;

	BUG_ON(!mutex_is_locked(&journal->j_checkpoint_mutex));
	read_lock(&journal->j_state_lock);
	/* Is it already empty? */
	if (sb->s_start == 0) {
		read_unlock(&journal->j_state_lock);
		return;
	}
	jbd_debug(1, "JBD2: Marking journal as empty (seq %d)\n",
		  journal->j_tail_sequence);

	sb->s_sequence = cpu_to_be32(journal->j_tail_sequence);
	sb->s_start    = cpu_to_be32(0);
	read_unlock(&journal->j_state_lock);

	jbd2_write_superblock(journal, write_op);

	/* Log is no longer empty */
	write_lock(&journal->j_state_lock);
	journal->j_flags |= JBD2_FLUSHED;
	write_unlock(&journal->j_state_lock);
}


/**
 * jbd2_journal_update_sb_errno() - Update error in the journal.
 * @journal: The journal to update.
 *
 * Update a journal's errno.  Write updated superblock to disk waiting for IO
 * to complete.
 */
void jbd2_journal_update_sb_errno(journal_t *journal)
{
	journal_superblock_t *sb = journal->j_superblock;

	read_lock(&journal->j_state_lock);
	jbd_debug(1, "JBD2: updating superblock error (errno %d)\n",
		  journal->j_errno);
	sb->s_errno    = cpu_to_be32(journal->j_errno);
	read_unlock(&journal->j_state_lock);

	jbd2_write_superblock(journal, REQ_FUA);
}
EXPORT_SYMBOL(jbd2_journal_update_sb_errno);

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
		ll_rw_block(REQ_OP_READ, 0, 1, &bh);
		wait_on_buffer(bh);
		if (!buffer_uptodate(bh)) {
			printk(KERN_ERR
				"JBD2: IO error reading journal superblock\n");
			goto out;
		}
	}

	if (buffer_verified(bh))
		return 0;

	sb = journal->j_superblock;

	err = -EINVAL;

	if (sb->s_header.h_magic != cpu_to_be32(JBD2_MAGIC_NUMBER) ||
	    sb->s_blocksize != cpu_to_be32(journal->j_blocksize)) {
		printk(KERN_WARNING "JBD2: no valid journal superblock found\n");
		goto out;
	}

	switch(be32_to_cpu(sb->s_header.h_blocktype)) {
	case JBD2_SUPERBLOCK_V1:
		journal->j_format_version = 1;
		break;
	case JBD2_SUPERBLOCK_V2:
		journal->j_format_version = 2;
		break;
	default:
		printk(KERN_WARNING "JBD2: unrecognised superblock format ID\n");
		goto out;
	}

	if (be32_to_cpu(sb->s_maxlen) < journal->j_maxlen)
		journal->j_maxlen = be32_to_cpu(sb->s_maxlen);
	else if (be32_to_cpu(sb->s_maxlen) > journal->j_maxlen) {
		printk(KERN_WARNING "JBD2: journal file too short\n");
		goto out;
	}

	if (be32_to_cpu(sb->s_first) == 0 ||
	    be32_to_cpu(sb->s_first) >= journal->j_maxlen) {
		printk(KERN_WARNING
			"JBD2: Invalid start block of journal: %u\n",
			be32_to_cpu(sb->s_first));
		goto out;
	}

	if (jbd2_has_feature_csum2(journal) &&
	    jbd2_has_feature_csum3(journal)) {
		/* Can't have checksum v2 and v3 at the same time! */
		printk(KERN_ERR "JBD2: Can't enable checksumming v2 and v3 "
		       "at the same time!\n");
		goto out;
	}

	if (jbd2_journal_has_csum_v2or3_feature(journal) &&
	    jbd2_has_feature_checksum(journal)) {
		/* Can't have checksum v1 and v2 on at the same time! */
		printk(KERN_ERR "JBD2: Can't enable checksumming v1 and v2/3 "
		       "at the same time!\n");
		goto out;
	}

	if (!jbd2_verify_csum_type(journal, sb)) {
		printk(KERN_ERR "JBD2: Unknown checksum type\n");
		goto out;
	}

	/* Load the checksum driver */
	if (jbd2_journal_has_csum_v2or3_feature(journal)) {
		journal->j_chksum_driver = crypto_alloc_shash("crc32c", 0, 0);
		if (IS_ERR(journal->j_chksum_driver)) {
			printk(KERN_ERR "JBD2: Cannot load crc32c driver.\n");
			err = PTR_ERR(journal->j_chksum_driver);
			journal->j_chksum_driver = NULL;
			goto out;
		}
	}

	/* Check superblock checksum */
	if (!jbd2_superblock_csum_verify(journal, sb)) {
		printk(KERN_ERR "JBD2: journal checksum error\n");
		err = -EFSBADCRC;
		goto out;
	}

	/* Precompute checksum seed for all metadata */
	if (jbd2_journal_has_csum_v2or3(journal))
		journal->j_csum_seed = jbd2_chksum(journal, ~0, sb->s_uuid,
						   sizeof(sb->s_uuid));

	set_buffer_verified(bh);

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
 * int jbd2_journal_load() - Read journal from disk.
 * @journal: Journal to act on.
 *
 * Given a journal_t structure which tells us which disk blocks contain
 * a journal, read the journal from disk to initialise the in-memory
 * structures.
 */
int jbd2_journal_load(journal_t *journal)
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
		     ~cpu_to_be32(JBD2_KNOWN_ROCOMPAT_FEATURES)) ||
		    (sb->s_feature_incompat &
		     ~cpu_to_be32(JBD2_KNOWN_INCOMPAT_FEATURES))) {
			printk(KERN_WARNING
				"JBD2: Unrecognised features on journal\n");
			return -EINVAL;
		}
	}

	/*
	 * Create a slab for this blocksize
	 */
	err = jbd2_journal_create_slab(be32_to_cpu(sb->s_blocksize));
	if (err)
		return err;

	/* Let the recovery code check whether it needs to recover any
	 * data from the journal. */
	if (jbd2_journal_recover(journal))
		goto recovery_error;

	if (journal->j_failed_commit) {
		printk(KERN_ERR "JBD2: journal transaction %u on %s "
		       "is corrupt.\n", journal->j_failed_commit,
		       journal->j_devname);
		return -EFSCORRUPTED;
	}

	/* OK, we've finished with the dynamic journal bits:
	 * reinitialise the dynamic contents of the superblock in memory
	 * and reset them on disk. */
	if (journal_reset(journal))
		goto recovery_error;

	journal->j_flags &= ~JBD2_ABORT;
	journal->j_flags |= JBD2_LOADED;
	return 0;

recovery_error:
	printk(KERN_WARNING "JBD2: recovery failed\n");
	return -EIO;
}

/**
 * void jbd2_journal_destroy() - Release a journal_t structure.
 * @journal: Journal to act on.
 *
 * Release a journal_t structure once it is no longer in use by the
 * journaled object.
 * Return <0 if we couldn't clean up the journal.
 */
int jbd2_journal_destroy(journal_t *journal)
{
	int err = 0;

	/* Wait for the commit thread to wake up and die. */
	journal_kill_thread(journal);

	/* Force a final log commit */
	if (journal->j_running_transaction)
		jbd2_journal_commit_transaction(journal);

	/* Force any old transactions to disk */

	/* Totally anal locking here... */
	spin_lock(&journal->j_list_lock);
	while (journal->j_checkpoint_transactions != NULL) {
		spin_unlock(&journal->j_list_lock);
		mutex_lock_io(&journal->j_checkpoint_mutex);
		err = jbd2_log_do_checkpoint(journal);
		mutex_unlock(&journal->j_checkpoint_mutex);
		/*
		 * If checkpointing failed, just free the buffers to avoid
		 * looping forever
		 */
		if (err) {
			jbd2_journal_destroy_checkpoint(journal);
			spin_lock(&journal->j_list_lock);
			break;
		}
		spin_lock(&journal->j_list_lock);
	}

	J_ASSERT(journal->j_running_transaction == NULL);
	J_ASSERT(journal->j_committing_transaction == NULL);
	J_ASSERT(journal->j_checkpoint_transactions == NULL);
	spin_unlock(&journal->j_list_lock);

	if (journal->j_sb_buffer) {
		if (!is_journal_aborted(journal)) {
			mutex_lock_io(&journal->j_checkpoint_mutex);

			write_lock(&journal->j_state_lock);
			journal->j_tail_sequence =
				++journal->j_transaction_sequence;
			write_unlock(&journal->j_state_lock);

			jbd2_mark_journal_empty(journal,
					REQ_PREFLUSH | REQ_FUA);
			mutex_unlock(&journal->j_checkpoint_mutex);
		} else
			err = -EIO;
		brelse(journal->j_sb_buffer);
	}

	if (journal->j_proc_entry)
		jbd2_stats_proc_exit(journal);
	iput(journal->j_inode);
	if (journal->j_revoke)
		jbd2_journal_destroy_revoke(journal);
	if (journal->j_chksum_driver)
		crypto_free_shash(journal->j_chksum_driver);
	kfree(journal->j_wbuf);
	kfree(journal);

	return err;
}


/**
 *int jbd2_journal_check_used_features () - Check if features specified are used.
 * @journal: Journal to check.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Check whether the journal uses all of a given set of
 * features.  Return true (non-zero) if it does.
 **/

int jbd2_journal_check_used_features (journal_t *journal, unsigned long compat,
				 unsigned long ro, unsigned long incompat)
{
	journal_superblock_t *sb;

	if (!compat && !ro && !incompat)
		return 1;
	/* Load journal superblock if it is not loaded yet. */
	if (journal->j_format_version == 0 &&
	    journal_get_superblock(journal) != 0)
		return 0;
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
 * int jbd2_journal_check_available_features() - Check feature set in journalling layer
 * @journal: Journal to check.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Check whether the journaling code supports the use of
 * all of a given set of features on this journal.  Return true
 * (non-zero) if it can. */

int jbd2_journal_check_available_features (journal_t *journal, unsigned long compat,
				      unsigned long ro, unsigned long incompat)
{
	if (!compat && !ro && !incompat)
		return 1;

	/* We can support any known requested features iff the
	 * superblock is in version 2.  Otherwise we fail to support any
	 * extended sb features. */

	if (journal->j_format_version != 2)
		return 0;

	if ((compat   & JBD2_KNOWN_COMPAT_FEATURES) == compat &&
	    (ro       & JBD2_KNOWN_ROCOMPAT_FEATURES) == ro &&
	    (incompat & JBD2_KNOWN_INCOMPAT_FEATURES) == incompat)
		return 1;

	return 0;
}

/**
 * int jbd2_journal_set_features () - Mark a given journal feature in the superblock
 * @journal: Journal to act on.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Mark a given journal feature as present on the
 * superblock.  Returns true if the requested features could be set.
 *
 */

int jbd2_journal_set_features (journal_t *journal, unsigned long compat,
			  unsigned long ro, unsigned long incompat)
{
#define INCOMPAT_FEATURE_ON(f) \
		((incompat & (f)) && !(sb->s_feature_incompat & cpu_to_be32(f)))
#define COMPAT_FEATURE_ON(f) \
		((compat & (f)) && !(sb->s_feature_compat & cpu_to_be32(f)))
	journal_superblock_t *sb;

	if (jbd2_journal_check_used_features(journal, compat, ro, incompat))
		return 1;

	if (!jbd2_journal_check_available_features(journal, compat, ro, incompat))
		return 0;

	/* If enabling v2 checksums, turn on v3 instead */
	if (incompat & JBD2_FEATURE_INCOMPAT_CSUM_V2) {
		incompat &= ~JBD2_FEATURE_INCOMPAT_CSUM_V2;
		incompat |= JBD2_FEATURE_INCOMPAT_CSUM_V3;
	}

	/* Asking for checksumming v3 and v1?  Only give them v3. */
	if (incompat & JBD2_FEATURE_INCOMPAT_CSUM_V3 &&
	    compat & JBD2_FEATURE_COMPAT_CHECKSUM)
		compat &= ~JBD2_FEATURE_COMPAT_CHECKSUM;

	jbd_debug(1, "Setting new features 0x%lx/0x%lx/0x%lx\n",
		  compat, ro, incompat);

	sb = journal->j_superblock;

	/* If enabling v3 checksums, update superblock */
	if (INCOMPAT_FEATURE_ON(JBD2_FEATURE_INCOMPAT_CSUM_V3)) {
		sb->s_checksum_type = JBD2_CRC32C_CHKSUM;
		sb->s_feature_compat &=
			~cpu_to_be32(JBD2_FEATURE_COMPAT_CHECKSUM);

		/* Load the checksum driver */
		if (journal->j_chksum_driver == NULL) {
			journal->j_chksum_driver = crypto_alloc_shash("crc32c",
								      0, 0);
			if (IS_ERR(journal->j_chksum_driver)) {
				printk(KERN_ERR "JBD2: Cannot load crc32c "
				       "driver.\n");
				journal->j_chksum_driver = NULL;
				return 0;
			}

			/* Precompute checksum seed for all metadata */
			journal->j_csum_seed = jbd2_chksum(journal, ~0,
							   sb->s_uuid,
							   sizeof(sb->s_uuid));
		}
	}

	/* If enabling v1 checksums, downgrade superblock */
	if (COMPAT_FEATURE_ON(JBD2_FEATURE_COMPAT_CHECKSUM))
		sb->s_feature_incompat &=
			~cpu_to_be32(JBD2_FEATURE_INCOMPAT_CSUM_V2 |
				     JBD2_FEATURE_INCOMPAT_CSUM_V3);

	sb->s_feature_compat    |= cpu_to_be32(compat);
	sb->s_feature_ro_compat |= cpu_to_be32(ro);
	sb->s_feature_incompat  |= cpu_to_be32(incompat);

	return 1;
#undef COMPAT_FEATURE_ON
#undef INCOMPAT_FEATURE_ON
}

/*
 * jbd2_journal_clear_features () - Clear a given journal feature in the
 * 				    superblock
 * @journal: Journal to act on.
 * @compat: bitmask of compatible features
 * @ro: bitmask of features that force read-only mount
 * @incompat: bitmask of incompatible features
 *
 * Clear a given journal feature as present on the
 * superblock.
 */
void jbd2_journal_clear_features(journal_t *journal, unsigned long compat,
				unsigned long ro, unsigned long incompat)
{
	journal_superblock_t *sb;

	jbd_debug(1, "Clear features 0x%lx/0x%lx/0x%lx\n",
		  compat, ro, incompat);

	sb = journal->j_superblock;

	sb->s_feature_compat    &= ~cpu_to_be32(compat);
	sb->s_feature_ro_compat &= ~cpu_to_be32(ro);
	sb->s_feature_incompat  &= ~cpu_to_be32(incompat);
}
EXPORT_SYMBOL(jbd2_journal_clear_features);

/**
 * int jbd2_journal_flush () - Flush journal
 * @journal: Journal to act on.
 *
 * Flush all data for a given journal to disk and empty the journal.
 * Filesystems can use this when remounting readonly to ensure that
 * recovery does not need to happen on remount.
 */

int jbd2_journal_flush(journal_t *journal)
{
	int err = 0;
	transaction_t *transaction = NULL;

	write_lock(&journal->j_state_lock);

	/* Force everything buffered to the log... */
	if (journal->j_running_transaction) {
		transaction = journal->j_running_transaction;
		__jbd2_log_start_commit(journal, transaction->t_tid);
	} else if (journal->j_committing_transaction)
		transaction = journal->j_committing_transaction;

	/* Wait for the log commit to complete... */
	if (transaction) {
		tid_t tid = transaction->t_tid;

		write_unlock(&journal->j_state_lock);
		jbd2_log_wait_commit(journal, tid);
	} else {
		write_unlock(&journal->j_state_lock);
	}

	/* ...and flush everything in the log out to disk. */
	spin_lock(&journal->j_list_lock);
	while (!err && journal->j_checkpoint_transactions != NULL) {
		spin_unlock(&journal->j_list_lock);
		mutex_lock_io(&journal->j_checkpoint_mutex);
		err = jbd2_log_do_checkpoint(journal);
		mutex_unlock(&journal->j_checkpoint_mutex);
		spin_lock(&journal->j_list_lock);
	}
	spin_unlock(&journal->j_list_lock);

	if (is_journal_aborted(journal))
		return -EIO;

	mutex_lock_io(&journal->j_checkpoint_mutex);
	if (!err) {
		err = jbd2_cleanup_journal_tail(journal);
		if (err < 0) {
			mutex_unlock(&journal->j_checkpoint_mutex);
			goto out;
		}
		err = 0;
	}

	/* Finally, mark the journal as really needing no recovery.
	 * This sets s_start==0 in the underlying superblock, which is
	 * the magic code for a fully-recovered superblock.  Any future
	 * commits of data to the journal will restore the current
	 * s_start value. */
	jbd2_mark_journal_empty(journal, REQ_FUA);
	mutex_unlock(&journal->j_checkpoint_mutex);
	write_lock(&journal->j_state_lock);
	J_ASSERT(!journal->j_running_transaction);
	J_ASSERT(!journal->j_committing_transaction);
	J_ASSERT(!journal->j_checkpoint_transactions);
	J_ASSERT(journal->j_head == journal->j_tail);
	J_ASSERT(journal->j_tail_sequence == journal->j_transaction_sequence);
	write_unlock(&journal->j_state_lock);
out:
	return err;
}

/**
 * int jbd2_journal_wipe() - Wipe journal contents
 * @journal: Journal to act on.
 * @write: flag (see below)
 *
 * Wipe out all of the contents of a journal, safely.  This will produce
 * a warning if the journal contains any valid recovery information.
 * Must be called between journal_init_*() and jbd2_journal_load().
 *
 * If 'write' is non-zero, then we wipe out the journal on disk; otherwise
 * we merely suppress recovery.
 */

int jbd2_journal_wipe(journal_t *journal, int write)
{
	int err = 0;

	J_ASSERT (!(journal->j_flags & JBD2_LOADED));

	err = load_superblock(journal);
	if (err)
		return err;

	if (!journal->j_tail)
		goto no_recovery;

	printk(KERN_WARNING "JBD2: %s recovery information on journal\n",
		write ? "Clearing" : "Ignoring");

	err = jbd2_journal_skip_recovery(journal);
	if (write) {
		/* Lock to make assertions happy... */
		mutex_lock(&journal->j_checkpoint_mutex);
		jbd2_mark_journal_empty(journal, REQ_FUA);
		mutex_unlock(&journal->j_checkpoint_mutex);
	}

 no_recovery:
	return err;
}

/*
 * Journal abort has very specific semantics, which we describe
 * for journal abort.
 *
 * Two internal functions, which provide abort to the jbd layer
 * itself are here.
 */

/*
 * Quick version for internal journal use (doesn't lock the journal).
 * Aborts hard --- we mark the abort as occurred, but do _nothing_ else,
 * and don't attempt to make any other journal updates.
 */
void __jbd2_journal_abort_hard(journal_t *journal)
{
	transaction_t *transaction;

	if (journal->j_flags & JBD2_ABORT)
		return;

	printk(KERN_ERR "Aborting journal on device %s.\n",
	       journal->j_devname);

	write_lock(&journal->j_state_lock);
	journal->j_flags |= JBD2_ABORT;
	transaction = journal->j_running_transaction;
	if (transaction)
		__jbd2_log_start_commit(journal, transaction->t_tid);
	write_unlock(&journal->j_state_lock);
}

/* Soft abort: record the abort error status in the journal superblock,
 * but don't do any other IO. */
static void __journal_abort_soft (journal_t *journal, int errno)
{
	if (journal->j_flags & JBD2_ABORT)
		return;

	if (!journal->j_errno)
		journal->j_errno = errno;

	__jbd2_journal_abort_hard(journal);

	if (errno) {
		jbd2_journal_update_sb_errno(journal);
		write_lock(&journal->j_state_lock);
		journal->j_flags |= JBD2_REC_ERR;
		write_unlock(&journal->j_state_lock);
	}
}

/**
 * void jbd2_journal_abort () - Shutdown the journal immediately.
 * @journal: the journal to shutdown.
 * @errno:   an error number to record in the journal indicating
 *           the reason for the shutdown.
 *
 * Perform a complete, immediate shutdown of the ENTIRE
 * journal (not of a single transaction).  This operation cannot be
 * undone without closing and reopening the journal.
 *
 * The jbd2_journal_abort function is intended to support higher level error
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
 * jbd2_journal_stop on an existing handle will return -EIO if we have
 * entered abort state during the update.
 *
 * Recursive transactions are not disturbed by journal abort until the
 * final jbd2_journal_stop, which will receive the -EIO error.
 *
 * Finally, the jbd2_journal_abort call allows the caller to supply an errno
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

void jbd2_journal_abort(journal_t *journal, int errno)
{
	__journal_abort_soft(journal, errno);
}

/**
 * int jbd2_journal_errno () - returns the journal's error state.
 * @journal: journal to examine.
 *
 * This is the errno number set with jbd2_journal_abort(), the last
 * time the journal was mounted - if the journal was stopped
 * without calling abort this will be 0.
 *
 * If the journal has been aborted on this mount time -EROFS will
 * be returned.
 */
int jbd2_journal_errno(journal_t *journal)
{
	int err;

	read_lock(&journal->j_state_lock);
	if (journal->j_flags & JBD2_ABORT)
		err = -EROFS;
	else
		err = journal->j_errno;
	read_unlock(&journal->j_state_lock);
	return err;
}

/**
 * int jbd2_journal_clear_err () - clears the journal's error state
 * @journal: journal to act on.
 *
 * An error must be cleared or acked to take a FS out of readonly
 * mode.
 */
int jbd2_journal_clear_err(journal_t *journal)
{
	int err = 0;

	write_lock(&journal->j_state_lock);
	if (journal->j_flags & JBD2_ABORT)
		err = -EROFS;
	else
		journal->j_errno = 0;
	write_unlock(&journal->j_state_lock);
	return err;
}

/**
 * void jbd2_journal_ack_err() - Ack journal err.
 * @journal: journal to act on.
 *
 * An error must be cleared or acked to take a FS out of readonly
 * mode.
 */
void jbd2_journal_ack_err(journal_t *journal)
{
	write_lock(&journal->j_state_lock);
	if (journal->j_errno)
		journal->j_flags |= JBD2_ACK_ERR;
	write_unlock(&journal->j_state_lock);
}

int jbd2_journal_blocks_per_page(struct inode *inode)
{
	return 1 << (PAGE_SHIFT - inode->i_sb->s_blocksize_bits);
}

/*
 * helper functions to deal with 32 or 64bit block numbers.
 */
size_t journal_tag_bytes(journal_t *journal)
{
	size_t sz;

	if (jbd2_has_feature_csum3(journal))
		return sizeof(journal_block_tag3_t);

	sz = sizeof(journal_block_tag_t);

	if (jbd2_has_feature_csum2(journal))
		sz += sizeof(__u16);

	if (jbd2_has_feature_64bit(journal))
		return sz;
	else
		return sz - sizeof(__u32);
}

/*
 * JBD memory management
 *
 * These functions are used to allocate block-sized chunks of memory
 * used for making copies of buffer_head data.  Very often it will be
 * page-sized chunks of data, but sometimes it will be in
 * sub-page-size chunks.  (For example, 16k pages on Power systems
 * with a 4k block file system.)  For blocks smaller than a page, we
 * use a SLAB allocator.  There are slab caches for each block size,
 * which are allocated at mount time, if necessary, and we only free
 * (all of) the slab caches when/if the jbd2 module is unloaded.  For
 * this reason we don't need to a mutex to protect access to
 * jbd2_slab[] allocating or releasing memory; only in
 * jbd2_journal_create_slab().
 */
#define JBD2_MAX_SLABS 8
static struct kmem_cache *jbd2_slab[JBD2_MAX_SLABS];

static const char *jbd2_slab_names[JBD2_MAX_SLABS] = {
	"jbd2_1k", "jbd2_2k", "jbd2_4k", "jbd2_8k",
	"jbd2_16k", "jbd2_32k", "jbd2_64k", "jbd2_128k"
};


static void jbd2_journal_destroy_slabs(void)
{
	int i;

	for (i = 0; i < JBD2_MAX_SLABS; i++) {
		if (jbd2_slab[i])
			kmem_cache_destroy(jbd2_slab[i]);
		jbd2_slab[i] = NULL;
	}
}

static int jbd2_journal_create_slab(size_t size)
{
	static DEFINE_MUTEX(jbd2_slab_create_mutex);
	int i = order_base_2(size) - 10;
	size_t slab_size;

	if (size == PAGE_SIZE)
		return 0;

	if (i >= JBD2_MAX_SLABS)
		return -EINVAL;

	if (unlikely(i < 0))
		i = 0;
	mutex_lock(&jbd2_slab_create_mutex);
	if (jbd2_slab[i]) {
		mutex_unlock(&jbd2_slab_create_mutex);
		return 0;	/* Already created */
	}

	slab_size = 1 << (i+10);
	jbd2_slab[i] = kmem_cache_create(jbd2_slab_names[i], slab_size,
					 slab_size, 0, NULL);
	mutex_unlock(&jbd2_slab_create_mutex);
	if (!jbd2_slab[i]) {
		printk(KERN_EMERG "JBD2: no memory for jbd2_slab cache\n");
		return -ENOMEM;
	}
	return 0;
}

static struct kmem_cache *get_slab(size_t size)
{
	int i = order_base_2(size) - 10;

	BUG_ON(i >= JBD2_MAX_SLABS);
	if (unlikely(i < 0))
		i = 0;
	BUG_ON(jbd2_slab[i] == NULL);
	return jbd2_slab[i];
}

void *jbd2_alloc(size_t size, gfp_t flags)
{
	void *ptr;

	BUG_ON(size & (size-1)); /* Must be a power of 2 */

	if (size < PAGE_SIZE)
		ptr = kmem_cache_alloc(get_slab(size), flags);
	else
		ptr = (void *)__get_free_pages(flags, get_order(size));

	/* Check alignment; SLUB has gotten this wrong in the past,
	 * and this can lead to user data corruption! */
	BUG_ON(((unsigned long) ptr) & (size-1));

	return ptr;
}

void jbd2_free(void *ptr, size_t size)
{
	if (size < PAGE_SIZE)
		kmem_cache_free(get_slab(size), ptr);
	else
		free_pages((unsigned long)ptr, get_order(size));
};

/*
 * Journal_head storage management
 */
static struct kmem_cache *jbd2_journal_head_cache;
#ifdef CONFIG_JBD2_DEBUG
static atomic_t nr_journal_heads = ATOMIC_INIT(0);
#endif

static int jbd2_journal_init_journal_head_cache(void)
{
	int retval;

	J_ASSERT(jbd2_journal_head_cache == NULL);
	jbd2_journal_head_cache = kmem_cache_create("jbd2_journal_head",
				sizeof(struct journal_head),
				0,		/* offset */
				SLAB_TEMPORARY | SLAB_DESTROY_BY_RCU,
				NULL);		/* ctor */
	retval = 0;
	if (!jbd2_journal_head_cache) {
		retval = -ENOMEM;
		printk(KERN_EMERG "JBD2: no memory for journal_head cache\n");
	}
	return retval;
}

static void jbd2_journal_destroy_journal_head_cache(void)
{
	if (jbd2_journal_head_cache) {
		kmem_cache_destroy(jbd2_journal_head_cache);
		jbd2_journal_head_cache = NULL;
	}
}

/*
 * journal_head splicing and dicing
 */
static struct journal_head *journal_alloc_journal_head(void)
{
	struct journal_head *ret;

#ifdef CONFIG_JBD2_DEBUG
	atomic_inc(&nr_journal_heads);
#endif
	ret = kmem_cache_zalloc(jbd2_journal_head_cache, GFP_NOFS);
	if (!ret) {
		jbd_debug(1, "out of memory for journal_head\n");
		pr_notice_ratelimited("ENOMEM in %s, retrying.\n", __func__);
		ret = kmem_cache_zalloc(jbd2_journal_head_cache,
				GFP_NOFS | __GFP_NOFAIL);
	}
	return ret;
}

static void journal_free_journal_head(struct journal_head *jh)
{
#ifdef CONFIG_JBD2_DEBUG
	atomic_dec(&nr_journal_heads);
	memset(jh, JBD2_POISON_FREE, sizeof(*jh));
#endif
	kmem_cache_free(jbd2_journal_head_cache, jh);
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
 * A journal_head is detached from its buffer_head when the journal_head's
 * b_jcount reaches zero. Running transaction (b_transaction) and checkpoint
 * transaction (b_cp_transaction) hold their references to b_jcount.
 *
 * Various places in the kernel want to attach a journal_head to a buffer_head
 * _before_ attaching the journal_head to a transaction.  To protect the
 * journal_head in this situation, jbd2_journal_add_journal_head elevates the
 * journal_head's b_jcount refcount by one.  The caller must call
 * jbd2_journal_put_journal_head() to undo this.
 *
 * So the typical usage would be:
 *
 *	(Attach a journal_head if needed.  Increments b_jcount)
 *	struct journal_head *jh = jbd2_journal_add_journal_head(bh);
 *	...
 *      (Get another reference for transaction)
 *	jbd2_journal_grab_journal_head(bh);
 *	jh->b_transaction = xxx;
 *	(Put original reference)
 *	jbd2_journal_put_journal_head(jh);
 */

/*
 * Give a buffer_head a journal_head.
 *
 * May sleep.
 */
struct journal_head *jbd2_journal_add_journal_head(struct buffer_head *bh)
{
	struct journal_head *jh;
	struct journal_head *new_jh = NULL;

repeat:
	if (!buffer_jbd(bh))
		new_jh = journal_alloc_journal_head();

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
struct journal_head *jbd2_journal_grab_journal_head(struct buffer_head *bh)
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
	J_ASSERT_JH(jh, jh->b_transaction == NULL);
	J_ASSERT_JH(jh, jh->b_next_transaction == NULL);
	J_ASSERT_JH(jh, jh->b_cp_transaction == NULL);
	J_ASSERT_JH(jh, jh->b_jlist == BJ_None);
	J_ASSERT_BH(bh, buffer_jbd(bh));
	J_ASSERT_BH(bh, jh2bh(jh) == bh);
	BUFFER_TRACE(bh, "remove journal_head");
	if (jh->b_frozen_data) {
		printk(KERN_WARNING "%s: freeing b_frozen_data\n", __func__);
		jbd2_free(jh->b_frozen_data, bh->b_size);
	}
	if (jh->b_committed_data) {
		printk(KERN_WARNING "%s: freeing b_committed_data\n", __func__);
		jbd2_free(jh->b_committed_data, bh->b_size);
	}
	bh->b_private = NULL;
	jh->b_bh = NULL;	/* debug, really */
	clear_buffer_jbd(bh);
	journal_free_journal_head(jh);
}

/*
 * Drop a reference on the passed journal_head.  If it fell to zero then
 * release the journal_head from the buffer_head.
 */
void jbd2_journal_put_journal_head(struct journal_head *jh)
{
	struct buffer_head *bh = jh2bh(jh);

	jbd_lock_bh_journal_head(bh);
	J_ASSERT_JH(jh, jh->b_jcount > 0);
	--jh->b_jcount;
	if (!jh->b_jcount) {
		__journal_remove_journal_head(bh);
		jbd_unlock_bh_journal_head(bh);
		__brelse(bh);
	} else
		jbd_unlock_bh_journal_head(bh);
}

/*
 * Initialize jbd inode head
 */
void jbd2_journal_init_jbd_inode(struct jbd2_inode *jinode, struct inode *inode)
{
	jinode->i_transaction = NULL;
	jinode->i_next_transaction = NULL;
	jinode->i_vfs_inode = inode;
	jinode->i_flags = 0;
	INIT_LIST_HEAD(&jinode->i_list);
}

/*
 * Function to be called before we start removing inode from memory (i.e.,
 * clear_inode() is a fine place to be called from). It removes inode from
 * transaction's lists.
 */
void jbd2_journal_release_jbd_inode(journal_t *journal,
				    struct jbd2_inode *jinode)
{
	if (!journal)
		return;
restart:
	spin_lock(&journal->j_list_lock);
	/* Is commit writing out inode - we have to wait */
	if (jinode->i_flags & JI_COMMIT_RUNNING) {
		wait_queue_head_t *wq;
		DEFINE_WAIT_BIT(wait, &jinode->i_flags, __JI_COMMIT_RUNNING);
		wq = bit_waitqueue(&jinode->i_flags, __JI_COMMIT_RUNNING);
		prepare_to_wait(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
		spin_unlock(&journal->j_list_lock);
		schedule();
		finish_wait(wq, &wait.wait);
		goto restart;
	}

	if (jinode->i_transaction) {
		list_del(&jinode->i_list);
		jinode->i_transaction = NULL;
	}
	spin_unlock(&journal->j_list_lock);
}


#ifdef CONFIG_PROC_FS

#define JBD2_STATS_PROC_NAME "fs/jbd2"

static void __init jbd2_create_jbd_stats_proc_entry(void)
{
	proc_jbd2_stats = proc_mkdir(JBD2_STATS_PROC_NAME, NULL);
}

static void __exit jbd2_remove_jbd_stats_proc_entry(void)
{
	if (proc_jbd2_stats)
		remove_proc_entry(JBD2_STATS_PROC_NAME, NULL);
}

#else

#define jbd2_create_jbd_stats_proc_entry() do {} while (0)
#define jbd2_remove_jbd_stats_proc_entry() do {} while (0)

#endif

struct kmem_cache *jbd2_handle_cache, *jbd2_inode_cache;

static int __init jbd2_journal_init_handle_cache(void)
{
	jbd2_handle_cache = KMEM_CACHE(jbd2_journal_handle, SLAB_TEMPORARY);
	if (jbd2_handle_cache == NULL) {
		printk(KERN_EMERG "JBD2: failed to create handle cache\n");
		return -ENOMEM;
	}
	jbd2_inode_cache = KMEM_CACHE(jbd2_inode, 0);
	if (jbd2_inode_cache == NULL) {
		printk(KERN_EMERG "JBD2: failed to create inode cache\n");
		kmem_cache_destroy(jbd2_handle_cache);
		return -ENOMEM;
	}
	return 0;
}

static void jbd2_journal_destroy_handle_cache(void)
{
	if (jbd2_handle_cache)
		kmem_cache_destroy(jbd2_handle_cache);
	if (jbd2_inode_cache)
		kmem_cache_destroy(jbd2_inode_cache);

}

/*
 * Module startup and shutdown
 */

static int __init journal_init_caches(void)
{
	int ret;

	ret = jbd2_journal_init_revoke_caches();
	if (ret == 0)
		ret = jbd2_journal_init_journal_head_cache();
	if (ret == 0)
		ret = jbd2_journal_init_handle_cache();
	if (ret == 0)
		ret = jbd2_journal_init_transaction_cache();
	return ret;
}

static void jbd2_journal_destroy_caches(void)
{
	jbd2_journal_destroy_revoke_caches();
	jbd2_journal_destroy_journal_head_cache();
	jbd2_journal_destroy_handle_cache();
	jbd2_journal_destroy_transaction_cache();
	jbd2_journal_destroy_slabs();
}

static int __init journal_init(void)
{
	int ret;

	BUILD_BUG_ON(sizeof(struct journal_superblock_s) != 1024);

	ret = journal_init_caches();
	if (ret == 0) {
		jbd2_create_jbd_stats_proc_entry();
	} else {
		jbd2_journal_destroy_caches();
	}
	return ret;
}

static void __exit journal_exit(void)
{
#ifdef CONFIG_JBD2_DEBUG
	int n = atomic_read(&nr_journal_heads);
	if (n)
		printk(KERN_ERR "JBD2: leaked %d journal_heads!\n", n);
#endif
	jbd2_remove_jbd_stats_proc_entry();
	jbd2_journal_destroy_caches();
}

MODULE_LICENSE("GPL");
module_init(journal_init);
module_exit(journal_exit);

