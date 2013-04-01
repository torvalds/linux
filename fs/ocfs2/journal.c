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
#include <linux/time.h>
#include <linux/random.h>

#include <cluster/masklog.h>

#include "ocfs2.h"

#include "alloc.h"
#include "blockcheck.h"
#include "dir.h"
#include "dlmglue.h"
#include "extent_map.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "localalloc.h"
#include "slot_map.h"
#include "super.h"
#include "sysfile.h"
#include "uptodate.h"
#include "quota.h"

#include "buffer_head_io.h"
#include "ocfs2_trace.h"

DEFINE_SPINLOCK(trans_inc_lock);

#define ORPHAN_SCAN_SCHEDULE_TIMEOUT 300000

static int ocfs2_force_read_journal(struct inode *inode);
static int ocfs2_recover_node(struct ocfs2_super *osb,
			      int node_num, int slot_num);
static int __ocfs2_recovery_thread(void *arg);
static int ocfs2_commit_cache(struct ocfs2_super *osb);
static int __ocfs2_wait_on_mount(struct ocfs2_super *osb, int quota);
static int ocfs2_journal_toggle_dirty(struct ocfs2_super *osb,
				      int dirty, int replayed);
static int ocfs2_trylock_journal(struct ocfs2_super *osb,
				 int slot_num);
static int ocfs2_recover_orphans(struct ocfs2_super *osb,
				 int slot);
static int ocfs2_commit_thread(void *arg);
static void ocfs2_queue_recovery_completion(struct ocfs2_journal *journal,
					    int slot_num,
					    struct ocfs2_dinode *la_dinode,
					    struct ocfs2_dinode *tl_dinode,
					    struct ocfs2_quota_recovery *qrec);

static inline int ocfs2_wait_on_mount(struct ocfs2_super *osb)
{
	return __ocfs2_wait_on_mount(osb, 0);
}

static inline int ocfs2_wait_on_quotas(struct ocfs2_super *osb)
{
	return __ocfs2_wait_on_mount(osb, 1);
}

/*
 * This replay_map is to track online/offline slots, so we could recover
 * offline slots during recovery and mount
 */

enum ocfs2_replay_state {
	REPLAY_UNNEEDED = 0,	/* Replay is not needed, so ignore this map */
	REPLAY_NEEDED, 		/* Replay slots marked in rm_replay_slots */
	REPLAY_DONE 		/* Replay was already queued */
};

struct ocfs2_replay_map {
	unsigned int rm_slots;
	enum ocfs2_replay_state rm_state;
	unsigned char rm_replay_slots[0];
};

void ocfs2_replay_map_set_state(struct ocfs2_super *osb, int state)
{
	if (!osb->replay_map)
		return;

	/* If we've already queued the replay, we don't have any more to do */
	if (osb->replay_map->rm_state == REPLAY_DONE)
		return;

	osb->replay_map->rm_state = state;
}

int ocfs2_compute_replay_slots(struct ocfs2_super *osb)
{
	struct ocfs2_replay_map *replay_map;
	int i, node_num;

	/* If replay map is already set, we don't do it again */
	if (osb->replay_map)
		return 0;

	replay_map = kzalloc(sizeof(struct ocfs2_replay_map) +
			     (osb->max_slots * sizeof(char)), GFP_KERNEL);

	if (!replay_map) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	spin_lock(&osb->osb_lock);

	replay_map->rm_slots = osb->max_slots;
	replay_map->rm_state = REPLAY_UNNEEDED;

	/* set rm_replay_slots for offline slot(s) */
	for (i = 0; i < replay_map->rm_slots; i++) {
		if (ocfs2_slot_to_node_num_locked(osb, i, &node_num) == -ENOENT)
			replay_map->rm_replay_slots[i] = 1;
	}

	osb->replay_map = replay_map;
	spin_unlock(&osb->osb_lock);
	return 0;
}

void ocfs2_queue_replay_slots(struct ocfs2_super *osb)
{
	struct ocfs2_replay_map *replay_map = osb->replay_map;
	int i;

	if (!replay_map)
		return;

	if (replay_map->rm_state != REPLAY_NEEDED)
		return;

	for (i = 0; i < replay_map->rm_slots; i++)
		if (replay_map->rm_replay_slots[i])
			ocfs2_queue_recovery_completion(osb->journal, i, NULL,
							NULL, NULL);
	replay_map->rm_state = REPLAY_DONE;
}

void ocfs2_free_replay_slots(struct ocfs2_super *osb)
{
	struct ocfs2_replay_map *replay_map = osb->replay_map;

	if (!osb->replay_map)
		return;

	kfree(replay_map);
	osb->replay_map = NULL;
}

int ocfs2_recovery_init(struct ocfs2_super *osb)
{
	struct ocfs2_recovery_map *rm;

	mutex_init(&osb->recovery_lock);
	osb->disable_recovery = 0;
	osb->recovery_thread_task = NULL;
	init_waitqueue_head(&osb->recovery_event);

	rm = kzalloc(sizeof(struct ocfs2_recovery_map) +
		     osb->max_slots * sizeof(unsigned int),
		     GFP_KERNEL);
	if (!rm) {
		mlog_errno(-ENOMEM);
		return -ENOMEM;
	}

	rm->rm_entries = (unsigned int *)((char *)rm +
					  sizeof(struct ocfs2_recovery_map));
	osb->recovery_map = rm;

	return 0;
}

/* we can't grab the goofy sem lock from inside wait_event, so we use
 * memory barriers to make sure that we'll see the null task before
 * being woken up */
static int ocfs2_recovery_thread_running(struct ocfs2_super *osb)
{
	mb();
	return osb->recovery_thread_task != NULL;
}

void ocfs2_recovery_exit(struct ocfs2_super *osb)
{
	struct ocfs2_recovery_map *rm;

	/* disable any new recovery threads and wait for any currently
	 * running ones to exit. Do this before setting the vol_state. */
	mutex_lock(&osb->recovery_lock);
	osb->disable_recovery = 1;
	mutex_unlock(&osb->recovery_lock);
	wait_event(osb->recovery_event, !ocfs2_recovery_thread_running(osb));

	/* At this point, we know that no more recovery threads can be
	 * launched, so wait for any recovery completion work to
	 * complete. */
	flush_workqueue(ocfs2_wq);

	/*
	 * Now that recovery is shut down, and the osb is about to be
	 * freed,  the osb_lock is not taken here.
	 */
	rm = osb->recovery_map;
	/* XXX: Should we bug if there are dirty entries? */

	kfree(rm);
}

static int __ocfs2_recovery_map_test(struct ocfs2_super *osb,
				     unsigned int node_num)
{
	int i;
	struct ocfs2_recovery_map *rm = osb->recovery_map;

	assert_spin_locked(&osb->osb_lock);

	for (i = 0; i < rm->rm_used; i++) {
		if (rm->rm_entries[i] == node_num)
			return 1;
	}

	return 0;
}

/* Behaves like test-and-set.  Returns the previous value */
static int ocfs2_recovery_map_set(struct ocfs2_super *osb,
				  unsigned int node_num)
{
	struct ocfs2_recovery_map *rm = osb->recovery_map;

	spin_lock(&osb->osb_lock);
	if (__ocfs2_recovery_map_test(osb, node_num)) {
		spin_unlock(&osb->osb_lock);
		return 1;
	}

	/* XXX: Can this be exploited? Not from o2dlm... */
	BUG_ON(rm->rm_used >= osb->max_slots);

	rm->rm_entries[rm->rm_used] = node_num;
	rm->rm_used++;
	spin_unlock(&osb->osb_lock);

	return 0;
}

static void ocfs2_recovery_map_clear(struct ocfs2_super *osb,
				     unsigned int node_num)
{
	int i;
	struct ocfs2_recovery_map *rm = osb->recovery_map;

	spin_lock(&osb->osb_lock);

	for (i = 0; i < rm->rm_used; i++) {
		if (rm->rm_entries[i] == node_num)
			break;
	}

	if (i < rm->rm_used) {
		/* XXX: be careful with the pointer math */
		memmove(&(rm->rm_entries[i]), &(rm->rm_entries[i + 1]),
			(rm->rm_used - i - 1) * sizeof(unsigned int));
		rm->rm_used--;
	}

	spin_unlock(&osb->osb_lock);
}

static int ocfs2_commit_cache(struct ocfs2_super *osb)
{
	int status = 0;
	unsigned int flushed;
	struct ocfs2_journal *journal = NULL;

	journal = osb->journal;

	/* Flush all pending commits and checkpoint the journal. */
	down_write(&journal->j_trans_barrier);

	flushed = atomic_read(&journal->j_num_trans);
	trace_ocfs2_commit_cache_begin(flushed);
	if (flushed == 0) {
		up_write(&journal->j_trans_barrier);
		goto finally;
	}

	jbd2_journal_lock_updates(journal->j_journal);
	status = jbd2_journal_flush(journal->j_journal);
	jbd2_journal_unlock_updates(journal->j_journal);
	if (status < 0) {
		up_write(&journal->j_trans_barrier);
		mlog_errno(status);
		goto finally;
	}

	ocfs2_inc_trans_id(journal);

	flushed = atomic_read(&journal->j_num_trans);
	atomic_set(&journal->j_num_trans, 0);
	up_write(&journal->j_trans_barrier);

	trace_ocfs2_commit_cache_end(journal->j_trans_id, flushed);

	ocfs2_wake_downconvert_thread(osb);
	wake_up(&journal->j_checkpointed);
finally:
	return status;
}

handle_t *ocfs2_start_trans(struct ocfs2_super *osb, int max_buffs)
{
	journal_t *journal = osb->journal->j_journal;
	handle_t *handle;

	BUG_ON(!osb || !osb->journal->j_journal);

	if (ocfs2_is_hard_readonly(osb))
		return ERR_PTR(-EROFS);

	BUG_ON(osb->journal->j_state == OCFS2_JOURNAL_FREE);
	BUG_ON(max_buffs <= 0);

	/* Nested transaction? Just return the handle... */
	if (journal_current_handle())
		return jbd2_journal_start(journal, max_buffs);

	sb_start_intwrite(osb->sb);

	down_read(&osb->journal->j_trans_barrier);

	handle = jbd2_journal_start(journal, max_buffs);
	if (IS_ERR(handle)) {
		up_read(&osb->journal->j_trans_barrier);
		sb_end_intwrite(osb->sb);

		mlog_errno(PTR_ERR(handle));

		if (is_journal_aborted(journal)) {
			ocfs2_abort(osb->sb, "Detected aborted journal");
			handle = ERR_PTR(-EROFS);
		}
	} else {
		if (!ocfs2_mount_local(osb))
			atomic_inc(&(osb->journal->j_num_trans));
	}

	return handle;
}

int ocfs2_commit_trans(struct ocfs2_super *osb,
		       handle_t *handle)
{
	int ret, nested;
	struct ocfs2_journal *journal = osb->journal;

	BUG_ON(!handle);

	nested = handle->h_ref > 1;
	ret = jbd2_journal_stop(handle);
	if (ret < 0)
		mlog_errno(ret);

	if (!nested) {
		up_read(&journal->j_trans_barrier);
		sb_end_intwrite(osb->sb);
	}

	return ret;
}

/*
 * 'nblocks' is what you want to add to the current transaction.
 *
 * This might call jbd2_journal_restart() which will commit dirty buffers
 * and then restart the transaction. Before calling
 * ocfs2_extend_trans(), any changed blocks should have been
 * dirtied. After calling it, all blocks which need to be changed must
 * go through another set of journal_access/journal_dirty calls.
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
	int status, old_nblocks;

	BUG_ON(!handle);
	BUG_ON(nblocks < 0);

	if (!nblocks)
		return 0;

	old_nblocks = handle->h_buffer_credits;

	trace_ocfs2_extend_trans(old_nblocks, nblocks);

#ifdef CONFIG_OCFS2_DEBUG_FS
	status = 1;
#else
	status = jbd2_journal_extend(handle, nblocks);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
#endif

	if (status > 0) {
		trace_ocfs2_extend_trans_restart(old_nblocks + nblocks);
		status = jbd2_journal_restart(handle,
					      old_nblocks + nblocks);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}
	}

	status = 0;
bail:
	return status;
}

struct ocfs2_triggers {
	struct jbd2_buffer_trigger_type	ot_triggers;
	int				ot_offset;
};

static inline struct ocfs2_triggers *to_ocfs2_trigger(struct jbd2_buffer_trigger_type *triggers)
{
	return container_of(triggers, struct ocfs2_triggers, ot_triggers);
}

static void ocfs2_frozen_trigger(struct jbd2_buffer_trigger_type *triggers,
				 struct buffer_head *bh,
				 void *data, size_t size)
{
	struct ocfs2_triggers *ot = to_ocfs2_trigger(triggers);

	/*
	 * We aren't guaranteed to have the superblock here, so we
	 * must unconditionally compute the ecc data.
	 * __ocfs2_journal_access() will only set the triggers if
	 * metaecc is enabled.
	 */
	ocfs2_block_check_compute(data, size, data + ot->ot_offset);
}

/*
 * Quota blocks have their own trigger because the struct ocfs2_block_check
 * offset depends on the blocksize.
 */
static void ocfs2_dq_frozen_trigger(struct jbd2_buffer_trigger_type *triggers,
				 struct buffer_head *bh,
				 void *data, size_t size)
{
	struct ocfs2_disk_dqtrailer *dqt =
		ocfs2_block_dqtrailer(size, data);

	/*
	 * We aren't guaranteed to have the superblock here, so we
	 * must unconditionally compute the ecc data.
	 * __ocfs2_journal_access() will only set the triggers if
	 * metaecc is enabled.
	 */
	ocfs2_block_check_compute(data, size, &dqt->dq_check);
}

/*
 * Directory blocks also have their own trigger because the
 * struct ocfs2_block_check offset depends on the blocksize.
 */
static void ocfs2_db_frozen_trigger(struct jbd2_buffer_trigger_type *triggers,
				 struct buffer_head *bh,
				 void *data, size_t size)
{
	struct ocfs2_dir_block_trailer *trailer =
		ocfs2_dir_trailer_from_size(size, data);

	/*
	 * We aren't guaranteed to have the superblock here, so we
	 * must unconditionally compute the ecc data.
	 * __ocfs2_journal_access() will only set the triggers if
	 * metaecc is enabled.
	 */
	ocfs2_block_check_compute(data, size, &trailer->db_check);
}

static void ocfs2_abort_trigger(struct jbd2_buffer_trigger_type *triggers,
				struct buffer_head *bh)
{
	mlog(ML_ERROR,
	     "ocfs2_abort_trigger called by JBD2.  bh = 0x%lx, "
	     "bh->b_blocknr = %llu\n",
	     (unsigned long)bh,
	     (unsigned long long)bh->b_blocknr);

	/* We aren't guaranteed to have the superblock here - but if we
	 * don't, it'll just crash. */
	ocfs2_error(bh->b_assoc_map->host->i_sb,
		    "JBD2 has aborted our journal, ocfs2 cannot continue\n");
}

static struct ocfs2_triggers di_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_dinode, i_check),
};

static struct ocfs2_triggers eb_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_extent_block, h_check),
};

static struct ocfs2_triggers rb_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_refcount_block, rf_check),
};

static struct ocfs2_triggers gd_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_group_desc, bg_check),
};

static struct ocfs2_triggers db_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_db_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
};

static struct ocfs2_triggers xb_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_xattr_block, xb_check),
};

static struct ocfs2_triggers dq_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_dq_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
};

static struct ocfs2_triggers dr_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_dx_root_block, dr_check),
};

static struct ocfs2_triggers dl_triggers = {
	.ot_triggers = {
		.t_frozen = ocfs2_frozen_trigger,
		.t_abort = ocfs2_abort_trigger,
	},
	.ot_offset	= offsetof(struct ocfs2_dx_leaf, dl_check),
};

static int __ocfs2_journal_access(handle_t *handle,
				  struct ocfs2_caching_info *ci,
				  struct buffer_head *bh,
				  struct ocfs2_triggers *triggers,
				  int type)
{
	int status;
	struct ocfs2_super *osb =
		OCFS2_SB(ocfs2_metadata_cache_get_super(ci));

	BUG_ON(!ci || !ci->ci_ops);
	BUG_ON(!handle);
	BUG_ON(!bh);

	trace_ocfs2_journal_access(
		(unsigned long long)ocfs2_metadata_cache_owner(ci),
		(unsigned long long)bh->b_blocknr, type, bh->b_size);

	/* we can safely remove this assertion after testing. */
	if (!buffer_uptodate(bh)) {
		mlog(ML_ERROR, "giving me a buffer that's not uptodate!\n");
		mlog(ML_ERROR, "b_blocknr=%llu\n",
		     (unsigned long long)bh->b_blocknr);
		BUG();
	}

	/* Set the current transaction information on the ci so
	 * that the locking code knows whether it can drop it's locks
	 * on this ci or not. We're protected from the commit
	 * thread updating the current transaction id until
	 * ocfs2_commit_trans() because ocfs2_start_trans() took
	 * j_trans_barrier for us. */
	ocfs2_set_ci_lock_trans(osb->journal, ci);

	ocfs2_metadata_cache_io_lock(ci);
	switch (type) {
	case OCFS2_JOURNAL_ACCESS_CREATE:
	case OCFS2_JOURNAL_ACCESS_WRITE:
		status = jbd2_journal_get_write_access(handle, bh);
		break;

	case OCFS2_JOURNAL_ACCESS_UNDO:
		status = jbd2_journal_get_undo_access(handle, bh);
		break;

	default:
		status = -EINVAL;
		mlog(ML_ERROR, "Unknown access type!\n");
	}
	if (!status && ocfs2_meta_ecc(osb) && triggers)
		jbd2_journal_set_triggers(bh, &triggers->ot_triggers);
	ocfs2_metadata_cache_io_unlock(ci);

	if (status < 0)
		mlog(ML_ERROR, "Error %d getting %d access to buffer!\n",
		     status, type);

	return status;
}

int ocfs2_journal_access_di(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &di_triggers, type);
}

int ocfs2_journal_access_eb(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &eb_triggers, type);
}

int ocfs2_journal_access_rb(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &rb_triggers,
				      type);
}

int ocfs2_journal_access_gd(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &gd_triggers, type);
}

int ocfs2_journal_access_db(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &db_triggers, type);
}

int ocfs2_journal_access_xb(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &xb_triggers, type);
}

int ocfs2_journal_access_dq(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &dq_triggers, type);
}

int ocfs2_journal_access_dr(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &dr_triggers, type);
}

int ocfs2_journal_access_dl(handle_t *handle, struct ocfs2_caching_info *ci,
			    struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, &dl_triggers, type);
}

int ocfs2_journal_access(handle_t *handle, struct ocfs2_caching_info *ci,
			 struct buffer_head *bh, int type)
{
	return __ocfs2_journal_access(handle, ci, bh, NULL, type);
}

void ocfs2_journal_dirty(handle_t *handle, struct buffer_head *bh)
{
	int status;

	trace_ocfs2_journal_dirty((unsigned long long)bh->b_blocknr);

	status = jbd2_journal_dirty_metadata(handle, bh);
	BUG_ON(status);
}

#define OCFS2_DEFAULT_COMMIT_INTERVAL	(HZ * JBD2_DEFAULT_MAX_COMMIT_AGE)

void ocfs2_set_journal_params(struct ocfs2_super *osb)
{
	journal_t *journal = osb->journal->j_journal;
	unsigned long commit_interval = OCFS2_DEFAULT_COMMIT_INTERVAL;

	if (osb->osb_commit_interval)
		commit_interval = osb->osb_commit_interval;

	write_lock(&journal->j_state_lock);
	journal->j_commit_interval = commit_interval;
	if (osb->s_mount_opt & OCFS2_MOUNT_BARRIER)
		journal->j_flags |= JBD2_BARRIER;
	else
		journal->j_flags &= ~JBD2_BARRIER;
	write_unlock(&journal->j_state_lock);
}

int ocfs2_journal_init(struct ocfs2_journal *journal, int *dirty)
{
	int status = -1;
	struct inode *inode = NULL; /* the journal inode */
	journal_t *j_journal = NULL;
	struct ocfs2_dinode *di = NULL;
	struct buffer_head *bh = NULL;
	struct ocfs2_super *osb;
	int inode_lock = 0;

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
	status = ocfs2_inode_lock_full(inode, &bh, 1, OCFS2_META_LOCK_RECOVERY);
	if (status < 0) {
		if (status != -ERESTARTSYS)
			mlog(ML_ERROR, "Could not get lock on journal!\n");
		goto done;
	}

	inode_lock = 1;
	di = (struct ocfs2_dinode *)bh->b_data;

	if (inode->i_size <  OCFS2_MIN_JOURNAL_SIZE) {
		mlog(ML_ERROR, "Journal file size (%lld) is too small!\n",
		     inode->i_size);
		status = -EINVAL;
		goto done;
	}

	trace_ocfs2_journal_init(inode->i_size,
				 (unsigned long long)inode->i_blocks,
				 OCFS2_I(inode)->ip_clusters);

	/* call the kernels journal init function now */
	j_journal = jbd2_journal_init_inode(inode);
	if (j_journal == NULL) {
		mlog(ML_ERROR, "Linux journal layer error\n");
		status = -EINVAL;
		goto done;
	}

	trace_ocfs2_journal_init_maxlen(j_journal->j_maxlen);

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
		if (inode_lock)
			ocfs2_inode_unlock(inode, 1);
		brelse(bh);
		if (inode) {
			OCFS2_I(inode)->ip_open_count--;
			iput(inode);
		}
	}

	return status;
}

static void ocfs2_bump_recovery_generation(struct ocfs2_dinode *di)
{
	le32_add_cpu(&(di->id1.journal1.ij_recovery_generation), 1);
}

static u32 ocfs2_get_recovery_generation(struct ocfs2_dinode *di)
{
	return le32_to_cpu(di->id1.journal1.ij_recovery_generation);
}

static int ocfs2_journal_toggle_dirty(struct ocfs2_super *osb,
				      int dirty, int replayed)
{
	int status;
	unsigned int flags;
	struct ocfs2_journal *journal = osb->journal;
	struct buffer_head *bh = journal->j_bh;
	struct ocfs2_dinode *fe;

	fe = (struct ocfs2_dinode *)bh->b_data;

	/* The journal bh on the osb always comes from ocfs2_journal_init()
	 * and was validated there inside ocfs2_inode_lock_full().  It's a
	 * code bug if we mess it up. */
	BUG_ON(!OCFS2_IS_VALID_DINODE(fe));

	flags = le32_to_cpu(fe->id1.journal1.ij_flags);
	if (dirty)
		flags |= OCFS2_JOURNAL_DIRTY_FL;
	else
		flags &= ~OCFS2_JOURNAL_DIRTY_FL;
	fe->id1.journal1.ij_flags = cpu_to_le32(flags);

	if (replayed)
		ocfs2_bump_recovery_generation(fe);

	ocfs2_compute_meta_ecc(osb->sb, bh->b_data, &fe->i_check);
	status = ocfs2_write_block(osb, bh, INODE_CACHE(journal->j_inode));
	if (status < 0)
		mlog_errno(status);

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

	BUG_ON(!osb);

	journal = osb->journal;
	if (!journal)
		goto done;

	inode = journal->j_inode;

	if (journal->j_state != OCFS2_JOURNAL_LOADED)
		goto done;

	/* need to inc inode use count - jbd2_journal_destroy will iput. */
	if (!igrab(inode))
		BUG();

	num_running_trans = atomic_read(&(osb->journal->j_num_trans));
	trace_ocfs2_journal_shutdown(num_running_trans);

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
		trace_ocfs2_journal_shutdown_wait(osb->commit_task);
		kthread_stop(osb->commit_task);
		osb->commit_task = NULL;
	}

	BUG_ON(atomic_read(&(osb->journal->j_num_trans)) != 0);

	if (ocfs2_mount_local(osb)) {
		jbd2_journal_lock_updates(journal->j_journal);
		status = jbd2_journal_flush(journal->j_journal);
		jbd2_journal_unlock_updates(journal->j_journal);
		if (status < 0)
			mlog_errno(status);
	}

	if (status == 0) {
		/*
		 * Do not toggle if flush was unsuccessful otherwise
		 * will leave dirty metadata in a "clean" journal
		 */
		status = ocfs2_journal_toggle_dirty(osb, 0, 0);
		if (status < 0)
			mlog_errno(status);
	}

	/* Shutdown the kernel journal system */
	jbd2_journal_destroy(journal->j_journal);
	journal->j_journal = NULL;

	OCFS2_I(inode)->ip_open_count--;

	/* unlock our journal */
	ocfs2_inode_unlock(inode, 1);

	brelse(journal->j_bh);
	journal->j_bh = NULL;

	journal->j_state = OCFS2_JOURNAL_FREE;

//	up_write(&journal->j_trans_barrier);
done:
	if (inode)
		iput(inode);
}

static void ocfs2_clear_journal_error(struct super_block *sb,
				      journal_t *journal,
				      int slot)
{
	int olderr;

	olderr = jbd2_journal_errno(journal);
	if (olderr) {
		mlog(ML_ERROR, "File system error %d recorded in "
		     "journal %u.\n", olderr, slot);
		mlog(ML_ERROR, "File system on device %s needs checking.\n",
		     sb->s_id);

		jbd2_journal_ack_err(journal);
		jbd2_journal_clear_err(journal);
	}
}

int ocfs2_journal_load(struct ocfs2_journal *journal, int local, int replayed)
{
	int status = 0;
	struct ocfs2_super *osb;

	BUG_ON(!journal);

	osb = journal->j_osb;

	status = jbd2_journal_load(journal->j_journal);
	if (status < 0) {
		mlog(ML_ERROR, "Failed to load journal!\n");
		goto done;
	}

	ocfs2_clear_journal_error(osb->sb, journal->j_journal, osb->slot_num);

	status = ocfs2_journal_toggle_dirty(osb, 1, replayed);
	if (status < 0) {
		mlog_errno(status);
		goto done;
	}

	/* Launch the commit thread */
	if (!local) {
		osb->commit_task = kthread_run(ocfs2_commit_thread, osb,
					       "ocfs2cmt");
		if (IS_ERR(osb->commit_task)) {
			status = PTR_ERR(osb->commit_task);
			osb->commit_task = NULL;
			mlog(ML_ERROR, "unable to launch ocfs2commit thread, "
			     "error=%d", status);
			goto done;
		}
	} else
		osb->commit_task = NULL;

done:
	return status;
}


/* 'full' flag tells us whether we clear out all blocks or if we just
 * mark the journal clean */
int ocfs2_journal_wipe(struct ocfs2_journal *journal, int full)
{
	int status;

	BUG_ON(!journal);

	status = jbd2_journal_wipe(journal->j_journal, full);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_journal_toggle_dirty(journal->j_osb, 0, 0);
	if (status < 0)
		mlog_errno(status);

bail:
	return status;
}

static int ocfs2_recovery_completed(struct ocfs2_super *osb)
{
	int empty;
	struct ocfs2_recovery_map *rm = osb->recovery_map;

	spin_lock(&osb->osb_lock);
	empty = (rm->rm_used == 0);
	spin_unlock(&osb->osb_lock);

	return empty;
}

void ocfs2_wait_for_recovery(struct ocfs2_super *osb)
{
	wait_event(osb->recovery_event, ocfs2_recovery_completed(osb));
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
	int i;
	u64 v_blkno, p_blkno, p_blocks, num_blocks;
#define CONCURRENT_JOURNAL_FILL 32ULL
	struct buffer_head *bhs[CONCURRENT_JOURNAL_FILL];

	memset(bhs, 0, sizeof(struct buffer_head *) * CONCURRENT_JOURNAL_FILL);

	num_blocks = ocfs2_blocks_for_bytes(inode->i_sb, inode->i_size);
	v_blkno = 0;
	while (v_blkno < num_blocks) {
		status = ocfs2_extent_map_get_blocks(inode, v_blkno,
						     &p_blkno, &p_blocks, NULL);
		if (status < 0) {
			mlog_errno(status);
			goto bail;
		}

		if (p_blocks > CONCURRENT_JOURNAL_FILL)
			p_blocks = CONCURRENT_JOURNAL_FILL;

		/* We are reading journal data which should not
		 * be put in the uptodate cache */
		status = ocfs2_read_blocks_sync(OCFS2_SB(inode->i_sb),
						p_blkno, p_blocks, bhs);
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
		brelse(bhs[i]);
	return status;
}

struct ocfs2_la_recovery_item {
	struct list_head	lri_list;
	int			lri_slot;
	struct ocfs2_dinode	*lri_la_dinode;
	struct ocfs2_dinode	*lri_tl_dinode;
	struct ocfs2_quota_recovery *lri_qrec;
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
void ocfs2_complete_recovery(struct work_struct *work)
{
	int ret = 0;
	struct ocfs2_journal *journal =
		container_of(work, struct ocfs2_journal, j_recovery_work);
	struct ocfs2_super *osb = journal->j_osb;
	struct ocfs2_dinode *la_dinode, *tl_dinode;
	struct ocfs2_la_recovery_item *item, *n;
	struct ocfs2_quota_recovery *qrec;
	LIST_HEAD(tmp_la_list);

	trace_ocfs2_complete_recovery(
		(unsigned long long)OCFS2_I(journal->j_inode)->ip_blkno);

	spin_lock(&journal->j_lock);
	list_splice_init(&journal->j_la_cleanups, &tmp_la_list);
	spin_unlock(&journal->j_lock);

	list_for_each_entry_safe(item, n, &tmp_la_list, lri_list) {
		list_del_init(&item->lri_list);

		ocfs2_wait_on_quotas(osb);

		la_dinode = item->lri_la_dinode;
		tl_dinode = item->lri_tl_dinode;
		qrec = item->lri_qrec;

		trace_ocfs2_complete_recovery_slot(item->lri_slot,
			la_dinode ? le64_to_cpu(la_dinode->i_blkno) : 0,
			tl_dinode ? le64_to_cpu(tl_dinode->i_blkno) : 0,
			qrec);

		if (la_dinode) {
			ret = ocfs2_complete_local_alloc_recovery(osb,
								  la_dinode);
			if (ret < 0)
				mlog_errno(ret);

			kfree(la_dinode);
		}

		if (tl_dinode) {
			ret = ocfs2_complete_truncate_log_recovery(osb,
								   tl_dinode);
			if (ret < 0)
				mlog_errno(ret);

			kfree(tl_dinode);
		}

		ret = ocfs2_recover_orphans(osb, item->lri_slot);
		if (ret < 0)
			mlog_errno(ret);

		if (qrec) {
			ret = ocfs2_finish_quota_recovery(osb, qrec,
							  item->lri_slot);
			if (ret < 0)
				mlog_errno(ret);
			/* Recovery info is already freed now */
		}

		kfree(item);
	}

	trace_ocfs2_complete_recovery_end(ret);
}

/* NOTE: This function always eats your references to la_dinode and
 * tl_dinode, either manually on error, or by passing them to
 * ocfs2_complete_recovery */
static void ocfs2_queue_recovery_completion(struct ocfs2_journal *journal,
					    int slot_num,
					    struct ocfs2_dinode *la_dinode,
					    struct ocfs2_dinode *tl_dinode,
					    struct ocfs2_quota_recovery *qrec)
{
	struct ocfs2_la_recovery_item *item;

	item = kmalloc(sizeof(struct ocfs2_la_recovery_item), GFP_NOFS);
	if (!item) {
		/* Though we wish to avoid it, we are in fact safe in
		 * skipping local alloc cleanup as fsck.ocfs2 is more
		 * than capable of reclaiming unused space. */
		kfree(la_dinode);
		kfree(tl_dinode);

		if (qrec)
			ocfs2_free_quota_recovery(qrec);

		mlog_errno(-ENOMEM);
		return;
	}

	INIT_LIST_HEAD(&item->lri_list);
	item->lri_la_dinode = la_dinode;
	item->lri_slot = slot_num;
	item->lri_tl_dinode = tl_dinode;
	item->lri_qrec = qrec;

	spin_lock(&journal->j_lock);
	list_add_tail(&item->lri_list, &journal->j_la_cleanups);
	queue_work(ocfs2_wq, &journal->j_recovery_work);
	spin_unlock(&journal->j_lock);
}

/* Called by the mount code to queue recovery the last part of
 * recovery for it's own and offline slot(s). */
void ocfs2_complete_mount_recovery(struct ocfs2_super *osb)
{
	struct ocfs2_journal *journal = osb->journal;

	if (ocfs2_is_hard_readonly(osb))
		return;

	/* No need to queue up our truncate_log as regular cleanup will catch
	 * that */
	ocfs2_queue_recovery_completion(journal, osb->slot_num,
					osb->local_alloc_copy, NULL, NULL);
	ocfs2_schedule_truncate_log_flush(osb, 0);

	osb->local_alloc_copy = NULL;
	osb->dirty = 0;

	/* queue to recover orphan slots for all offline slots */
	ocfs2_replay_map_set_state(osb, REPLAY_NEEDED);
	ocfs2_queue_replay_slots(osb);
	ocfs2_free_replay_slots(osb);
}

void ocfs2_complete_quota_recovery(struct ocfs2_super *osb)
{
	if (osb->quota_rec) {
		ocfs2_queue_recovery_completion(osb->journal,
						osb->slot_num,
						NULL,
						NULL,
						osb->quota_rec);
		osb->quota_rec = NULL;
	}
}

static int __ocfs2_recovery_thread(void *arg)
{
	int status, node_num, slot_num;
	struct ocfs2_super *osb = arg;
	struct ocfs2_recovery_map *rm = osb->recovery_map;
	int *rm_quota = NULL;
	int rm_quota_used = 0, i;
	struct ocfs2_quota_recovery *qrec;

	status = ocfs2_wait_on_mount(osb);
	if (status < 0) {
		goto bail;
	}

	rm_quota = kzalloc(osb->max_slots * sizeof(int), GFP_NOFS);
	if (!rm_quota) {
		status = -ENOMEM;
		goto bail;
	}
restart:
	status = ocfs2_super_lock(osb, 1);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = ocfs2_compute_replay_slots(osb);
	if (status < 0)
		mlog_errno(status);

	/* queue recovery for our own slot */
	ocfs2_queue_recovery_completion(osb->journal, osb->slot_num, NULL,
					NULL, NULL);

	spin_lock(&osb->osb_lock);
	while (rm->rm_used) {
		/* It's always safe to remove entry zero, as we won't
		 * clear it until ocfs2_recover_node() has succeeded. */
		node_num = rm->rm_entries[0];
		spin_unlock(&osb->osb_lock);
		slot_num = ocfs2_node_num_to_slot(osb, node_num);
		trace_ocfs2_recovery_thread_node(node_num, slot_num);
		if (slot_num == -ENOENT) {
			status = 0;
			goto skip_recovery;
		}

		/* It is a bit subtle with quota recovery. We cannot do it
		 * immediately because we have to obtain cluster locks from
		 * quota files and we also don't want to just skip it because
		 * then quota usage would be out of sync until some node takes
		 * the slot. So we remember which nodes need quota recovery
		 * and when everything else is done, we recover quotas. */
		for (i = 0; i < rm_quota_used && rm_quota[i] != slot_num; i++);
		if (i == rm_quota_used)
			rm_quota[rm_quota_used++] = slot_num;

		status = ocfs2_recover_node(osb, node_num, slot_num);
skip_recovery:
		if (!status) {
			ocfs2_recovery_map_clear(osb, node_num);
		} else {
			mlog(ML_ERROR,
			     "Error %d recovering node %d on device (%u,%u)!\n",
			     status, node_num,
			     MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));
			mlog(ML_ERROR, "Volume requires unmount.\n");
		}

		spin_lock(&osb->osb_lock);
	}
	spin_unlock(&osb->osb_lock);
	trace_ocfs2_recovery_thread_end(status);

	/* Refresh all journal recovery generations from disk */
	status = ocfs2_check_journals_nolocks(osb);
	status = (status == -EROFS) ? 0 : status;
	if (status < 0)
		mlog_errno(status);

	/* Now it is right time to recover quotas... We have to do this under
	 * superblock lock so that no one can start using the slot (and crash)
	 * before we recover it */
	for (i = 0; i < rm_quota_used; i++) {
		qrec = ocfs2_begin_quota_recovery(osb, rm_quota[i]);
		if (IS_ERR(qrec)) {
			status = PTR_ERR(qrec);
			mlog_errno(status);
			continue;
		}
		ocfs2_queue_recovery_completion(osb->journal, rm_quota[i],
						NULL, NULL, qrec);
	}

	ocfs2_super_unlock(osb, 1);

	/* queue recovery for offline slots */
	ocfs2_queue_replay_slots(osb);

bail:
	mutex_lock(&osb->recovery_lock);
	if (!status && !ocfs2_recovery_completed(osb)) {
		mutex_unlock(&osb->recovery_lock);
		goto restart;
	}

	ocfs2_free_replay_slots(osb);
	osb->recovery_thread_task = NULL;
	mb(); /* sync with ocfs2_recovery_thread_running */
	wake_up(&osb->recovery_event);

	mutex_unlock(&osb->recovery_lock);

	kfree(rm_quota);

	/* no one is callint kthread_stop() for us so the kthread() api
	 * requires that we call do_exit().  And it isn't exported, but
	 * complete_and_exit() seems to be a minimal wrapper around it. */
	complete_and_exit(NULL, status);
	return status;
}

void ocfs2_recovery_thread(struct ocfs2_super *osb, int node_num)
{
	mutex_lock(&osb->recovery_lock);

	trace_ocfs2_recovery_thread(node_num, osb->node_num,
		osb->disable_recovery, osb->recovery_thread_task,
		osb->disable_recovery ?
		-1 : ocfs2_recovery_map_set(osb, node_num));

	if (osb->disable_recovery)
		goto out;

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
}

static int ocfs2_read_journal_inode(struct ocfs2_super *osb,
				    int slot_num,
				    struct buffer_head **bh,
				    struct inode **ret_inode)
{
	int status = -EACCES;
	struct inode *inode = NULL;

	BUG_ON(slot_num >= osb->max_slots);

	inode = ocfs2_get_system_file_inode(osb, JOURNAL_SYSTEM_INODE,
					    slot_num);
	if (!inode || is_bad_inode(inode)) {
		mlog_errno(status);
		goto bail;
	}
	SET_INODE_JOURNAL(inode);

	status = ocfs2_read_inode_block_full(inode, bh, OCFS2_BH_IGNORE_CACHE);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	status = 0;

bail:
	if (inode) {
		if (status || !ret_inode)
			iput(inode);
		else
			*ret_inode = inode;
	}
	return status;
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
	u32 slot_reco_gen;

	status = ocfs2_read_journal_inode(osb, slot_num, &bh, &inode);
	if (status) {
		mlog_errno(status);
		goto done;
	}

	fe = (struct ocfs2_dinode *)bh->b_data;
	slot_reco_gen = ocfs2_get_recovery_generation(fe);
	brelse(bh);
	bh = NULL;

	/*
	 * As the fs recovery is asynchronous, there is a small chance that
	 * another node mounted (and recovered) the slot before the recovery
	 * thread could get the lock. To handle that, we dirty read the journal
	 * inode for that slot to get the recovery generation. If it is
	 * different than what we expected, the slot has been recovered.
	 * If not, it needs recovery.
	 */
	if (osb->slot_recovery_generations[slot_num] != slot_reco_gen) {
		trace_ocfs2_replay_journal_recovered(slot_num,
		     osb->slot_recovery_generations[slot_num], slot_reco_gen);
		osb->slot_recovery_generations[slot_num] = slot_reco_gen;
		status = -EBUSY;
		goto done;
	}

	/* Continue with recovery as the journal has not yet been recovered */

	status = ocfs2_inode_lock_full(inode, &bh, 1, OCFS2_META_LOCK_RECOVERY);
	if (status < 0) {
		trace_ocfs2_replay_journal_lock_err(status);
		if (status != -ERESTARTSYS)
			mlog(ML_ERROR, "Could not lock journal!\n");
		goto done;
	}
	got_lock = 1;

	fe = (struct ocfs2_dinode *) bh->b_data;

	flags = le32_to_cpu(fe->id1.journal1.ij_flags);
	slot_reco_gen = ocfs2_get_recovery_generation(fe);

	if (!(flags & OCFS2_JOURNAL_DIRTY_FL)) {
		trace_ocfs2_replay_journal_skip(node_num);
		/* Refresh recovery generation for the slot */
		osb->slot_recovery_generations[slot_num] = slot_reco_gen;
		goto done;
	}

	/* we need to run complete recovery for offline orphan slots */
	ocfs2_replay_map_set_state(osb, REPLAY_NEEDED);

	printk(KERN_NOTICE "ocfs2: Begin replay journal (node %d, slot %d) on "\
	       "device (%u,%u)\n", node_num, slot_num, MAJOR(osb->sb->s_dev),
	       MINOR(osb->sb->s_dev));

	OCFS2_I(inode)->ip_clusters = le32_to_cpu(fe->i_clusters);

	status = ocfs2_force_read_journal(inode);
	if (status < 0) {
		mlog_errno(status);
		goto done;
	}

	journal = jbd2_journal_init_inode(inode);
	if (journal == NULL) {
		mlog(ML_ERROR, "Linux journal layer error\n");
		status = -EIO;
		goto done;
	}

	status = jbd2_journal_load(journal);
	if (status < 0) {
		mlog_errno(status);
		if (!igrab(inode))
			BUG();
		jbd2_journal_destroy(journal);
		goto done;
	}

	ocfs2_clear_journal_error(osb->sb, journal, slot_num);

	/* wipe the journal */
	jbd2_journal_lock_updates(journal);
	status = jbd2_journal_flush(journal);
	jbd2_journal_unlock_updates(journal);
	if (status < 0)
		mlog_errno(status);

	/* This will mark the node clean */
	flags = le32_to_cpu(fe->id1.journal1.ij_flags);
	flags &= ~OCFS2_JOURNAL_DIRTY_FL;
	fe->id1.journal1.ij_flags = cpu_to_le32(flags);

	/* Increment recovery generation to indicate successful recovery */
	ocfs2_bump_recovery_generation(fe);
	osb->slot_recovery_generations[slot_num] =
					ocfs2_get_recovery_generation(fe);

	ocfs2_compute_meta_ecc(osb->sb, bh->b_data, &fe->i_check);
	status = ocfs2_write_block(osb, bh, INODE_CACHE(inode));
	if (status < 0)
		mlog_errno(status);

	if (!igrab(inode))
		BUG();

	jbd2_journal_destroy(journal);

	printk(KERN_NOTICE "ocfs2: End replay journal (node %d, slot %d) on "\
	       "device (%u,%u)\n", node_num, slot_num, MAJOR(osb->sb->s_dev),
	       MINOR(osb->sb->s_dev));
done:
	/* drop the lock on this nodes journal */
	if (got_lock)
		ocfs2_inode_unlock(inode, 1);

	if (inode)
		iput(inode);

	brelse(bh);

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
			      int node_num, int slot_num)
{
	int status = 0;
	struct ocfs2_dinode *la_copy = NULL;
	struct ocfs2_dinode *tl_copy = NULL;

	trace_ocfs2_recover_node(node_num, slot_num, osb->node_num);

	/* Should not ever be called to recover ourselves -- in that
	 * case we should've called ocfs2_journal_load instead. */
	BUG_ON(osb->node_num == node_num);

	status = ocfs2_replay_journal(osb, node_num, slot_num);
	if (status < 0) {
		if (status == -EBUSY) {
			trace_ocfs2_recover_node_skip(slot_num, node_num);
			status = 0;
			goto done;
		}
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
	status = ocfs2_clear_slot(osb, slot_num);
	if (status < 0)
		mlog_errno(status);

	/* This will kfree the memory pointed to by la_copy and tl_copy */
	ocfs2_queue_recovery_completion(osb->journal, slot_num, la_copy,
					tl_copy, NULL);

	status = 0;
done:

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
	status = ocfs2_inode_lock_full(inode, NULL, 1, flags);
	if (status < 0) {
		if (status != -EAGAIN)
			mlog_errno(status);
		goto bail;
	}

	ocfs2_inode_unlock(inode, 1);
bail:
	if (inode)
		iput(inode);

	return status;
}

/* Call this underneath ocfs2_super_lock. It also assumes that the
 * slot info struct has been updated from disk. */
int ocfs2_mark_dead_nodes(struct ocfs2_super *osb)
{
	unsigned int node_num;
	int status, i;
	u32 gen;
	struct buffer_head *bh = NULL;
	struct ocfs2_dinode *di;

	/* This is called with the super block cluster lock, so we
	 * know that the slot map can't change underneath us. */

	for (i = 0; i < osb->max_slots; i++) {
		/* Read journal inode to get the recovery generation */
		status = ocfs2_read_journal_inode(osb, i, &bh, NULL);
		if (status) {
			mlog_errno(status);
			goto bail;
		}
		di = (struct ocfs2_dinode *)bh->b_data;
		gen = ocfs2_get_recovery_generation(di);
		brelse(bh);
		bh = NULL;

		spin_lock(&osb->osb_lock);
		osb->slot_recovery_generations[i] = gen;

		trace_ocfs2_mark_dead_nodes(i,
					    osb->slot_recovery_generations[i]);

		if (i == osb->slot_num) {
			spin_unlock(&osb->osb_lock);
			continue;
		}

		status = ocfs2_slot_to_node_num_locked(osb, i, &node_num);
		if (status == -ENOENT) {
			spin_unlock(&osb->osb_lock);
			continue;
		}

		if (__ocfs2_recovery_map_test(osb, node_num)) {
			spin_unlock(&osb->osb_lock);
			continue;
		}
		spin_unlock(&osb->osb_lock);

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
	}

	status = 0;
bail:
	return status;
}

/*
 * Scan timer should get fired every ORPHAN_SCAN_SCHEDULE_TIMEOUT. Add some
 * randomness to the timeout to minimize multple nodes firing the timer at the
 * same time.
 */
static inline unsigned long ocfs2_orphan_scan_timeout(void)
{
	unsigned long time;

	get_random_bytes(&time, sizeof(time));
	time = ORPHAN_SCAN_SCHEDULE_TIMEOUT + (time % 5000);
	return msecs_to_jiffies(time);
}

/*
 * ocfs2_queue_orphan_scan calls ocfs2_queue_recovery_completion for
 * every slot, queuing a recovery of the slot on the ocfs2_wq thread. This
 * is done to catch any orphans that are left over in orphan directories.
 *
 * It scans all slots, even ones that are in use. It does so to handle the
 * case described below:
 *
 *   Node 1 has an inode it was using. The dentry went away due to memory
 *   pressure.  Node 1 closes the inode, but it's on the free list. The node
 *   has the open lock.
 *   Node 2 unlinks the inode. It grabs the dentry lock to notify others,
 *   but node 1 has no dentry and doesn't get the message. It trylocks the
 *   open lock, sees that another node has a PR, and does nothing.
 *   Later node 2 runs its orphan dir. It igets the inode, trylocks the
 *   open lock, sees the PR still, and does nothing.
 *   Basically, we have to trigger an orphan iput on node 1. The only way
 *   for this to happen is if node 1 runs node 2's orphan dir.
 *
 * ocfs2_queue_orphan_scan gets called every ORPHAN_SCAN_SCHEDULE_TIMEOUT
 * seconds.  It gets an EX lock on os_lockres and checks sequence number
 * stored in LVB. If the sequence number has changed, it means some other
 * node has done the scan.  This node skips the scan and tracks the
 * sequence number.  If the sequence number didn't change, it means a scan
 * hasn't happened.  The node queues a scan and increments the
 * sequence number in the LVB.
 */
void ocfs2_queue_orphan_scan(struct ocfs2_super *osb)
{
	struct ocfs2_orphan_scan *os;
	int status, i;
	u32 seqno = 0;

	os = &osb->osb_orphan_scan;

	if (atomic_read(&os->os_state) == ORPHAN_SCAN_INACTIVE)
		goto out;

	trace_ocfs2_queue_orphan_scan_begin(os->os_count, os->os_seqno,
					    atomic_read(&os->os_state));

	status = ocfs2_orphan_scan_lock(osb, &seqno);
	if (status < 0) {
		if (status != -EAGAIN)
			mlog_errno(status);
		goto out;
	}

	/* Do no queue the tasks if the volume is being umounted */
	if (atomic_read(&os->os_state) == ORPHAN_SCAN_INACTIVE)
		goto unlock;

	if (os->os_seqno != seqno) {
		os->os_seqno = seqno;
		goto unlock;
	}

	for (i = 0; i < osb->max_slots; i++)
		ocfs2_queue_recovery_completion(osb->journal, i, NULL, NULL,
						NULL);
	/*
	 * We queued a recovery on orphan slots, increment the sequence
	 * number and update LVB so other node will skip the scan for a while
	 */
	seqno++;
	os->os_count++;
	os->os_scantime = CURRENT_TIME;
unlock:
	ocfs2_orphan_scan_unlock(osb, seqno);
out:
	trace_ocfs2_queue_orphan_scan_end(os->os_count, os->os_seqno,
					  atomic_read(&os->os_state));
	return;
}

/* Worker task that gets fired every ORPHAN_SCAN_SCHEDULE_TIMEOUT millsec */
void ocfs2_orphan_scan_work(struct work_struct *work)
{
	struct ocfs2_orphan_scan *os;
	struct ocfs2_super *osb;

	os = container_of(work, struct ocfs2_orphan_scan,
			  os_orphan_scan_work.work);
	osb = os->os_osb;

	mutex_lock(&os->os_lock);
	ocfs2_queue_orphan_scan(osb);
	if (atomic_read(&os->os_state) == ORPHAN_SCAN_ACTIVE)
		queue_delayed_work(ocfs2_wq, &os->os_orphan_scan_work,
				      ocfs2_orphan_scan_timeout());
	mutex_unlock(&os->os_lock);
}

void ocfs2_orphan_scan_stop(struct ocfs2_super *osb)
{
	struct ocfs2_orphan_scan *os;

	os = &osb->osb_orphan_scan;
	if (atomic_read(&os->os_state) == ORPHAN_SCAN_ACTIVE) {
		atomic_set(&os->os_state, ORPHAN_SCAN_INACTIVE);
		mutex_lock(&os->os_lock);
		cancel_delayed_work(&os->os_orphan_scan_work);
		mutex_unlock(&os->os_lock);
	}
}

void ocfs2_orphan_scan_init(struct ocfs2_super *osb)
{
	struct ocfs2_orphan_scan *os;

	os = &osb->osb_orphan_scan;
	os->os_osb = osb;
	os->os_count = 0;
	os->os_seqno = 0;
	mutex_init(&os->os_lock);
	INIT_DELAYED_WORK(&os->os_orphan_scan_work, ocfs2_orphan_scan_work);
}

void ocfs2_orphan_scan_start(struct ocfs2_super *osb)
{
	struct ocfs2_orphan_scan *os;

	os = &osb->osb_orphan_scan;
	os->os_scantime = CURRENT_TIME;
	if (ocfs2_is_hard_readonly(osb) || ocfs2_mount_local(osb))
		atomic_set(&os->os_state, ORPHAN_SCAN_INACTIVE);
	else {
		atomic_set(&os->os_state, ORPHAN_SCAN_ACTIVE);
		queue_delayed_work(ocfs2_wq, &os->os_orphan_scan_work,
				   ocfs2_orphan_scan_timeout());
	}
}

struct ocfs2_orphan_filldir_priv {
	struct inode		*head;
	struct ocfs2_super	*osb;
};

static int ocfs2_orphan_filldir(void *priv, const char *name, int name_len,
				loff_t pos, u64 ino, unsigned type)
{
	struct ocfs2_orphan_filldir_priv *p = priv;
	struct inode *iter;

	if (name_len == 1 && !strncmp(".", name, 1))
		return 0;
	if (name_len == 2 && !strncmp("..", name, 2))
		return 0;

	/* Skip bad inodes so that recovery can continue */
	iter = ocfs2_iget(p->osb, ino,
			  OCFS2_FI_FLAG_ORPHAN_RECOVERY, 0);
	if (IS_ERR(iter))
		return 0;

	trace_ocfs2_orphan_filldir((unsigned long long)OCFS2_I(iter)->ip_blkno);
	/* No locking is required for the next_orphan queue as there
	 * is only ever a single process doing orphan recovery. */
	OCFS2_I(iter)->ip_next_orphan = p->head;
	p->head = iter;

	return 0;
}

static int ocfs2_queue_orphans(struct ocfs2_super *osb,
			       int slot,
			       struct inode **head)
{
	int status;
	struct inode *orphan_dir_inode = NULL;
	struct ocfs2_orphan_filldir_priv priv;
	loff_t pos = 0;

	priv.osb = osb;
	priv.head = *head;

	orphan_dir_inode = ocfs2_get_system_file_inode(osb,
						       ORPHAN_DIR_SYSTEM_INODE,
						       slot);
	if  (!orphan_dir_inode) {
		status = -ENOENT;
		mlog_errno(status);
		return status;
	}

	mutex_lock(&orphan_dir_inode->i_mutex);
	status = ocfs2_inode_lock(orphan_dir_inode, NULL, 0);
	if (status < 0) {
		mlog_errno(status);
		goto out;
	}

	status = ocfs2_dir_foreach(orphan_dir_inode, &pos, &priv,
				   ocfs2_orphan_filldir);
	if (status) {
		mlog_errno(status);
		goto out_cluster;
	}

	*head = priv.head;

out_cluster:
	ocfs2_inode_unlock(orphan_dir_inode, 0);
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

	trace_ocfs2_recover_orphans(slot);

	ocfs2_mark_recovering_orphan_dir(osb, slot);
	ret = ocfs2_queue_orphans(osb, slot, &inode);
	ocfs2_clear_recovering_orphan_dir(osb, slot);

	/* Error here should be noted, but we want to continue with as
	 * many queued inodes as we've got. */
	if (ret)
		mlog_errno(ret);

	while (inode) {
		oi = OCFS2_I(inode);
		trace_ocfs2_recover_orphans_iput(
					(unsigned long long)oi->ip_blkno);

		iter = oi->ip_next_orphan;

		spin_lock(&oi->ip_lock);
		/* The remote delete code may have set these on the
		 * assumption that the other node would wipe them
		 * successfully.  If they are still in the node's
		 * orphan dir, we need to reset that state. */
		oi->ip_flags &= ~(OCFS2_INODE_DELETED|OCFS2_INODE_SKIP_DELETE);

		/* Set the proper information to get us going into
		 * ocfs2_delete_inode. */
		oi->ip_flags |= OCFS2_INODE_MAYBE_ORPHANED;
		spin_unlock(&oi->ip_lock);

		iput(inode);

		inode = iter;
	}

	return ret;
}

static int __ocfs2_wait_on_mount(struct ocfs2_super *osb, int quota)
{
	/* This check is good because ocfs2 will wait on our recovery
	 * thread before changing it to something other than MOUNTED
	 * or DISABLED. */
	wait_event(osb->osb_mount_event,
		  (!quota && atomic_read(&osb->vol_state) == VOLUME_MOUNTED) ||
		   atomic_read(&osb->vol_state) == VOLUME_MOUNTED_QUOTAS ||
		   atomic_read(&osb->vol_state) == VOLUME_DISABLED);

	/* If there's an error on mount, then we may never get to the
	 * MOUNTED flag, but this is set right before
	 * dismount_volume() so we can trust it. */
	if (atomic_read(&osb->vol_state) == VOLUME_DISABLED) {
		trace_ocfs2_wait_on_mount(VOLUME_DISABLED);
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

/* Reads all the journal inodes without taking any cluster locks. Used
 * for hard readonly access to determine whether any journal requires
 * recovery. Also used to refresh the recovery generation numbers after
 * a journal has been recovered by another node.
 */
int ocfs2_check_journals_nolocks(struct ocfs2_super *osb)
{
	int ret = 0;
	unsigned int slot;
	struct buffer_head *di_bh = NULL;
	struct ocfs2_dinode *di;
	int journal_dirty = 0;

	for(slot = 0; slot < osb->max_slots; slot++) {
		ret = ocfs2_read_journal_inode(osb, slot, &di_bh, NULL);
		if (ret) {
			mlog_errno(ret);
			goto out;
		}

		di = (struct ocfs2_dinode *) di_bh->b_data;

		osb->slot_recovery_generations[slot] =
					ocfs2_get_recovery_generation(di);

		if (le32_to_cpu(di->id1.journal1.ij_flags) &
		    OCFS2_JOURNAL_DIRTY_FL)
			journal_dirty = 1;

		brelse(di_bh);
		di_bh = NULL;
	}

out:
	if (journal_dirty)
		ret = -EROFS;
	return ret;
}
