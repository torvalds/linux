/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * journal.c
 *
 * Defines functions of journalling api
 *
 * Copyright (C) 2003, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/kthread.h>

#define MLOG_MASK_PREFIX ML_JOURNAL
#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "localalloc.h"
#include "namei.h"
#include "slot_map.h"
#include "super.h"
#include "vote.h"
#include "sysfile.h"

#include "buffer_head_io.h"

DEFINE_SPINLOCK(trans_inc_lock);

static int ocfs2_force_read_journal(struct inode *inode);
static int ocfs2_recover_node(struct ocfs2_super *osb,
			      int node_num);
static int __ocfs2_recovery_thread(void *arg);
static int ocfs2_commit_cache(struct ocfs2_super *osb);
static int ocfs2_wait_on_mount(struct ocfs2_super *osb);
static void ocfs2_handle_cleanup_locks(struct ocfs2_journal *journal,
				       struct ocfs2_journal_handle *handle);
static void ocfs2_commit_unstarted_handle(struct ocfs2_journal_handle *handle);
static int ocfs2_journal_toggle_dirty(struct ocfs2_super *osb,
				      int dirty);
static int ocfs2_trylock_journal(struct ocfs2_super *osb,
				 int slot_num);
static int ocfs2_recover_orphans(struct ocfs2_super *osb,
				 int slot);
static int ocfs2_commit_thread(void *arg);

static int ocfs2_commit_cache(struct ocfs2_super *osb)
{
	int status = 0;
	unsigned int flushed;
	unsigned long old_id;
	struct ocfs2_journal *journal = NULL;

	mlog_entry_void();

	journal = osb->journal;

	/* Flush all pending commits and checkpoint the journal. */
	down_write(&journal->j_trans_barrier);

	if (atomic_read(&journal->j_num_trans) == 0) {
		up_write(&journal->j_trans_barrier);
		mlog(0, "No transactions for me to flush!\n");
		goto finally;
	}

	journal_lock_updates(journal->j_journal);
	status = journal_flush(journal->j_journal);
	journal_unlock_updates(journal->j_journal);
	if (status < 0) {
		up_write(&journal->j_trans_barrier);
		mlog_errno(status);
		goto finally;
	}

	old_id = ocfs2_inc_trans_id(journal);

	flushed = atomic_read(&journal->j_num_trans);
	atomic_set(&journal->j_num_trans, 0);
	up_write(&journal->j_trans_barrier);

	mlog(0, "commit_thread: flushed transaction %lu (%u handles)\n",
	     journal->j_trans_id, flushed);

	ocfs2_kick_vote_thread(osb);
	wake_up(&journal->j_checkpointed);
finally:
	mlog_exit(status);
	return status;
}

struct ocfs2_journal_handle *ocfs2_alloc_handle(struct ocfs2_super *osb)
{
	struct ocfs2_journal_handle *retval = NULL;

	retval = kcalloc(1, sizeof(*retval), GFP_NOFS);
	if (!retval) {
		mlog(ML_ERROR, "Failed to allocate memory for journal "
		     "handle!\n");
		return NULL;
	}

	retval->num_locks = 0;
	retval->k_handle = NULL;

	INIT_LIST_HEAD(&retval->locks);
	INIT_LIST_HEAD(&retval->inode_list);
	retval->journal = osb->journal;

	return retval;
}

/* pass it NULL and it will allocate a new handle object for you.  If
 * you pass it a handle however, it may still return error, in which
 * case it has free'd the passed handle for you. */
struct ocfs2_journal_handle *ocfs2_start_trans(struct ocfs2_super *osb,
					       struct ocfs2_journal_handle *handle,
					       int max_buffs)
{
	int ret;
	journal_t *journal = osb->journal->j_journal;

	mlog_entry("(max_buffs = %d)\n", max_buffs);

	BUG_ON(!osb || !osb->journal->j_journal);

	if (ocfs2_is_hard_readonly(osb)) {
		ret = -EROFS;
		goto done_free;
	}

	BUG_ON(osb->journal->j_state == OCFS2_JOURNAL_FREE);
	BUG_ON(max_buffs <= 0);

	/* JBD might support this, but our journalling code doesn't yet. */
	if (journal_current_handle()) {
		mlog(ML_ERROR, "Recursive transaction attempted!\n");
		BUG();
	}

	if (!handle)
		handle = ocfs2_alloc_handle(osb);
	if (!handle) {
		ret = -ENOMEM;
		mlog(ML_ERROR, "Failed to allocate memory for journal "
		     "handle!\n");
		goto done_free;
	}

	down_read(&osb->journal->j_trans_barrier);

	/* actually start the transaction now */
	handle->k_handle = journal_start(journal, max_buffs);
	if (IS_ERR(handle->k_handle)) {
		up_read(&osb->journal->j_trans_barrier);

		ret = PTR_ERR(handle->k_handle);
		handle->k_handle = NULL;
		mlog_errno(ret);

		if (is_journal_aborted(journal)) {
			ocfs2_abort(osb->sb, "Detected aborted journal");
			ret = -EROFS;
		}
		goto done_free;
	}

	atomic_inc(&(osb->journal->j_num_trans));
	handle->flags |= OCFS2_HANDLE_STARTED;

	mlog_exit_ptr(handle);
	return handle;

done_free:
	if (handle)
		ocfs2_commit_unstarted_handle(handle); /* will kfree handle */

	mlog_exit(ret);
	return ERR_PTR(ret);
}

void ocfs2_handle_add_inode(struct ocfs2_journal_handle *handle,
			    struct inode *inode)
{
	BUG_ON(!handle);
	BUG_ON(!inode);

	atomic_inc(&inode->i_count);

	/* we're obviously changing it... */
	mutex_lock(&inode->i_mutex);

	/* sanity check */
	BUG_ON(OCFS2_I(inode)->ip_handle);
	BUG_ON(!list_empty(&OCFS2_I(inode)->ip_handle_list));

	OCFS2_I(inode)->ip_handle = handle;
	list_move_tail(&(OCFS2_I(inode)->ip_handle_list), &(handle->inode_list));
}

static void ocfs2_handle_unlock_inodes(struct ocfs2_journal_handle *handle)
{
	struct list_head *p, *n;
	struct inode *inode;
	struct ocfs2_inode_info *oi;

	list_for_each_safe(p, n, &handle->inode_list) {
		oi = list_entry(p, struct ocfs2_inode_info,
				ip_handle_list);
		inode = &oi->vfs_inode;

		OCFS2_I(inode)->ip_handle = NULL;
		list_del_init(&OCFS2_I(inode)->ip_handle_list);

		mutex_unlock(&inode->i_mutex);
		iput(inode);
	}
}

/* This is trivial so we do it out of the main commit
 * paths. Beware, it can be called from start_trans too! */
static void ocfs2_commit_unstarted_handle(struct ocfs2_journal_handle *handle)
{
	mlog_entry_void();

	BUG_ON(handle->flags & OCFS2_HANDLE_STARTED);

	ocfs2_handle_unlock_inodes(handle);
	/* You are allowed to add journal locks before the transaction
	 * has started. */
	ocfs2_handle_cleanup_locks(handle->journal, handle);

	kfree(handle);

	mlog_exit_void();
}

void ocfs2_commit_trans(struct ocfs2_journal_handle *handle)
{
	handle_t *jbd_handle;
	int retval;
	struct ocfs2_journal *journal = handle->journal;

	mlog_entry_void();

	BUG_ON(!handle);

	if (!(handle->flags & OCFS2_HANDLE_STARTED)) {
		ocfs2_commit_unstarted_handle(handle);
		mlog_exit_void();
		return;
	}

	/* release inode semaphores we took during this transaction */
	ocfs2_handle_unlock_inodes(handle);

	/* ocfs2_extend_trans may have had to call journal_restart
	 * which will always commit the transaction, but may return
	 * error for any number of reasons. If this is the case, we
	 * clear k_handle as it's not valid any more. */
	if (handle->k_handle) {
		jbd_handle = handle->k_handle;

		if (handle->flags & OCFS2_HANDLE_SYNC)
			jbd_handle->h_sync = 1;
		else
			jbd_handle->h_sync = 0;

		/* actually stop the transaction. if we've set h_sync,
		 * it'll have been committed when we return */
		retval = journal_stop(jbd_handle);
		if (retval < 0) {
			mlog_errno(retval);
			mlog(ML_ERROR, "Could not commit transaction\n");
			BUG();
		}

		handle->k_handle = NULL; /* it's been free'd in journal_stop */
	}

	ocfs2_handle_cleanup_locks(journal, handle);

	up_read(&journal->j_trans_barrier);

	kfree(handle);
	mlog_exit_void();
}

/*
 * 'nblocks' is what you want to add to the current
 * transaction. extend_trans will either extend the current handle by
 * nblocks, or commit it and start a new one with nblocks credits.
 *
 * WARNING: This will not release any semaphores or disk locks taken
 * during the transaction, so make sure they were taken *before*
 * start_trans or we'll have ordering deadlocks.
 *
 * WARNING2: Note that we do *not* drop j_trans_barrier here. This is
 * good because transaction ids haven't yet been recorded on the
 * cluster locks associated with this handle.
 */
int ocfs2_extend_trans(handle_t *handle, int nblocks)
{
	int status;

	BUG_ON(!handle);
	BUG_ON(!nblocks);

	mlog_entry_void();

	mlog(0, "Trying to extend transaction by %d blocks\n", nblocks);

	status = journal_extend(handle, nblocks);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	if (status > 0) {
		mlog(0, "journal_extend failed, trying journal_restart\n");
		status = journal_restart(handle, nblocks);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	status = 0;
bail:

	mlog_exit(status);
	return status;
}

int ocfs2_journal_access(struct ocfs2_journal_handle *handle,
			 struct inode *inode,
			 struct buffer_head *bh,
			 int type)
{
	int status;

	BUG_ON(!inode);
	BUG_ON(!handle);
	BUG_ON(!bh);
	BUG_ON(!(handle->flags & OCFS2_HANDLE_STARTED));

	mlog_entry("bh->b_blocknr=%llu, type=%d (\"%s\"), bh->b_size = %zu\n",
		   (unsigned long long)bh->b_blocknr, type,
		   (type == OCFS2_JOURNAL_ACCESS_CREATE) ?
		   "OCFS2_JOURNAL_ACCESS_CREATE" :
		   "OCFS2_JOURNAL_ACCESS_WRITE",
		   bh->b_size);

	/* we can safely remove this assertion after testing. */
	if (!buffer_uptodate(bh)) {
		mlog(ML_ERROR, "giving me a buffer that's not uptodate!\n");
		mlog(ML_ERROR, "b_blocknr=%llu\n",
		     (unsigned long long)bh->b_blocknr);
		BUG();
	}

	/* Set the current transaction information on the inode so
	 * that the locking code knows whether it can drop it's locks
	 * on this inode or not. We're protected from the commit
	 * thread updating the current transaction id until
	 * ocfs2_commit_trans() because ocfs2_start_trans() took
	 * j_trans_barrier for us. */
	ocfs2_set_inode_lock_trans(OCFS2_SB(inode->i_sb)->journal, inode);

	mutex_lock(&OCFS2_I(inode)->ip_io_mutex);
	switch (type) {
	case OCFS2_JOURNAL_ACCESS_CREATE:
	case OCFS2_JOURNAL_ACCESS_WRITE:
		status = journal_get_write_access(handle->k_handle, bh);
		break;

	case OCFS2_JOURNAL_ACCESS_UNDO:
		status = journal_get_undo_access(handle->k_handle, bh);
		break;

	default:
		status = -EINVAL;
		mlog(ML_ERROR, "Uknown access type!\n");
	}
	mutex_unlock(&OCFS2_I(inode)->ip_io_mutex);

	if (status < 0)
		mlog(ML_ERROR, "Error %d getting %d access to buffer!\n",
		     status, type);

	mlog_exit(status);
	return status;
}

int ocfs2_journal_dirty(struct ocfs2_journal_handle *handle,
			struct buffer_head *bh)
{
	int status;

	BUG_ON(!(handle->flags & OCFS2_HANDLE_STARTED));

	mlog_entry("(bh->b_blocknr=%llu)\n",
		   (unsigned long long)bh->b_blocknr);

	status = journal_dirty_metadata(handle->k_handle, bh);
	if (status < 0)
		mlog(ML_ERROR, "Could not dirty metadata buffer. "
		     "(bh->b_blocknr=%llu)\n",
		     (unsigned long long)bh->b_blocknr);

	mlog_exit(status);
	return status;
}

int ocfs2_journal_dirty_data(handle_t *handle,
			     struct buffer_head *bh)
{
	int err = journal_dirty_data(handle, bh);
	if (err)
		mlog_errno(err);
	/* TODO: When we can handle it, abort the handle and go RO on
	 * error here. */

	return err;
}

/* We always assume you're adding a metadata lock at level 'ex' */
int ocfs2_handle_add_lock(struct ocfs2_journal_handle *handle,
			  struct inode *inode)
{
	int status;
	struct ocfs2_journal_lock *lock;

	BUG_ON(!inode);

	lock = kmem_cache_alloc(ocfs2_lock_cache, GFP_NOFS);
	if (!lock) {
		status = -ENOMEM;
		mlog_errno(-ENOMEM);
		goto bail;
	}

	if (!igrab(inode))
		BUG();
	lock->jl_inode = inode;

	list_add_tail(&(lock->jl_lock_list), &(handle->locks));
	handle->num_locks++;

	status = 0;
bail:
	mlog_exit(status);
	return status;
}

static void ocfs2_handle_cleanup_locks(struct ocfs2_journal *journal,
				       struct ocfs2_journal_handle *handle)
{
	struct list_head *p, *n;
	struct ocfs2_journal_lock *lock;
	struct inode *inode;

	list_for_each_safe(p, n, &(handle->locks)) {
		lock = list_entry(p, struct ocfs2_journal_lock,
				  jl_lock_list);
		list_del(&lock->jl_lock_list);
		handle->num_locks--;

		inode = lock->jl_inode;
		ocfs2_meta_unlock(inode, 1);
		if (atomic_read(&inode->i_count) == 1)
			mlog(ML_ERROR,
			     "Inode %llu, I'm doing a last iput for!",
			     (unsigned long long)OCFS2_I(inode)->ip_blkno);
		iput(inode);
		kmem_cache_free(ocfs2_lock_cache, lock);
	}
}

#define OCFS2_DEFAULT_COMMIT_INTERVAL 	(HZ * 5)

void ocfs2_set_journal_params(struct ocfs2_super *osb)
{
	journal_t *journal = osb->journal->j_journal;

	spin_lock(&journal->j_state_lock);
	journal->j_commit_interval = OCFS2_DEFAULT_COMMIT_INTERVAL;
	if (osb->s_mount_opt & OCFS2_MOUNT_BARRIER)
		journal->j_flags |= JFS_BARRIER;
	else
		journal->j_flags &= ~JFS_BARRIER;
	spin_unlock(&journal->j_state_lock);
}

int ocfs2_journal_init(struct ocfs2_journal *journal, int *dirty)
{
	int status = -1;
	struct inode *inode = NULL; /* the journal inode */
	journal_t *j_journal = NULL;
	struct ocfs2_dinode *di = NULL;
	struct buffer_head *bh = NULL;
	struct ocfs2_super *osb;
	int meta_lock = 0;

	mlog_entry_void();

	BUG_ON(!journal);

	osb = journal->j_osb;

	/* already have the inode for our journal */
	inode = ocfs2_get_system_file_inode(osb, JOURNAL_SYSTEM_INODE,
					    osb->slot_num);
	if (inode == NULL) {
		status = -EACCES;
		mlog_errno(status);
		goto done;
	}
	if (is_bad_inode(inode)) {
		mlog(ML_ERROR, "access error (bad inode)\n");
		iput(inode);
		inode = NULL;
		status = -EACCES;
		goto done;
	}

	SET_INODE_JOURNAL(inode);
	OCFS2_I(inode)->ip_open_count++;

	/* Skip recovery waits here - journal inode metadata never
	 * changes in a live cluster so it can be considered an
	 * exception to the rule. */
	status = ocfs2_meta_lock_full(inode, NULL, &bh, 1,
				      OCFS2_META_LOCK_RECOVERY);
	if (status < 0) {
		if (status != -ERESTARTSYS)
			mlog(ML_ERROR, "Could not get lock on journal!\n");
		goto done;
	}

	meta_lock = 1;
	di = (struct ocfs2_dinode *)bh->b_data;

	if (inode->i_size <  OCFS2_MIN_JOURNAL_SIZE) {
		mlog(ML_ERROR, "Journal file size (%lld) is too small!\n",
		     inode->i_size);
		status = -EINVAL;
		goto done;
	}

	mlog(0, "inode->i_size = %lld\n", inode->i_size);
	mlog(0, "inode->i_blocks = %llu\n",
			(unsigned long long)inode->i_blocks);
	mlog(0, "inode->ip_clusters = %u\n", OCFS2_I(inode)->ip_clusters);

	/* call the kernels journal init function now */
	j_journal = journal_init_inode(inode);
	if (j_journal == NULL) {
		mlog(ML_ERROR, "Linux journal layer error\n");
		status = -EINVAL;
		goto done;
	}

	mlog(0, "Returned from journal_init_inode\n");
	mlog(0, "j_journal->j_maxlen = %u\n", j_journal->j_maxlen);

	*dirty = (le32_to_cpu(di->id1.journal1.ij_flags) &
		  OCFS2_JOURNAL_DIRTY_FL);

	journal->j_journal = j_journal;
	journal->j_inode = inode;
	journal->j_bh = bh;

	ocfs2_set_journal_params(osb);

	journal->j_state = OCFS2_JOURNAL_LOADED;

	status = 0;
done:
	if (status < 0) {
		if (meta_lock)
			ocfs2_meta_unlock(inode, 1);
		if (bh != NULL)
			brelse(bh);
		if (inode) {
			OCFS2_I(inode)->ip_open_count--;
			iput(inode);
		}
	}

	mlog_exit(status);
	return status;
}

static int ocfs2_journal_toggle_dirty(struct ocfs2_super *osb,
				      int dirty)
{
	int status;
	unsigned int flags;
	struct ocfs2_journal *journal = osb->journal;
	struct buffer_head *bh = journal->j_bh;
	struct ocfs2_dinode *fe;

	mlog_entry_void();

	fe = (struct ocfs2_dinode *)bh->b_data;
	if (!OCFS2_IS_VALID_DINODE(fe)) {
		/* This is called from startup/shutdown which will
		 * handle the errors in a specific manner, so no need
		 * to call ocfs2_error() here. */
		mlog(ML_ERROR, "Journal dinode %llu  has invalid "
		     "signature: %.*s", (unsigned long long)fe->i_blkno, 7,
		     fe->i_signature);
		status = -EIO;
		goto out;
	}

	flags = le32_to_cpu(fe->id1.journal1.ij_flags);
	if (dirty)
		flags |= OCFS2_JOURNAL_DIRTY_FL;
	else
		flags &= ~OCFS2_JOURNAL_DIRTY_FL;
	fe->id1.journal1.ij_flags = cpu_to_le32(flags);

	status = ocfs2_write_block(osb, bh, journal->j_inode);
	if (status < 0)
		mlog_errno(status);

out:
	mlog_exit(status);
	return status;
}

/*
 * If the journal has been kmalloc'd it needs to be freed after this
 * call.
 */
void ocfs2_journal_shutdown(struct ocfs2_super *osb)
{
	struct ocfs2_journal *journal = NULL;
	int status = 0;
	struct inode *inode = NULL;
	int num_running_trans = 0;

	mlog_entry_void();

	BUG_ON(!osb);

	journal = osb->journal;
	if (!journal)
		goto done;

	inode = journal->j_inode;

	if (journal->j_state != OCFS2_JOURNAL_LOADED)
		goto done;

	/* need to inc inode use count as journal_destroy will iput. */
	if (!igrab(inode))
		BUG();

	num_running_trans = atomic_read(&(osb->journal->j_num_trans));
	if (num_running_trans > 0)
		mlog(0, "Shutting down journal: must wait on %d "
		     "running transactions!\n",
		     num_running_trans);

	/* Do a commit_cache here. It will flush our journal, *and*
	 * release any locks that are still held.
	 * set the SHUTDOWN flag and release the trans lock.
	 * the commit thread will take the trans lock for us below. */
	journal->j_state = OCFS2_JOURNAL_IN_SHUTDOWN;

	/* The OCFS2_JOURNAL_IN_SHUTDOWN will signal to commit_cache to not
	 * drop the trans_lock (which we want to hold until we
	 * completely destroy the journal. */
	if (osb->commit_task) {
		/* Wait for the commit thread */
		mlog(0, "Waiting for ocfs2commit to exit....\n");
		kthread_stop(osb->commit_task);
		osb->commit_task = NULL;
	}

	BUG_ON(atomic_read(&(osb->journal->j_num_trans)) != 0);

	status = ocfs2_journal_toggle_dirty(osb, 0);
	if (status < 0)
		mlog_errno(status);

	/* Shutdown the kernel journal system */
	journal_destroy(journal->j_journal);

	OCFS2_I(inode)->ip_open_count--;

	/* unlock our journal */
	ocfs2_meta_unlock(inode, 1);

	brelse(journal->j_bh);
	journal->j_bh = NULL;

	journal->j_state = OCFS2_JOURNAL_FREE;

//	up_write(&journal->j_trans_barrier);
done:
	if (inode)
		iput(inode);
	mlog_exit_void();
}

static void ocfs2_clear_journal_error(struct super_block *sb,
				      journal_t *journal,
				      int slot)
{
	int olderr;

	olderr = journal_errno(journal);
	if (olderr) {
		mlog(ML_ERROR, "File system error %d recorded in "
		     "journal %u.\n", olderr, slot);
		mlog(ML_ERROR, "File system on device %s needs checking.\n",
		     sb->s_id);

		journal_ack_err(journal);
		journal_clear_err(journal);
	}
}

int ocfs2_journal_load(struct ocfs2_journal *journal)
{
	int status = 0;
	struct ocfs2_super *osb;

	mlog_entry_void();

	if (!journal)
		BUG();

	osb = journal->j_osb;

	status = journal_load(journal->j_journal);
	if (status < 0) {
		mlog(ML_ERROR, "Failed to load journal!\n");
		goto done;
	}

	ocfs2_clear_journal_error(osb->sb, journal->j_journal, osb->slot_num);

	status = ocfs2_journal_toggle_dirty(osb, 1);
	if (status < 0) {
		mlog_errno(status);
		goto done;
	}

	/* Launch the commit thread */
	osb->commit_task = kthread_run(ocfs2_commit_thread, osb, "ocfs2cmt");
	if (IS_ERR(osb->commit_task)) {
		status = PTR_ERR(osb->commit_task);
		osb->commit_task = NULL;
		mlog(ML_ERROR, "unable to launch ocfs2commit thread, error=%d",
		     status);
		goto done;
	}

done:
	mlog_exit(status);
	return status;
}


/* 'full' flag tells us whether we clear out all blocks or if we just
 * mark the journal clean */
int ocfs2_journal_wipe(struct ocfs2_journal *journal, int full)
{
	int status;

	mlog_entry_void();

	BUG_ON(!journal);

	status = journal_wipe(journal->j_journal, full);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_journal_toggle_dirty(journal->j_osb, 0);
	if (status < 0)
		mlog_errno(status);

bail:
	mlog_exit(status);
	return status;
}

/*
 * JBD Might read a cached version of another nodes journal file. We
 * don't want this as this file changes often and we get no
 * notification on those changes. The only way to be sure that we've
 * got the most up to date version of those blocks then is to force
 * read them off disk. Just searching through the buffer cache won't
 * work as there may be pages backing this file which are still marked
 * up to date. We know things can't change on this file underneath us
 * as we have the lock by now :)
 */
static int ocfs2_force_read_journal(struct inode *inode)
{
	int status = 0;
	int i, p_blocks;
	u64 v_blkno, p_blkno;
#define CONCURRENT_JOURNAL_FILL 32
	struct buffer_head *bhs[CONCURRENT_JOURNAL_FILL];

	mlog_entry_void();

	BUG_ON(inode->i_blocks !=
		     ocfs2_align_bytes_to_sectors(i_size_read(inode)));

	memset(bhs, 0, sizeof(struct buffer_head *) * CONCURRENT_JOURNAL_FILL);

	mlog(0, "Force reading %llu blocks\n",
		(unsigned long long)(inode->i_blocks >>
			(inode->i_sb->s_blocksize_bits - 9)));

	v_blkno = 0;
	while (v_blkno <
	       (inode->i_blocks >> (inode->i_sb->s_blocksize_bits - 9))) {

		status = ocfs2_extent_map_get_blocks(inode, v_blkno,
						     1, &p_blkno,
						     &p_blocks);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		if (p_blocks > CONCURRENT_JOURNAL_FILL)
			p_blocks = CONCURRENT_JOURNAL_FILL;

		/* We are reading journal data which should not
		 * be put in the uptodate cache */
		status = ocfs2_read_blocks(OCFS2_SB(inode->i_sb),
					   p_blkno, p_blocks, bhs, 0,
					   NULL);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		for(i = 0; i < p_blocks; i++) {
			brelse(bhs[i]);
			bhs[i] = NULL;
		}

		v_blkno += p_blocks;
	}

bail:
	for(i = 0; i < CONCURRENT_JOURNAL_FILL; i++)
		if (bhs[i])
			brelse(bhs[i]);
	mlog_exit(status);
	return status;
}

struct ocfs2_la_recovery_item {
	struct list_head	lri_list;
	int			lri_slot;
	struct ocfs2_dinode	*lri_la_dinode;
	struct ocfs2_dinode	*lri_tl_dinode;
};

/* Does the second half of the recovery process. By this point, the
 * node is marked clean and can actually be considered recovered,
 * hence it's no longer in the recovery map, but there's still some
 * cleanup we can do which shouldn't happen within the recovery thread
 * as locking in that context becomes very difficult if we are to take
 * recovering nodes into account.
 *
 * NOTE: This function can and will sleep on recovery of other nodes
 * during cluster locking, just like any other ocfs2 process.
 */
void ocfs2_complete_recovery(void *data)
{
	int ret;
	struct ocfs2_super *osb = data;
	struct ocfs2_journal *journal = osb->journal;
	struct ocfs2_dinode *la_dinode, *tl_dinode;
	struct ocfs2_la_recovery_item *item;
	struct list_head *p, *n;
	LIST_HEAD(tmp_la_list);

	mlog_entry_void();

	mlog(0, "completing recovery from keventd\n");

	spin_lock(&journal->j_lock);
	list_splice_init(&journal->j_la_cleanups, &tmp_la_list);
	spin_unlock(&journal->j_lock);

	list_for_each_safe(p, n, &tmp_la_list) {
		item = list_entry(p, struct ocfs2_la_recovery_item, lri_list);
		list_del_init(&item->lri_list);

		mlog(0, "Complete recovery for slot %d\n", item->lri_slot);

		la_dinode = item->lri_la_dinode;
		if (la_dinode) {
			mlog(0, "Clean up local alloc %llu\n",
			     (unsigned long long)la_dinode->i_blkno);

			ret = ocfs2_complete_local_alloc_recovery(osb,
								  la_dinode);
			if (ret < 0)
				mlog_errno(ret);

			kfree(la_dinode);
		}

		tl_dinode = item->lri_tl_dinode;
		if (tl_dinode) {
			mlog(0, "Clean up truncate log %llu\n",
			     (unsigned long long)tl_dinode->i_blkno);

			ret = ocfs2_complete_truncate_log_recovery(osb,
								   tl_dinode);
			if (ret < 0)
				mlog_errno(ret);

			kfree(tl_dinode);
		}

		ret = ocfs2_recover_orphans(osb, item->lri_slot);
		if (ret < 0)
			mlog_errno(ret);

		kfree(item);
	}

	mlog(0, "Recovery completion\n");
	mlog_exit_void();
}

/* NOTE: This function always eats your references to la_dinode and
 * tl_dinode, either manually on error, or by passing them to
 * ocfs2_complete_recovery */
static void ocfs2_queue_recovery_completion(struct ocfs2_journal *journal,
					    int slot_num,
					    struct ocfs2_dinode *la_dinode,
					    struct ocfs2_dinode *tl_dinode)
{
	struct ocfs2_la_recovery_item *item;

	item = kmalloc(sizeof(struct ocfs2_la_recovery_item), GFP_NOFS);
	if (!item) {
		/* Though we wish to avoid it, we are in fact safe in
		 * skipping local alloc cleanup as fsck.ocfs2 is more
		 * than capable of reclaiming unused space. */
		if (la_dinode)
			kfree(la_dinode);

		if (tl_dinode)
			kfree(tl_dinode);

		mlog_errno(-ENOMEM);
		return;
	}

	INIT_LIST_HEAD(&item->lri_list);
	item->lri_la_dinode = la_dinode;
	item->lri_slot = slot_num;
	item->lri_tl_dinode = tl_dinode;

	spin_lock(&journal->j_lock);
	list_add_tail(&item->lri_list, &journal->j_la_cleanups);
	queue_work(ocfs2_wq, &journal->j_recovery_work);
	spin_unlock(&journal->j_lock);
}

/* Called by the mount code to queue recovery the last part of
 * recovery for it's own slot. */
void ocfs2_complete_mount_recovery(struct ocfs2_super *osb)
{
	struct ocfs2_journal *journal = osb->journal;

	if (osb->dirty) {
		/* No need to queue up our truncate_log as regular
		 * cleanup will catch that. */
		ocfs2_queue_recovery_completion(journal,
						osb->slot_num,
						osb->local_alloc_copy,
						NULL);
		ocfs2_schedule_truncate_log_flush(osb, 0);

		osb->local_alloc_copy = NULL;
		osb->dirty = 0;
	}
}

static int __ocfs2_recovery_thread(void *arg)
{
	int status, node_num;
	struct ocfs2_super *osb = arg;

	mlog_entry_void();

	status = ocfs2_wait_on_mount(osb);
	if (status < 0) {
		goto bail;
	}

restart:
	status = ocfs2_super_lock(osb, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	while(!ocfs2_node_map_is_empty(osb, &osb->recovery_map)) {
		node_num = ocfs2_node_map_first_set_bit(osb,
							&osb->recovery_map);
		if (node_num == O2NM_INVALID_NODE_NUM) {
			mlog(0, "Out of nodes to recover.\n");
			break;
		}

		status = ocfs2_recover_node(osb, node_num);
		if (status < 0) {
			mlog(ML_ERROR,
			     "Error %d recovering node %d on device (%u,%u)!\n",
			     status, node_num,
			     MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));
			mlog(ML_ERROR, "Volume requires unmount.\n");
			continue;
		}

		ocfs2_recovery_map_clear(osb, node_num);
	}
	ocfs2_super_unlock(osb, 1);

	/* We always run recovery on our own orphan dir - the dead
	 * node(s) may have voted "no" on an inode delete earlier. A
	 * revote is therefore required. */
	ocfs2_queue_recovery_completion(osb->journal, osb->slot_num, NULL,
					NULL);

bail:
	mutex_lock(&osb->recovery_lock);
	if (!status &&
	    !ocfs2_node_map_is_empty(osb, &osb->recovery_map)) {
		mutex_unlock(&osb->recovery_lock);
		goto restart;
	}

	osb->recovery_thread_task = NULL;
	mb(); /* sync with ocfs2_recovery_thread_running */
	wake_up(&osb->recovery_event);

	mutex_unlock(&osb->recovery_lock);

	mlog_exit(status);
	/* no one is callint kthread_stop() for us so the kthread() api
	 * requires that we call do_exit().  And it isn't exported, but
	 * complete_and_exit() seems to be a minimal wrapper around it. */
	complete_and_exit(NULL, status);
	return status;
}

void ocfs2_recovery_thread(struct ocfs2_super *osb, int node_num)
{
	mlog_entry("(node_num=%d, osb->node_num = %d)\n",
		   node_num, osb->node_num);

	mutex_lock(&osb->recovery_lock);
	if (osb->disable_recovery)
		goto out;

	/* People waiting on recovery will wait on
	 * the recovery map to empty. */
	if (!ocfs2_recovery_map_set(osb, node_num))
		mlog(0, "node %d already be in recovery.\n", node_num);

	mlog(0, "starting recovery thread...\n");

	if (osb->recovery_thread_task)
		goto out;

	osb->recovery_thread_task =  kthread_run(__ocfs2_recovery_thread, osb,
						 "ocfs2rec");
	if (IS_ERR(osb->recovery_thread_task)) {
		mlog_errno((int)PTR_ERR(osb->recovery_thread_task));
		osb->recovery_thread_task = NULL;
	}

out:
	mutex_unlock(&osb->recovery_lock);
	wake_up(&osb->recovery_event);

	mlog_exit_void();
}

/* Does the actual journal replay and marks the journal inode as
 * clean. Will only replay if the journal inode is marked dirty. */
static int ocfs2_replay_journal(struct ocfs2_super *osb,
				int node_num,
				int slot_num)
{
	int status;
	int got_lock = 0;
	unsigned int flags;
	struct inode *inode = NULL;
	struct ocfs2_dinode *fe;
	journal_t *journal = NULL;
	struct buffer_head *bh = NULL;

	inode = ocfs2_get_system_file_inode(osb, JOURNAL_SYSTEM_INODE,
					    slot_num);
	if (inode == NULL) {
		status = -EACCES;
		mlog_errno(status);
		goto done;
	}
	if (is_bad_inode(inode)) {
		status = -EACCES;
		iput(inode);
		inode = NULL;
		mlog_errno(status);
		goto done;
	}
	SET_INODE_JOURNAL(inode);

	status = ocfs2_meta_lock_full(inode, NULL, &bh, 1,
				      OCFS2_META_LOCK_RECOVERY);
	if (status < 0) {
		mlog(0, "status returned from ocfs2_meta_lock=%d\n", status);
		if (status != -ERESTARTSYS)
			mlog(ML_ERROR, "Could not lock journal!\n");
		goto done;
	}
	got_lock = 1;

	fe = (struct ocfs2_dinode *) bh->b_data;

	flags = le32_to_cpu(fe->id1.journal1.ij_flags);

	if (!(flags & OCFS2_JOURNAL_DIRTY_FL)) {
		mlog(0, "No recovery required for node %d\n", node_num);
		goto done;
	}

	mlog(ML_NOTICE, "Recovering node %d from slot %d on device (%u,%u)\n",
	     node_num, slot_num,
	     MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));

	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters);

	status = ocfs2_force_read_journal(inode);
	if (status < 0) {
		mlog_errno(status);
		goto done;
	}

	mlog(0, "calling journal_init_inode\n");
	journal = journal_init_inode(inode);
	if (journal == NULL) {
		mlog(ML_ERROR, "Linux journal layer error\n");
		status = -EIO;
		goto done;
	}

	status = journal_load(journal);
	if (status < 0) {
		mlog_errno(status);
		if (!igrab(inode))
			BUG();
		journal_destroy(journal);
		goto done;
	}

	ocfs2_clear_journal_error(osb->sb, journal, slot_num);

	/* wipe the journal */
	mlog(0, "flushing the journal.\n");
	journal_lock_updates(journal);
	status = journal_flush(journal);
	journal_unlock_updates(journal);
	if (status < 0)
		mlog_errno(status);

	/* This will mark the node clean */
	flags = le32_to_cpu(fe->id1.journal1.ij_flags);
	flags &= ~OCFS2_JOURNAL_DIRTY_FL;
	fe->id1.journal1.ij_flags = cpu_to_le32(flags);

	status = ocfs2_write_block(osb, bh, inode);
	if (status < 0)
		mlog_errno(status);

	if (!igrab(inode))
		BUG();

	journal_destroy(journal);

done:
	/* drop the lock on this nodes journal */
	if (got_lock)
		ocfs2_meta_unlock(inode, 1);

	if (inode)
		iput(inode);

	if (bh)
		brelse(bh);

	mlog_exit(status);
	return status;
}

/*
 * Do the most important parts of node recovery:
 *  - Replay it's journal
 *  - Stamp a clean local allocator file
 *  - Stamp a clean truncate log
 *  - Mark the node clean
 *
 * If this function completes without error, a node in OCFS2 can be
 * said to have been safely recovered. As a result, failure during the
 * second part of a nodes recovery process (local alloc recovery) is
 * far less concerning.
 */
static int ocfs2_recover_node(struct ocfs2_super *osb,
			      int node_num)
{
	int status = 0;
	int slot_num;
	struct ocfs2_slot_info *si = osb->slot_info;
	struct ocfs2_dinode *la_copy = NULL;
	struct ocfs2_dinode *tl_copy = NULL;

	mlog_entry("(node_num=%d, osb->node_num = %d)\n",
		   node_num, osb->node_num);

	mlog(0, "checking node %d\n", node_num);

	/* Should not ever be called to recover ourselves -- in that
	 * case we should've called ocfs2_journal_load instead. */
	BUG_ON(osb->node_num == node_num);

	slot_num = ocfs2_node_num_to_slot(si, node_num);
	if (slot_num == OCFS2_INVALID_SLOT) {
		status = 0;
		mlog(0, "no slot for this node, so no recovery required.\n");
		goto done;
	}

	mlog(0, "node %d was using slot %d\n", node_num, slot_num);

	status = ocfs2_replay_journal(osb, node_num, slot_num);
	if (status < 0) {
		mlog_errno(status);
		goto done;
	}

	/* Stamp a clean local alloc file AFTER recovering the journal... */
	status = ocfs2_begin_local_alloc_recovery(osb, slot_num, &la_copy);
	if (status < 0) {
		mlog_errno(status);
		goto done;
	}

	/* An error from begin_truncate_log_recovery is not
	 * serious enough to warrant halting the rest of
	 * recovery. */
	status = ocfs2_begin_truncate_log_recovery(osb, slot_num, &tl_copy);
	if (status < 0)
		mlog_errno(status);

	/* Likewise, this would be a strange but ultimately not so
	 * harmful place to get an error... */
	ocfs2_clear_slot(si, slot_num);
	status = ocfs2_update_disk_slots(osb, si);
	if (status < 0)
		mlog_errno(status);

	/* This will kfree the memory pointed to by la_copy and tl_copy */
	ocfs2_queue_recovery_completion(osb->journal, slot_num, la_copy,
					tl_copy);

	status = 0;
done:

	mlog_exit(status);
	return status;
}

/* Test node liveness by trylocking his journal. If we get the lock,
 * we drop it here. Return 0 if we got the lock, -EAGAIN if node is
 * still alive (we couldn't get the lock) and < 0 on error. */
static int ocfs2_trylock_journal(struct ocfs2_super *osb,
				 int slot_num)
{
	int status, flags;
	struct inode *inode = NULL;

	inode = ocfs2_get_system_file_inode(osb, JOURNAL_SYSTEM_INODE,
					    slot_num);
	if (inode == NULL) {
		mlog(ML_ERROR, "access error\n");
		status = -EACCES;
		goto bail;
	}
	if (is_bad_inode(inode)) {
		mlog(ML_ERROR, "access error (bad inode)\n");
		iput(inode);
		inode = NULL;
		status = -EACCES;
		goto bail;
	}
	SET_INODE_JOURNAL(inode);

	flags = OCFS2_META_LOCK_RECOVERY | OCFS2_META_LOCK_NOQUEUE;
	status = ocfs2_meta_lock_full(inode, NULL, NULL, 1, flags);
	if (status < 0) {
		if (status != -EAGAIN)
			mlog_errno(status);
		goto bail;
	}

	ocfs2_meta_unlock(inode, 1);
bail:
	if (inode)
		iput(inode);

	return status;
}

/* Call this underneath ocfs2_super_lock. It also assumes that the
 * slot info struct has been updated from disk. */
int ocfs2_mark_dead_nodes(struct ocfs2_super *osb)
{
	int status, i, node_num;
	struct ocfs2_slot_info *si = osb->slot_info;

	/* This is called with the super block cluster lock, so we
	 * know that the slot map can't change underneath us. */

	spin_lock(&si->si_lock);
	for(i = 0; i < si->si_num_slots; i++) {
		if (i == osb->slot_num)
			continue;
		if (ocfs2_is_empty_slot(si, i))
			continue;

		node_num = si->si_global_node_nums[i];
		if (ocfs2_node_map_test_bit(osb, &osb->recovery_map, node_num))
			continue;
		spin_unlock(&si->si_lock);

		/* Ok, we have a slot occupied by another node which
		 * is not in the recovery map. We trylock his journal
		 * file here to test if he's alive. */
		status = ocfs2_trylock_journal(osb, i);
		if (!status) {
			/* Since we're called from mount, we know that
			 * the recovery thread can't race us on
			 * setting / checking the recovery bits. */
			ocfs2_recovery_thread(osb, node_num);
		} else if ((status < 0) && (status != -EAGAIN)) {
			mlog_errno(status);
			goto bail;
		}

		spin_lock(&si->si_lock);
	}
	spin_unlock(&si->si_lock);

	status = 0;
bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_queue_orphans(struct ocfs2_super *osb,
			       int slot,
			       struct inode **head)
{
	int status;
	struct inode *orphan_dir_inode = NULL;
	struct inode *iter;
	unsigned long offset, blk, local;
	struct buffer_head *bh = NULL;
	struct ocfs2_dir_entry *de;
	struct super_block *sb = osb->sb;

	orphan_dir_inode = ocfs2_get_system_file_inode(osb,
						       ORPHAN_DIR_SYSTEM_INODE,
						       slot);
	if  (!orphan_dir_inode) {
		status = -ENOENT;
		mlog_errno(status);
		return status;
	}	

	mutex_lock(&orphan_dir_inode->i_mutex);
	status = ocfs2_meta_lock(orphan_dir_inode, NULL, NULL, 0);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	offset = 0;
	iter = NULL;
	while(offset < i_size_read(orphan_dir_inode)) {
		blk = offset >> sb->s_blocksize_bits;

		bh = ocfs2_bread(orphan_dir_inode, blk, &status, 0);
		if (!bh)
			status = -EINVAL;
		if (status < 0) {
			if (bh)
				brelse(bh);
			mlog_errno(status);
			goto out_unlock;
		}

		local = 0;
		while(offset < i_size_read(orphan_dir_inode)
		      && local < sb->s_blocksize) {
			de = (struct ocfs2_dir_entry *) (bh->b_data + local);

			if (!ocfs2_check_dir_entry(orphan_dir_inode,
						  de, bh, local)) {
				status = -EINVAL;
				mlog_errno(status);
				brelse(bh);
				goto out_unlock;
			}

			local += le16_to_cpu(de->rec_len);
			offset += le16_to_cpu(de->rec_len);

			/* I guess we silently fail on no inode? */
			if (!le64_to_cpu(de->inode))
				continue;
			if (de->file_type > OCFS2_FT_MAX) {
				mlog(ML_ERROR,
				     "block %llu contains invalid de: "
				     "inode = %llu, rec_len = %u, "
				     "name_len = %u, file_type = %u, "
				     "name='%.*s'\n",
				     (unsigned long long)bh->b_blocknr,
				     (unsigned long long)le64_to_cpu(de->inode),
				     le16_to_cpu(de->rec_len),
				     de->name_len,
				     de->file_type,
				     de->name_len,
				     de->name);
				continue;
			}
			if (de->name_len == 1 && !strncmp(".", de->name, 1))
				continue;
			if (de->name_len == 2 && !strncmp("..", de->name, 2))
				continue;

			iter = ocfs2_iget(osb, le64_to_cpu(de->inode),
					  OCFS2_FI_FLAG_NOLOCK);
			if (IS_ERR(iter))
				continue;

			mlog(0, "queue orphan %llu\n",
			     (unsigned long long)OCFS2_I(iter)->ip_blkno);
			/* No locking is required for the next_orphan
			 * queue as there is only ever a single
			 * process doing orphan recovery. */
			OCFS2_I(iter)->ip_next_orphan = *head;
			*head = iter;
		}
		brelse(bh);
	}

out_unlock:
	ocfs2_meta_unlock(orphan_dir_inode, 0);
out:
	mutex_unlock(&orphan_dir_inode->i_mutex);
	iput(orphan_dir_inode);
	return status;
}

static int ocfs2_orphan_recovery_can_continue(struct ocfs2_super *osb,
					      int slot)
{
	int ret;

	spin_lock(&osb->osb_lock);
	ret = !osb->osb_orphan_wipes[slot];
	spin_unlock(&osb->osb_lock);
	return ret;
}

static void ocfs2_mark_recovering_orphan_dir(struct ocfs2_super *osb,
					     int slot)
{
	spin_lock(&osb->osb_lock);
	/* Mark ourselves such that new processes in delete_inode()
	 * know to quit early. */
	ocfs2_node_map_set_bit(osb, &osb->osb_recovering_orphan_dirs, slot);
	while (osb->osb_orphan_wipes[slot]) {
		/* If any processes are already in the middle of an
		 * orphan wipe on this dir, then we need to wait for
		 * them. */
		spin_unlock(&osb->osb_lock);
		wait_event_interruptible(osb->osb_wipe_event,
					 ocfs2_orphan_recovery_can_continue(osb, slot));
		spin_lock(&osb->osb_lock);
	}
	spin_unlock(&osb->osb_lock);
}

static void ocfs2_clear_recovering_orphan_dir(struct ocfs2_super *osb,
					      int slot)
{
	ocfs2_node_map_clear_bit(osb, &osb->osb_recovering_orphan_dirs, slot);
}

/*
 * Orphan recovery. Each mounted node has it's own orphan dir which we
 * must run during recovery. Our strategy here is to build a list of
 * the inodes in the orphan dir and iget/iput them. The VFS does
 * (most) of the rest of the work.
 *
 * Orphan recovery can happen at any time, not just mount so we have a
 * couple of extra considerations.
 *
 * - We grab as many inodes as we can under the orphan dir lock -
 *   doing iget() outside the orphan dir risks getting a reference on
 *   an invalid inode.
 * - We must be sure not to deadlock with other processes on the
 *   system wanting to run delete_inode(). This can happen when they go
 *   to lock the orphan dir and the orphan recovery process attempts to
 *   iget() inside the orphan dir lock. This can be avoided by
 *   advertising our state to ocfs2_delete_inode().
 */
static int ocfs2_recover_orphans(struct ocfs2_super *osb,
				 int slot)
{
	int ret = 0;
	struct inode *inode = NULL;
	struct inode *iter;
	struct ocfs2_inode_info *oi;

	mlog(0, "Recover inodes from orphan dir in slot %d\n", slot);

	ocfs2_mark_recovering_orphan_dir(osb, slot);
	ret = ocfs2_queue_orphans(osb, slot, &inode);
	ocfs2_clear_recovering_orphan_dir(osb, slot);

	/* Error here should be noted, but we want to continue with as
	 * many queued inodes as we've got. */
	if (ret)
		mlog_errno(ret);

	while (inode) {
		oi = OCFS2_I(inode);
		mlog(0, "iput orphan %llu\n", (unsigned long long)oi->ip_blkno);

		iter = oi->ip_next_orphan;

		spin_lock(&oi->ip_lock);
		/* Delete voting may have set these on the assumption
		 * that the other node would wipe them successfully.
		 * If they are still in the node's orphan dir, we need
		 * to reset that state. */
		oi->ip_flags &= ~(OCFS2_INODE_DELETED|OCFS2_INODE_SKIP_DELETE);

		/* Set the proper information to get us going into
		 * ocfs2_delete_inode. */
		oi->ip_flags |= OCFS2_INODE_MAYBE_ORPHANED;
		oi->ip_orphaned_slot = slot;
		spin_unlock(&oi->ip_lock);

		iput(inode);

		inode = iter;
	}

	return ret;
}

static int ocfs2_wait_on_mount(struct ocfs2_super *osb)
{
	/* This check is good because ocfs2 will wait on our recovery
	 * thread before changing it to something other than MOUNTED
	 * or DISABLED. */
	wait_event(osb->osb_mount_event,
		   atomic_read(&osb->vol_state) == VOLUME_MOUNTED ||
		   atomic_read(&osb->vol_state) == VOLUME_DISABLED);

	/* If there's an error on mount, then we may never get to the
	 * MOUNTED flag, but this is set right before
	 * dismount_volume() so we can trust it. */
	if (atomic_read(&osb->vol_state) == VOLUME_DISABLED) {
		mlog(0, "mount error, exiting!\n");
		return -EBUSY;
	}

	return 0;
}

static int ocfs2_commit_thread(void *arg)
{
	int status;
	struct ocfs2_super *osb = arg;
	struct ocfs2_journal *journal = osb->journal;

	/* we can trust j_num_trans here because _should_stop() is only set in
	 * shutdown and nobody other than ourselves should be able to start
	 * transactions.  committing on shutdown might take a few iterations
	 * as final transactions put deleted inodes on the list */
	while (!(kthread_should_stop() &&
		 atomic_read(&journal->j_num_trans) == 0)) {

		wait_event_interruptible(osb->checkpoint_event,
					 atomic_read(&journal->j_num_trans)
					 || kthread_should_stop());

		status = ocfs2_commit_cache(osb);
		if (status < 0)
			mlog_errno(status);

		if (kthread_should_stop() && atomic_read(&journal->j_num_trans)){
			mlog(ML_KTHREAD,
			     "commit_thread: %u transactions pending on "
			     "shutdown\n",
			     atomic_read(&journal->j_num_trans));
		}
	}

	return 0;
}

/* Look for a dirty journal without taking any cluster locks. Used for
 * hard readonly access to determine whether the file system journals
 * require recovery. */
int ocfs2_check_journals_nolocks(struct ocfs2_super *osb)
{
	int ret = 0;
	unsigned int slot;
	struct buffer_head *di_bh;
	struct ocfs2_dinode *di;
	struct inode *journal = NULL;

	for(slot = 0; slot < osb->max_slots; slot++) {
		journal = ocfs2_get_system_file_inode(osb,
						      JOURNAL_SYSTEM_INODE,
						      slot);
		if (!journal || is_bad_inode(journal)) {
			ret = -EACCES;
			mlog_errno(ret);
			goto out;
		}

		di_bh = NULL;
		ret = ocfs2_read_block(osb, OCFS2_I(journal)->ip_blkno, &di_bh,
				       0, journal);
		if (ret < 0) {
			mlog_errno(ret);
			goto out;
		}

		di = (struct ocfs2_dinode *) di_bh->b_data;

		if (le32_to_cpu(di->id1.journal1.ij_flags) &
		    OCFS2_JOURNAL_DIRTY_FL)
			ret = -EROFS;

		brelse(di_bh);
		if (ret)
			break;
	}

out:
	if (journal)
		iput(journal);

	return ret;
}
