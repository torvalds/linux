/*
 * linux/fs/commit.c
 *
 * Written by Stephen C. Tweedie <sct@redhat.com>, 1998
 *
 * Copyright 1998 Red Hat corp --- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * Journal commit routines for the generic filesystem journaling code;
 * part of the ext2fs journaling system.
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

/*
 * Default IO end handler for temporary BJ_IO buffer_heads.
 */
static void journal_end_buffer_io_sync(struct buffer_head *bh, int uptodate)
{
	BUFFER_TRACE(bh, "");
	if (uptodate)
		set_buffer_uptodate(bh);
	else
		clear_buffer_uptodate(bh);
	unlock_buffer(bh);
}

/*
 * When an ext3-ordered file is truncated, it is possible that many pages are
 * not sucessfully freed, because they are attached to a committing transaction.
 * After the transaction commits, these pages are left on the LRU, with no
 * ->mapping, and with attached buffers.  These pages are trivially reclaimable
 * by the VM, but their apparent absence upsets the VM accounting, and it makes
 * the numbers in /proc/meminfo look odd.
 *
 * So here, we have a buffer which has just come off the forget list.  Look to
 * see if we can strip all buffers from the backing page.
 *
 * Called under lock_journal(), and possibly under journal_datalist_lock.  The
 * caller provided us with a ref against the buffer, and we drop that here.
 */
static void release_buffer_page(struct buffer_head *bh)
{
	struct page *page;

	if (buffer_dirty(bh))
		goto nope;
	if (atomic_read(&bh->b_count) != 1)
		goto nope;
	page = bh->b_page;
	if (!page)
		goto nope;
	if (page->mapping)
		goto nope;

	/* OK, it's a truncated page */
	if (TestSetPageLocked(page))
		goto nope;

	page_cache_get(page);
	__brelse(bh);
	try_to_free_buffers(page);
	unlock_page(page);
	page_cache_release(page);
	return;

nope:
	__brelse(bh);
}

/*
 * Try to acquire jbd_lock_bh_state() against the buffer, when j_list_lock is
 * held.  For ranking reasons we must trylock.  If we lose, schedule away and
 * return 0.  j_list_lock is dropped in this case.
 */
static int inverted_lock(journal_t *journal, struct buffer_head *bh)
{
	if (!jbd_trylock_bh_state(bh)) {
		spin_unlock(&journal->j_list_lock);
		schedule();
		return 0;
	}
	return 1;
}

/* Done it all: now write the commit record.  We should have
 * cleaned up our previous buffers by now, so if we are in abort
 * mode we can now just skip the rest of the journal write
 * entirely.
 *
 * Returns 1 if the journal needs to be aborted or 0 on success
 */
static int journal_write_commit_record(journal_t *journal,
					transaction_t *commit_transaction)
{
	struct journal_head *descriptor;
	struct buffer_head *bh;
	int i, ret;
	int barrier_done = 0;

	if (is_journal_aborted(journal))
		return 0;

	descriptor = journal_get_descriptor_buffer(journal);
	if (!descriptor)
		return 1;

	bh = jh2bh(descriptor);

	/* AKPM: buglet - add `i' to tmp! */
	for (i = 0; i < bh->b_size; i += 512) {
		journal_header_t *tmp = (journal_header_t*)bh->b_data;
		tmp->h_magic = cpu_to_be32(JFS_MAGIC_NUMBER);
		tmp->h_blocktype = cpu_to_be32(JFS_COMMIT_BLOCK);
		tmp->h_sequence = cpu_to_be32(commit_transaction->t_tid);
	}

	JBUFFER_TRACE(descriptor, "write commit block");
	set_buffer_dirty(bh);
	if (journal->j_flags & JFS_BARRIER) {
		set_buffer_ordered(bh);
		barrier_done = 1;
	}
	ret = sync_dirty_buffer(bh);
	/* is it possible for another commit to fail at roughly
	 * the same time as this one?  If so, we don't want to
	 * trust the barrier flag in the super, but instead want
	 * to remember if we sent a barrier request
	 */
	if (ret == -EOPNOTSUPP && barrier_done) {
		char b[BDEVNAME_SIZE];

		printk(KERN_WARNING
			"JBD: barrier-based sync failed on %s - "
			"disabling barriers\n",
			bdevname(journal->j_dev, b));
		spin_lock(&journal->j_state_lock);
		journal->j_flags &= ~JFS_BARRIER;
		spin_unlock(&journal->j_state_lock);

		/* And try again, without the barrier */
		clear_buffer_ordered(bh);
		set_buffer_uptodate(bh);
		set_buffer_dirty(bh);
		ret = sync_dirty_buffer(bh);
	}
	put_bh(bh);		/* One for getblk() */
	journal_put_journal_head(descriptor);

	return (ret == -EIO);
}

/*
 * journal_commit_transaction
 *
 * The primary function for committing a transaction to the log.  This
 * function is called by the journal thread to begin a complete commit.
 */
void journal_commit_transaction(journal_t *journal)
{
	transaction_t *commit_transaction;
	struct journal_head *jh, *new_jh, *descriptor;
	struct buffer_head **wbuf = journal->j_wbuf;
	int bufs;
	int flags;
	int err;
	unsigned long blocknr;
	char *tagp = NULL;
	journal_header_t *header;
	journal_block_tag_t *tag = NULL;
	int space_left = 0;
	int first_tag = 0;
	int tag_flag;
	int i;

	/*
	 * First job: lock down the current transaction and wait for
	 * all outstanding updates to complete.
	 */

#ifdef COMMIT_STATS
	spin_lock(&journal->j_list_lock);
	summarise_journal_usage(journal);
	spin_unlock(&journal->j_list_lock);
#endif

	/* Do we need to erase the effects of a prior journal_flush? */
	if (journal->j_flags & JFS_FLUSHED) {
		jbd_debug(3, "super block updated\n");
		journal_update_superblock(journal, 1);
	} else {
		jbd_debug(3, "superblock not updated\n");
	}

	J_ASSERT(journal->j_running_transaction != NULL);
	J_ASSERT(journal->j_committing_transaction == NULL);

	commit_transaction = journal->j_running_transaction;
	J_ASSERT(commit_transaction->t_state == T_RUNNING);

	jbd_debug(1, "JBD: starting commit of transaction %d\n",
			commit_transaction->t_tid);

	spin_lock(&journal->j_state_lock);
	commit_transaction->t_state = T_LOCKED;

	spin_lock(&commit_transaction->t_handle_lock);
	while (commit_transaction->t_updates) {
		DEFINE_WAIT(wait);

		prepare_to_wait(&journal->j_wait_updates, &wait,
					TASK_UNINTERRUPTIBLE);
		if (commit_transaction->t_updates) {
			spin_unlock(&commit_transaction->t_handle_lock);
			spin_unlock(&journal->j_state_lock);
			schedule();
			spin_lock(&journal->j_state_lock);
			spin_lock(&commit_transaction->t_handle_lock);
		}
		finish_wait(&journal->j_wait_updates, &wait);
	}
	spin_unlock(&commit_transaction->t_handle_lock);

	J_ASSERT (commit_transaction->t_outstanding_credits <=
			journal->j_max_transaction_buffers);

	/*
	 * First thing we are allowed to do is to discard any remaining
	 * BJ_Reserved buffers.  Note, it is _not_ permissible to assume
	 * that there are no such buffers: if a large filesystem
	 * operation like a truncate needs to split itself over multiple
	 * transactions, then it may try to do a journal_restart() while
	 * there are still BJ_Reserved buffers outstanding.  These must
	 * be released cleanly from the current transaction.
	 *
	 * In this case, the filesystem must still reserve write access
	 * again before modifying the buffer in the new transaction, but
	 * we do not require it to remember exactly which old buffers it
	 * has reserved.  This is consistent with the existing behaviour
	 * that multiple journal_get_write_access() calls to the same
	 * buffer are perfectly permissable.
	 */
	while (commit_transaction->t_reserved_list) {
		jh = commit_transaction->t_reserved_list;
		JBUFFER_TRACE(jh, "reserved, unused: refile");
		/*
		 * A journal_get_undo_access()+journal_release_buffer() may
		 * leave undo-committed data.
		 */
		if (jh->b_committed_data) {
			struct buffer_head *bh = jh2bh(jh);

			jbd_lock_bh_state(bh);
			kfree(jh->b_committed_data);
			jh->b_committed_data = NULL;
			jbd_unlock_bh_state(bh);
		}
		journal_refile_buffer(journal, jh);
	}

	/*
	 * Now try to drop any written-back buffers from the journal's
	 * checkpoint lists.  We do this *before* commit because it potentially
	 * frees some memory
	 */
	spin_lock(&journal->j_list_lock);
	__journal_clean_checkpoint_list(journal);
	spin_unlock(&journal->j_list_lock);

	jbd_debug (3, "JBD: commit phase 1\n");

	/*
	 * Switch to a new revoke table.
	 */
	journal_switch_revoke_table(journal);

	commit_transaction->t_state = T_FLUSH;
	journal->j_committing_transaction = commit_transaction;
	journal->j_running_transaction = NULL;
	commit_transaction->t_log_start = journal->j_head;
	wake_up(&journal->j_wait_transaction_locked);
	spin_unlock(&journal->j_state_lock);

	jbd_debug (3, "JBD: commit phase 2\n");

	/*
	 * First, drop modified flag: all accesses to the buffers
	 * will be tracked for a new trasaction only -bzzz
	 */
	spin_lock(&journal->j_list_lock);
	if (commit_transaction->t_buffers) {
		new_jh = jh = commit_transaction->t_buffers->b_tnext;
		do {
			J_ASSERT_JH(new_jh, new_jh->b_modified == 1 ||
					new_jh->b_modified == 0);
			new_jh->b_modified = 0;
			new_jh = new_jh->b_tnext;
		} while (new_jh != jh);
	}
	spin_unlock(&journal->j_list_lock);

	/*
	 * Now start flushing things to disk, in the order they appear
	 * on the transaction lists.  Data blocks go first.
	 */

	err = 0;
	/*
	 * Whenever we unlock the journal and sleep, things can get added
	 * onto ->t_sync_datalist, so we have to keep looping back to
	 * write_out_data until we *know* that the list is empty.
	 */
	bufs = 0;
	/*
	 * Cleanup any flushed data buffers from the data list.  Even in
	 * abort mode, we want to flush this out as soon as possible.
	 */
write_out_data:
	cond_resched();
	spin_lock(&journal->j_list_lock);

	while (commit_transaction->t_sync_datalist) {
		struct buffer_head *bh;

		jh = commit_transaction->t_sync_datalist;
		commit_transaction->t_sync_datalist = jh->b_tnext;
		bh = jh2bh(jh);
		if (buffer_locked(bh)) {
			BUFFER_TRACE(bh, "locked");
			if (!inverted_lock(journal, bh))
				goto write_out_data;
			__journal_temp_unlink_buffer(jh);
			__journal_file_buffer(jh, commit_transaction,
						BJ_Locked);
			jbd_unlock_bh_state(bh);
			if (lock_need_resched(&journal->j_list_lock)) {
				spin_unlock(&journal->j_list_lock);
				goto write_out_data;
			}
		} else {
			if (buffer_dirty(bh)) {
				BUFFER_TRACE(bh, "start journal writeout");
				get_bh(bh);
				wbuf[bufs++] = bh;
				if (bufs == journal->j_wbufsize) {
					jbd_debug(2, "submit %d writes\n",
							bufs);
					spin_unlock(&journal->j_list_lock);
					ll_rw_block(SWRITE, bufs, wbuf);
					journal_brelse_array(wbuf, bufs);
					bufs = 0;
					goto write_out_data;
				}
			} else {
				BUFFER_TRACE(bh, "writeout complete: unfile");
				if (!inverted_lock(journal, bh))
					goto write_out_data;
				__journal_unfile_buffer(jh);
				jbd_unlock_bh_state(bh);
				journal_remove_journal_head(bh);
				put_bh(bh);
				if (lock_need_resched(&journal->j_list_lock)) {
					spin_unlock(&journal->j_list_lock);
					goto write_out_data;
				}
			}
		}
	}

	if (bufs) {
		spin_unlock(&journal->j_list_lock);
		ll_rw_block(SWRITE, bufs, wbuf);
		journal_brelse_array(wbuf, bufs);
		spin_lock(&journal->j_list_lock);
	}

	/*
	 * Wait for all previously submitted IO to complete.
	 */
	while (commit_transaction->t_locked_list) {
		struct buffer_head *bh;

		jh = commit_transaction->t_locked_list->b_tprev;
		bh = jh2bh(jh);
		get_bh(bh);
		if (buffer_locked(bh)) {
			spin_unlock(&journal->j_list_lock);
			wait_on_buffer(bh);
			if (unlikely(!buffer_uptodate(bh)))
				err = -EIO;
			spin_lock(&journal->j_list_lock);
		}
		if (!inverted_lock(journal, bh)) {
			put_bh(bh);
			spin_lock(&journal->j_list_lock);
			continue;
		}
		if (buffer_jbd(bh) && jh->b_jlist == BJ_Locked) {
			__journal_unfile_buffer(jh);
			jbd_unlock_bh_state(bh);
			journal_remove_journal_head(bh);
			put_bh(bh);
		} else {
			jbd_unlock_bh_state(bh);
		}
		put_bh(bh);
		cond_resched_lock(&journal->j_list_lock);
	}
	spin_unlock(&journal->j_list_lock);

	if (err)
		__journal_abort_hard(journal);

	journal_write_revoke_records(journal, commit_transaction);

	jbd_debug(3, "JBD: commit phase 2\n");

	/*
	 * If we found any dirty or locked buffers, then we should have
	 * looped back up to the write_out_data label.  If there weren't
	 * any then journal_clean_data_list should have wiped the list
	 * clean by now, so check that it is in fact empty.
	 */
	J_ASSERT (commit_transaction->t_sync_datalist == NULL);

	jbd_debug (3, "JBD: commit phase 3\n");

	/*
	 * Way to go: we have now written out all of the data for a
	 * transaction!  Now comes the tricky part: we need to write out
	 * metadata.  Loop over the transaction's entire buffer list:
	 */
	commit_transaction->t_state = T_COMMIT;

	descriptor = NULL;
	bufs = 0;
	while (commit_transaction->t_buffers) {

		/* Find the next buffer to be journaled... */

		jh = commit_transaction->t_buffers;

		/* If we're in abort mode, we just un-journal the buffer and
		   release it for background writing. */

		if (is_journal_aborted(journal)) {
			JBUFFER_TRACE(jh, "journal is aborting: refile");
			journal_refile_buffer(journal, jh);
			/* If that was the last one, we need to clean up
			 * any descriptor buffers which may have been
			 * already allocated, even if we are now
			 * aborting. */
			if (!commit_transaction->t_buffers)
				goto start_journal_io;
			continue;
		}

		/* Make sure we have a descriptor block in which to
		   record the metadata buffer. */

		if (!descriptor) {
			struct buffer_head *bh;

			J_ASSERT (bufs == 0);

			jbd_debug(4, "JBD: get descriptor\n");

			descriptor = journal_get_descriptor_buffer(journal);
			if (!descriptor) {
				__journal_abort_hard(journal);
				continue;
			}

			bh = jh2bh(descriptor);
			jbd_debug(4, "JBD: got buffer %llu (%p)\n",
				(unsigned long long)bh->b_blocknr, bh->b_data);
			header = (journal_header_t *)&bh->b_data[0];
			header->h_magic     = cpu_to_be32(JFS_MAGIC_NUMBER);
			header->h_blocktype = cpu_to_be32(JFS_DESCRIPTOR_BLOCK);
			header->h_sequence  = cpu_to_be32(commit_transaction->t_tid);

			tagp = &bh->b_data[sizeof(journal_header_t)];
			space_left = bh->b_size - sizeof(journal_header_t);
			first_tag = 1;
			set_buffer_jwrite(bh);
			set_buffer_dirty(bh);
			wbuf[bufs++] = bh;

			/* Record it so that we can wait for IO
                           completion later */
			BUFFER_TRACE(bh, "ph3: file as descriptor");
			journal_file_buffer(descriptor, commit_transaction,
					BJ_LogCtl);
		}

		/* Where is the buffer to be written? */

		err = journal_next_log_block(journal, &blocknr);
		/* If the block mapping failed, just abandon the buffer
		   and repeat this loop: we'll fall into the
		   refile-on-abort condition above. */
		if (err) {
			__journal_abort_hard(journal);
			continue;
		}

		/*
		 * start_this_handle() uses t_outstanding_credits to determine
		 * the free space in the log, but this counter is changed
		 * by journal_next_log_block() also.
		 */
		commit_transaction->t_outstanding_credits--;

		/* Bump b_count to prevent truncate from stumbling over
                   the shadowed buffer!  @@@ This can go if we ever get
                   rid of the BJ_IO/BJ_Shadow pairing of buffers. */
		atomic_inc(&jh2bh(jh)->b_count);

		/* Make a temporary IO buffer with which to write it out
                   (this will requeue both the metadata buffer and the
                   temporary IO buffer). new_bh goes on BJ_IO*/

		set_bit(BH_JWrite, &jh2bh(jh)->b_state);
		/*
		 * akpm: journal_write_metadata_buffer() sets
		 * new_bh->b_transaction to commit_transaction.
		 * We need to clean this up before we release new_bh
		 * (which is of type BJ_IO)
		 */
		JBUFFER_TRACE(jh, "ph3: write metadata");
		flags = journal_write_metadata_buffer(commit_transaction,
						      jh, &new_jh, blocknr);
		set_bit(BH_JWrite, &jh2bh(new_jh)->b_state);
		wbuf[bufs++] = jh2bh(new_jh);

		/* Record the new block's tag in the current descriptor
                   buffer */

		tag_flag = 0;
		if (flags & 1)
			tag_flag |= JFS_FLAG_ESCAPE;
		if (!first_tag)
			tag_flag |= JFS_FLAG_SAME_UUID;

		tag = (journal_block_tag_t *) tagp;
		tag->t_blocknr = cpu_to_be32(jh2bh(jh)->b_blocknr);
		tag->t_flags = cpu_to_be32(tag_flag);
		tagp += sizeof(journal_block_tag_t);
		space_left -= sizeof(journal_block_tag_t);

		if (first_tag) {
			memcpy (tagp, journal->j_uuid, 16);
			tagp += 16;
			space_left -= 16;
			first_tag = 0;
		}

		/* If there's no more to do, or if the descriptor is full,
		   let the IO rip! */

		if (bufs == journal->j_wbufsize ||
		    commit_transaction->t_buffers == NULL ||
		    space_left < sizeof(journal_block_tag_t) + 16) {

			jbd_debug(4, "JBD: Submit %d IOs\n", bufs);

			/* Write an end-of-descriptor marker before
                           submitting the IOs.  "tag" still points to
                           the last tag we set up. */

			tag->t_flags |= cpu_to_be32(JFS_FLAG_LAST_TAG);

start_journal_io:
			for (i = 0; i < bufs; i++) {
				struct buffer_head *bh = wbuf[i];
				lock_buffer(bh);
				clear_buffer_dirty(bh);
				set_buffer_uptodate(bh);
				bh->b_end_io = journal_end_buffer_io_sync;
				submit_bh(WRITE, bh);
			}
			cond_resched();

			/* Force a new descriptor to be generated next
                           time round the loop. */
			descriptor = NULL;
			bufs = 0;
		}
	}

	/* Lo and behold: we have just managed to send a transaction to
           the log.  Before we can commit it, wait for the IO so far to
           complete.  Control buffers being written are on the
           transaction's t_log_list queue, and metadata buffers are on
           the t_iobuf_list queue.

	   Wait for the buffers in reverse order.  That way we are
	   less likely to be woken up until all IOs have completed, and
	   so we incur less scheduling load.
	*/

	jbd_debug(3, "JBD: commit phase 4\n");

	/*
	 * akpm: these are BJ_IO, and j_list_lock is not needed.
	 * See __journal_try_to_free_buffer.
	 */
wait_for_iobuf:
	while (commit_transaction->t_iobuf_list != NULL) {
		struct buffer_head *bh;

		jh = commit_transaction->t_iobuf_list->b_tprev;
		bh = jh2bh(jh);
		if (buffer_locked(bh)) {
			wait_on_buffer(bh);
			goto wait_for_iobuf;
		}
		if (cond_resched())
			goto wait_for_iobuf;

		if (unlikely(!buffer_uptodate(bh)))
			err = -EIO;

		clear_buffer_jwrite(bh);

		JBUFFER_TRACE(jh, "ph4: unfile after journal write");
		journal_unfile_buffer(journal, jh);

		/*
		 * ->t_iobuf_list should contain only dummy buffer_heads
		 * which were created by journal_write_metadata_buffer().
		 */
		BUFFER_TRACE(bh, "dumping temporary bh");
		journal_put_journal_head(jh);
		__brelse(bh);
		J_ASSERT_BH(bh, atomic_read(&bh->b_count) == 0);
		free_buffer_head(bh);

		/* We also have to unlock and free the corresponding
                   shadowed buffer */
		jh = commit_transaction->t_shadow_list->b_tprev;
		bh = jh2bh(jh);
		clear_bit(BH_JWrite, &bh->b_state);
		J_ASSERT_BH(bh, buffer_jbddirty(bh));

		/* The metadata is now released for reuse, but we need
                   to remember it against this transaction so that when
                   we finally commit, we can do any checkpointing
                   required. */
		JBUFFER_TRACE(jh, "file as BJ_Forget");
		journal_file_buffer(jh, commit_transaction, BJ_Forget);
		/* Wake up any transactions which were waiting for this
		   IO to complete */
		wake_up_bit(&bh->b_state, BH_Unshadow);
		JBUFFER_TRACE(jh, "brelse shadowed buffer");
		__brelse(bh);
	}

	J_ASSERT (commit_transaction->t_shadow_list == NULL);

	jbd_debug(3, "JBD: commit phase 5\n");

	/* Here we wait for the revoke record and descriptor record buffers */
 wait_for_ctlbuf:
	while (commit_transaction->t_log_list != NULL) {
		struct buffer_head *bh;

		jh = commit_transaction->t_log_list->b_tprev;
		bh = jh2bh(jh);
		if (buffer_locked(bh)) {
			wait_on_buffer(bh);
			goto wait_for_ctlbuf;
		}
		if (cond_resched())
			goto wait_for_ctlbuf;

		if (unlikely(!buffer_uptodate(bh)))
			err = -EIO;

		BUFFER_TRACE(bh, "ph5: control buffer writeout done: unfile");
		clear_buffer_jwrite(bh);
		journal_unfile_buffer(journal, jh);
		journal_put_journal_head(jh);
		__brelse(bh);		/* One for getblk */
		/* AKPM: bforget here */
	}

	jbd_debug(3, "JBD: commit phase 6\n");

	if (journal_write_commit_record(journal, commit_transaction))
		err = -EIO;

	if (err)
		__journal_abort_hard(journal);

	/* End of a transaction!  Finally, we can do checkpoint
           processing: any buffers committed as a result of this
           transaction can be removed from any checkpoint list it was on
           before. */

	jbd_debug(3, "JBD: commit phase 7\n");

	J_ASSERT(commit_transaction->t_sync_datalist == NULL);
	J_ASSERT(commit_transaction->t_buffers == NULL);
	J_ASSERT(commit_transaction->t_checkpoint_list == NULL);
	J_ASSERT(commit_transaction->t_iobuf_list == NULL);
	J_ASSERT(commit_transaction->t_shadow_list == NULL);
	J_ASSERT(commit_transaction->t_log_list == NULL);

restart_loop:
	/*
	 * As there are other places (journal_unmap_buffer()) adding buffers
	 * to this list we have to be careful and hold the j_list_lock.
	 */
	spin_lock(&journal->j_list_lock);
	while (commit_transaction->t_forget) {
		transaction_t *cp_transaction;
		struct buffer_head *bh;

		jh = commit_transaction->t_forget;
		spin_unlock(&journal->j_list_lock);
		bh = jh2bh(jh);
		jbd_lock_bh_state(bh);
		J_ASSERT_JH(jh,	jh->b_transaction == commit_transaction ||
			jh->b_transaction == journal->j_running_transaction);

		/*
		 * If there is undo-protected committed data against
		 * this buffer, then we can remove it now.  If it is a
		 * buffer needing such protection, the old frozen_data
		 * field now points to a committed version of the
		 * buffer, so rotate that field to the new committed
		 * data.
		 *
		 * Otherwise, we can just throw away the frozen data now.
		 */
		if (jh->b_committed_data) {
			kfree(jh->b_committed_data);
			jh->b_committed_data = NULL;
			if (jh->b_frozen_data) {
				jh->b_committed_data = jh->b_frozen_data;
				jh->b_frozen_data = NULL;
			}
		} else if (jh->b_frozen_data) {
			kfree(jh->b_frozen_data);
			jh->b_frozen_data = NULL;
		}

		spin_lock(&journal->j_list_lock);
		cp_transaction = jh->b_cp_transaction;
		if (cp_transaction) {
			JBUFFER_TRACE(jh, "remove from old cp transaction");
			__journal_remove_checkpoint(jh);
		}

		/* Only re-checkpoint the buffer_head if it is marked
		 * dirty.  If the buffer was added to the BJ_Forget list
		 * by journal_forget, it may no longer be dirty and
		 * there's no point in keeping a checkpoint record for
		 * it. */

		/* A buffer which has been freed while still being
		 * journaled by a previous transaction may end up still
		 * being dirty here, but we want to avoid writing back
		 * that buffer in the future now that the last use has
		 * been committed.  That's not only a performance gain,
		 * it also stops aliasing problems if the buffer is left
		 * behind for writeback and gets reallocated for another
		 * use in a different page. */
		if (buffer_freed(bh)) {
			clear_buffer_freed(bh);
			clear_buffer_jbddirty(bh);
		}

		if (buffer_jbddirty(bh)) {
			JBUFFER_TRACE(jh, "add to new checkpointing trans");
			__journal_insert_checkpoint(jh, commit_transaction);
			JBUFFER_TRACE(jh, "refile for checkpoint writeback");
			__journal_refile_buffer(jh);
			jbd_unlock_bh_state(bh);
		} else {
			J_ASSERT_BH(bh, !buffer_dirty(bh));
			J_ASSERT_JH(jh, jh->b_next_transaction == NULL);
			__journal_unfile_buffer(jh);
			jbd_unlock_bh_state(bh);
			journal_remove_journal_head(bh);  /* needs a brelse */
			release_buffer_page(bh);
		}
		cond_resched_lock(&journal->j_list_lock);
	}
	spin_unlock(&journal->j_list_lock);
	/*
	 * This is a bit sleazy.  We borrow j_list_lock to protect
	 * journal->j_committing_transaction in __journal_remove_checkpoint.
	 * Really, __journal_remove_checkpoint should be using j_state_lock but
	 * it's a bit hassle to hold that across __journal_remove_checkpoint
	 */
	spin_lock(&journal->j_state_lock);
	spin_lock(&journal->j_list_lock);
	/*
	 * Now recheck if some buffers did not get attached to the transaction
	 * while the lock was dropped...
	 */
	if (commit_transaction->t_forget) {
		spin_unlock(&journal->j_list_lock);
		spin_unlock(&journal->j_state_lock);
		goto restart_loop;
	}

	/* Done with this transaction! */

	jbd_debug(3, "JBD: commit phase 8\n");

	J_ASSERT(commit_transaction->t_state == T_COMMIT);

	commit_transaction->t_state = T_FINISHED;
	J_ASSERT(commit_transaction == journal->j_committing_transaction);
	journal->j_commit_sequence = commit_transaction->t_tid;
	journal->j_committing_transaction = NULL;
	spin_unlock(&journal->j_state_lock);

	if (commit_transaction->t_checkpoint_list == NULL) {
		__journal_drop_transaction(journal, commit_transaction);
	} else {
		if (journal->j_checkpoint_transactions == NULL) {
			journal->j_checkpoint_transactions = commit_transaction;
			commit_transaction->t_cpnext = commit_transaction;
			commit_transaction->t_cpprev = commit_transaction;
		} else {
			commit_transaction->t_cpnext =
				journal->j_checkpoint_transactions;
			commit_transaction->t_cpprev =
				commit_transaction->t_cpnext->t_cpprev;
			commit_transaction->t_cpnext->t_cpprev =
				commit_transaction;
			commit_transaction->t_cpprev->t_cpnext =
				commit_transaction;
		}
	}
	spin_unlock(&journal->j_list_lock);

	jbd_debug(1, "JBD: commit %d complete, head %d\n",
		  journal->j_commit_sequence, journal->j_tail_sequence);

	wake_up(&journal->j_wait_done_commit);
}
