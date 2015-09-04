/*
 * linux/fs/jbd2/checkpoint.c
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1999
 *
 * Copyright 1999 Red Hat Software --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Checkpoint routines for the generic filesystem journaling code.
 * Part of the ext2fs journaling system.
 *
 * Checkpointing is the process of ensuring that a section of the log is
 * committed fully to disk, so that that portion of the log can be
 * reused.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <trace/events/jbd2.h>

/*
 * Unlink a buffer from a transaction checkpoint list.
 *
 * Called with j_list_lock held.
 */
static inline void __buffer_unlink_first(struct journal_head *jh)
{
	transaction_t *transaction = jh->b_cp_transaction;

	jh->b_cpnext->b_cpprev = jh->b_cpprev;
	jh->b_cpprev->b_cpnext = jh->b_cpnext;
	if (transaction->t_checkpoint_list == jh) {
		transaction->t_checkpoint_list = jh->b_cpnext;
		if (transaction->t_checkpoint_list == jh)
			transaction->t_checkpoint_list = NULL;
	}
}

/*
 * Unlink a buffer from a transaction checkpoint(io) list.
 *
 * Called with j_list_lock held.
 */
static inline void __buffer_unlink(struct journal_head *jh)
{
	transaction_t *transaction = jh->b_cp_transaction;

	__buffer_unlink_first(jh);
	if (transaction->t_checkpoint_io_list == jh) {
		transaction->t_checkpoint_io_list = jh->b_cpnext;
		if (transaction->t_checkpoint_io_list == jh)
			transaction->t_checkpoint_io_list = NULL;
	}
}

/*
 * Move a buffer from the checkpoint list to the checkpoint io list
 *
 * Called with j_list_lock held
 */
static inline void __buffer_relink_io(struct journal_head *jh)
{
	transaction_t *transaction = jh->b_cp_transaction;

	__buffer_unlink_first(jh);

	if (!transaction->t_checkpoint_io_list) {
		jh->b_cpnext = jh->b_cpprev = jh;
	} else {
		jh->b_cpnext = transaction->t_checkpoint_io_list;
		jh->b_cpprev = transaction->t_checkpoint_io_list->b_cpprev;
		jh->b_cpprev->b_cpnext = jh;
		jh->b_cpnext->b_cpprev = jh;
	}
	transaction->t_checkpoint_io_list = jh;
}

/*
 * Try to release a checkpointed buffer from its transaction.
 * Returns 1 if we released it and 2 if we also released the
 * whole transaction.
 *
 * Requires j_list_lock
 */
static int __try_to_free_cp_buf(struct journal_head *jh)
{
	int ret = 0;
	struct buffer_head *bh = jh2bh(jh);

	if (jh->b_transaction == NULL && !buffer_locked(bh) &&
	    !buffer_dirty(bh) && !buffer_write_io_error(bh)) {
		JBUFFER_TRACE(jh, "remove from checkpoint list");
		ret = __jbd2_journal_remove_checkpoint(jh) + 1;
	}
	return ret;
}

/*
 * __jbd2_log_wait_for_space: wait until there is space in the journal.
 *
 * Called under j-state_lock *only*.  It will be unlocked if we have to wait
 * for a checkpoint to free up some space in the log.
 */
void __jbd2_log_wait_for_space(journal_t *journal)
{
	int nblocks, space_left;
	/* assert_spin_locked(&journal->j_state_lock); */

	nblocks = jbd2_space_needed(journal);
	while (jbd2_log_space_left(journal) < nblocks) {
		write_unlock(&journal->j_state_lock);
		mutex_lock(&journal->j_checkpoint_mutex);

		/*
		 * Test again, another process may have checkpointed while we
		 * were waiting for the checkpoint lock. If there are no
		 * transactions ready to be checkpointed, try to recover
		 * journal space by calling cleanup_journal_tail(), and if
		 * that doesn't work, by waiting for the currently committing
		 * transaction to complete.  If there is absolutely no way
		 * to make progress, this is either a BUG or corrupted
		 * filesystem, so abort the journal and leave a stack
		 * trace for forensic evidence.
		 */
		write_lock(&journal->j_state_lock);
		if (journal->j_flags & JBD2_ABORT) {
			mutex_unlock(&journal->j_checkpoint_mutex);
			return;
		}
		spin_lock(&journal->j_list_lock);
		nblocks = jbd2_space_needed(journal);
		space_left = jbd2_log_space_left(journal);
		if (space_left < nblocks) {
			int chkpt = journal->j_checkpoint_transactions != NULL;
			tid_t tid = 0;

			if (journal->j_committing_transaction)
				tid = journal->j_committing_transaction->t_tid;
			spin_unlock(&journal->j_list_lock);
			write_unlock(&journal->j_state_lock);
			if (chkpt) {
				jbd2_log_do_checkpoint(journal);
			} else if (jbd2_cleanup_journal_tail(journal) == 0) {
				/* We were able to recover space; yay! */
				;
			} else if (tid) {
				/*
				 * jbd2_journal_commit_transaction() may want
				 * to take the checkpoint_mutex if JBD2_FLUSHED
				 * is set.  So we need to temporarily drop it.
				 */
				mutex_unlock(&journal->j_checkpoint_mutex);
				jbd2_log_wait_commit(journal, tid);
				write_lock(&journal->j_state_lock);
				continue;
			} else {
				printk(KERN_ERR "%s: needed %d blocks and "
				       "only had %d space available\n",
				       __func__, nblocks, space_left);
				printk(KERN_ERR "%s: no way to get more "
				       "journal space in %s\n", __func__,
				       journal->j_devname);
				WARN_ON(1);
				jbd2_journal_abort(journal, 0);
			}
			write_lock(&journal->j_state_lock);
		} else {
			spin_unlock(&journal->j_list_lock);
		}
		mutex_unlock(&journal->j_checkpoint_mutex);
	}
}

static void
__flush_batch(journal_t *journal, int *batch_count)
{
	int i;
	struct blk_plug plug;

	blk_start_plug(&plug);
	for (i = 0; i < *batch_count; i++)
		write_dirty_buffer(journal->j_chkpt_bhs[i], WRITE_SYNC);
	blk_finish_plug(&plug);

	for (i = 0; i < *batch_count; i++) {
		struct buffer_head *bh = journal->j_chkpt_bhs[i];
		BUFFER_TRACE(bh, "brelse");
		__brelse(bh);
	}
	*batch_count = 0;
}

/*
 * Perform an actual checkpoint. We take the first transaction on the
 * list of transactions to be checkpointed and send all its buffers
 * to disk. We submit larger chunks of data at once.
 *
 * The journal should be locked before calling this function.
 * Called with j_checkpoint_mutex held.
 */
int jbd2_log_do_checkpoint(journal_t *journal)
{
	struct journal_head	*jh;
	struct buffer_head	*bh;
	transaction_t		*transaction;
	tid_t			this_tid;
	int			result, batch_count = 0;

	jbd_debug(1, "Start checkpoint\n");

	/*
	 * First thing: if there are any transactions in the log which
	 * don't need checkpointing, just eliminate them from the
	 * journal straight away.
	 */
	result = jbd2_cleanup_journal_tail(journal);
	trace_jbd2_checkpoint(journal, result);
	jbd_debug(1, "cleanup_journal_tail returned %d\n", result);
	if (result <= 0)
		return result;

	/*
	 * OK, we need to start writing disk blocks.  Take one transaction
	 * and write it.
	 */
	result = 0;
	spin_lock(&journal->j_list_lock);
	if (!journal->j_checkpoint_transactions)
		goto out;
	transaction = journal->j_checkpoint_transactions;
	if (transaction->t_chp_stats.cs_chp_time == 0)
		transaction->t_chp_stats.cs_chp_time = jiffies;
	this_tid = transaction->t_tid;
restart:
	/*
	 * If someone cleaned up this transaction while we slept, we're
	 * done (maybe it's a new transaction, but it fell at the same
	 * address).
	 */
	if (journal->j_checkpoint_transactions != transaction ||
	    transaction->t_tid != this_tid)
		goto out;

	/* checkpoint all of the transaction's buffers */
	while (transaction->t_checkpoint_list) {
		jh = transaction->t_checkpoint_list;
		bh = jh2bh(jh);

		if (buffer_locked(bh)) {
			spin_unlock(&journal->j_list_lock);
			get_bh(bh);
			wait_on_buffer(bh);
			/* the journal_head may have gone by now */
			BUFFER_TRACE(bh, "brelse");
			__brelse(bh);
			goto retry;
		}
		if (jh->b_transaction != NULL) {
			transaction_t *t = jh->b_transaction;
			tid_t tid = t->t_tid;

			transaction->t_chp_stats.cs_forced_to_close++;
			spin_unlock(&journal->j_list_lock);
			if (unlikely(journal->j_flags & JBD2_UNMOUNT))
				/*
				 * The journal thread is dead; so
				 * starting and waiting for a commit
				 * to finish will cause us to wait for
				 * a _very_ long time.
				 */
				printk(KERN_ERR
		"JBD2: %s: Waiting for Godot: block %llu\n",
		journal->j_devname, (unsigned long long) bh->b_blocknr);

			jbd2_log_start_commit(journal, tid);
			jbd2_log_wait_commit(journal, tid);
			goto retry;
		}
		if (!buffer_dirty(bh)) {
			if (unlikely(buffer_write_io_error(bh)) && !result)
				result = -EIO;
			BUFFER_TRACE(bh, "remove from checkpoint");
			if (__jbd2_journal_remove_checkpoint(jh))
				/* The transaction was released; we're done */
				goto out;
			continue;
		}
		/*
		 * Important: we are about to write the buffer, and
		 * possibly block, while still holding the journal
		 * lock.  We cannot afford to let the transaction
		 * logic start messing around with this buffer before
		 * we write it to disk, as that would break
		 * recoverability.
		 */
		BUFFER_TRACE(bh, "queue");
		get_bh(bh);
		J_ASSERT_BH(bh, !buffer_jwrite(bh));
		journal->j_chkpt_bhs[batch_count++] = bh;
		__buffer_relink_io(jh);
		transaction->t_chp_stats.cs_written++;
		if ((batch_count == JBD2_NR_BATCH) ||
		    need_resched() ||
		    spin_needbreak(&journal->j_list_lock))
			goto unlock_and_flush;
	}

	if (batch_count) {
		unlock_and_flush:
			spin_unlock(&journal->j_list_lock);
		retry:
			if (batch_count)
				__flush_batch(journal, &batch_count);
			spin_lock(&journal->j_list_lock);
			goto restart;
	}

	/*
	 * Now we issued all of the transaction's buffers, let's deal
	 * with the buffers that are out for I/O.
	 */
restart2:
	/* Did somebody clean up the transaction in the meanwhile? */
	if (journal->j_checkpoint_transactions != transaction ||
	    transaction->t_tid != this_tid)
		goto out;

	while (transaction->t_checkpoint_io_list) {
		jh = transaction->t_checkpoint_io_list;
		bh = jh2bh(jh);
		if (buffer_locked(bh)) {
			spin_unlock(&journal->j_list_lock);
			get_bh(bh);
			wait_on_buffer(bh);
			/* the journal_head may have gone by now */
			BUFFER_TRACE(bh, "brelse");
			__brelse(bh);
			spin_lock(&journal->j_list_lock);
			goto restart2;
		}
		if (unlikely(buffer_write_io_error(bh)) && !result)
			result = -EIO;

		/*
		 * Now in whatever state the buffer currently is, we
		 * know that it has been written out and so we can
		 * drop it from the list
		 */
		if (__jbd2_journal_remove_checkpoint(jh))
			break;
	}
out:
	spin_unlock(&journal->j_list_lock);
	if (result < 0)
		jbd2_journal_abort(journal, result);
	else
		result = jbd2_cleanup_journal_tail(journal);

	return (result < 0) ? result : 0;
}

/*
 * Check the list of checkpoint transactions for the journal to see if
 * we have already got rid of any since the last update of the log tail
 * in the journal superblock.  If so, we can instantly roll the
 * superblock forward to remove those transactions from the log.
 *
 * Return <0 on error, 0 on success, 1 if there was nothing to clean up.
 *
 * Called with the journal lock held.
 *
 * This is the only part of the journaling code which really needs to be
 * aware of transaction aborts.  Checkpointing involves writing to the
 * main filesystem area rather than to the journal, so it can proceed
 * even in abort state, but we must not update the super block if
 * checkpointing may have failed.  Otherwise, we would lose some metadata
 * buffers which should be written-back to the filesystem.
 */

int jbd2_cleanup_journal_tail(journal_t *journal)
{
	tid_t		first_tid;
	unsigned long	blocknr;

	if (is_journal_aborted(journal))
		return -EIO;

	if (!jbd2_journal_get_log_tail(journal, &first_tid, &blocknr))
		return 1;
	J_ASSERT(blocknr != 0);

	/*
	 * We need to make sure that any blocks that were recently written out
	 * --- perhaps by jbd2_log_do_checkpoint() --- are flushed out before
	 * we drop the transactions from the journal. It's unlikely this will
	 * be necessary, especially with an appropriately sized journal, but we
	 * need this to guarantee correctness.  Fortunately
	 * jbd2_cleanup_journal_tail() doesn't get called all that often.
	 */
	if (journal->j_flags & JBD2_BARRIER)
		blkdev_issue_flush(journal->j_fs_dev, GFP_NOFS, NULL);

	return __jbd2_update_log_tail(journal, first_tid, blocknr);
}


/* Checkpoint list management */

/*
 * journal_clean_one_cp_list
 *
 * Find all the written-back checkpoint buffers in the given list and
 * release them. If 'destroy' is set, clean all buffers unconditionally.
 *
 * Called with j_list_lock held.
 * Returns 1 if we freed the transaction, 0 otherwise.
 */
static int journal_clean_one_cp_list(struct journal_head *jh, bool destroy)
{
	struct journal_head *last_jh;
	struct journal_head *next_jh = jh;
	int ret;
	int freed = 0;

	if (!jh)
		return 0;

	last_jh = jh->b_cpprev;
	do {
		jh = next_jh;
		next_jh = jh->b_cpnext;
		if (!destroy)
			ret = __try_to_free_cp_buf(jh);
		else
			ret = __jbd2_journal_remove_checkpoint(jh) + 1;
		if (!ret)
			return freed;
		if (ret == 2)
			return 1;
		freed = 1;
		/*
		 * This function only frees up some memory
		 * if possible so we dont have an obligation
		 * to finish processing. Bail out if preemption
		 * requested:
		 */
		if (need_resched())
			return freed;
	} while (jh != last_jh);

	return freed;
}

/*
 * journal_clean_checkpoint_list
 *
 * Find all the written-back checkpoint buffers in the journal and release them.
 * If 'destroy' is set, release all buffers unconditionally.
 *
 * Called with j_list_lock held.
 */
void __jbd2_journal_clean_checkpoint_list(journal_t *journal, bool destroy)
{
	transaction_t *transaction, *last_transaction, *next_transaction;
	int ret;

	transaction = journal->j_checkpoint_transactions;
	if (!transaction)
		return;

	last_transaction = transaction->t_cpprev;
	next_transaction = transaction;
	do {
		transaction = next_transaction;
		next_transaction = transaction->t_cpnext;
		ret = journal_clean_one_cp_list(transaction->t_checkpoint_list,
						destroy);
		/*
		 * This function only frees up some memory if possible so we
		 * dont have an obligation to finish processing. Bail out if
		 * preemption requested:
		 */
		if (need_resched())
			return;
		if (ret)
			continue;
		/*
		 * It is essential that we are as careful as in the case of
		 * t_checkpoint_list with removing the buffer from the list as
		 * we can possibly see not yet submitted buffers on io_list
		 */
		ret = journal_clean_one_cp_list(transaction->
				t_checkpoint_io_list, destroy);
		if (need_resched())
			return;
		/*
		 * Stop scanning if we couldn't free the transaction. This
		 * avoids pointless scanning of transactions which still
		 * weren't checkpointed.
		 */
		if (!ret)
			return;
	} while (transaction != last_transaction);
}

/*
 * Remove buffers from all checkpoint lists as journal is aborted and we just
 * need to free memory
 */
void jbd2_journal_destroy_checkpoint(journal_t *journal)
{
	/*
	 * We loop because __jbd2_journal_clean_checkpoint_list() may abort
	 * early due to a need of rescheduling.
	 */
	while (1) {
		spin_lock(&journal->j_list_lock);
		if (!journal->j_checkpoint_transactions) {
			spin_unlock(&journal->j_list_lock);
			break;
		}
		__jbd2_journal_clean_checkpoint_list(journal, true);
		spin_unlock(&journal->j_list_lock);
		cond_resched();
	}
}

/*
 * journal_remove_checkpoint: called after a buffer has been committed
 * to disk (either by being write-back flushed to disk, or being
 * committed to the log).
 *
 * We cannot safely clean a transaction out of the log until all of the
 * buffer updates committed in that transaction have safely been stored
 * elsewhere on disk.  To achieve this, all of the buffers in a
 * transaction need to be maintained on the transaction's checkpoint
 * lists until they have been rewritten, at which point this function is
 * called to remove the buffer from the existing transaction's
 * checkpoint lists.
 *
 * The function returns 1 if it frees the transaction, 0 otherwise.
 * The function can free jh and bh.
 *
 * This function is called with j_list_lock held.
 */
int __jbd2_journal_remove_checkpoint(struct journal_head *jh)
{
	struct transaction_chp_stats_s *stats;
	transaction_t *transaction;
	journal_t *journal;
	int ret = 0;

	JBUFFER_TRACE(jh, "entry");

	if ((transaction = jh->b_cp_transaction) == NULL) {
		JBUFFER_TRACE(jh, "not on transaction");
		goto out;
	}
	journal = transaction->t_journal;

	JBUFFER_TRACE(jh, "removing from transaction");
	__buffer_unlink(jh);
	jh->b_cp_transaction = NULL;
	jbd2_journal_put_journal_head(jh);

	if (transaction->t_checkpoint_list != NULL ||
	    transaction->t_checkpoint_io_list != NULL)
		goto out;

	/*
	 * There is one special case to worry about: if we have just pulled the
	 * buffer off a running or committing transaction's checkpoing list,
	 * then even if the checkpoint list is empty, the transaction obviously
	 * cannot be dropped!
	 *
	 * The locking here around t_state is a bit sleazy.
	 * See the comment at the end of jbd2_journal_commit_transaction().
	 */
	if (transaction->t_state != T_FINISHED)
		goto out;

	/* OK, that was the last buffer for the transaction: we can now
	   safely remove this transaction from the log */
	stats = &transaction->t_chp_stats;
	if (stats->cs_chp_time)
		stats->cs_chp_time = jbd2_time_diff(stats->cs_chp_time,
						    jiffies);
	trace_jbd2_checkpoint_stats(journal->j_fs_dev->bd_dev,
				    transaction->t_tid, stats);

	__jbd2_journal_drop_transaction(journal, transaction);
	jbd2_journal_free_transaction(transaction);
	ret = 1;
out:
	return ret;
}

/*
 * journal_insert_checkpoint: put a committed buffer onto a checkpoint
 * list so that we know when it is safe to clean the transaction out of
 * the log.
 *
 * Called with the journal locked.
 * Called with j_list_lock held.
 */
void __jbd2_journal_insert_checkpoint(struct journal_head *jh,
			       transaction_t *transaction)
{
	JBUFFER_TRACE(jh, "entry");
	J_ASSERT_JH(jh, buffer_dirty(jh2bh(jh)) || buffer_jbddirty(jh2bh(jh)));
	J_ASSERT_JH(jh, jh->b_cp_transaction == NULL);

	/* Get reference for checkpointing transaction */
	jbd2_journal_grab_journal_head(jh2bh(jh));
	jh->b_cp_transaction = transaction;

	if (!transaction->t_checkpoint_list) {
		jh->b_cpnext = jh->b_cpprev = jh;
	} else {
		jh->b_cpnext = transaction->t_checkpoint_list;
		jh->b_cpprev = transaction->t_checkpoint_list->b_cpprev;
		jh->b_cpprev->b_cpnext = jh;
		jh->b_cpnext->b_cpprev = jh;
	}
	transaction->t_checkpoint_list = jh;
}

/*
 * We've finished with this transaction structure: adios...
 *
 * The transaction must have no links except for the checkpoint by this
 * point.
 *
 * Called with the journal locked.
 * Called with j_list_lock held.
 */

void __jbd2_journal_drop_transaction(journal_t *journal, transaction_t *transaction)
{
	assert_spin_locked(&journal->j_list_lock);
	if (transaction->t_cpnext) {
		transaction->t_cpnext->t_cpprev = transaction->t_cpprev;
		transaction->t_cpprev->t_cpnext = transaction->t_cpnext;
		if (journal->j_checkpoint_transactions == transaction)
			journal->j_checkpoint_transactions =
				transaction->t_cpnext;
		if (journal->j_checkpoint_transactions == transaction)
			journal->j_checkpoint_transactions = NULL;
	}

	J_ASSERT(transaction->t_state == T_FINISHED);
	J_ASSERT(transaction->t_buffers == NULL);
	J_ASSERT(transaction->t_forget == NULL);
	J_ASSERT(transaction->t_shadow_list == NULL);
	J_ASSERT(transaction->t_checkpoint_list == NULL);
	J_ASSERT(transaction->t_checkpoint_io_list == NULL);
	J_ASSERT(atomic_read(&transaction->t_updates) == 0);
	J_ASSERT(journal->j_committing_transaction != transaction);
	J_ASSERT(journal->j_running_transaction != transaction);

	trace_jbd2_drop_transaction(journal, transaction);

	jbd_debug(1, "Dropping transaction %d, all done\n", transaction->t_tid);
}
