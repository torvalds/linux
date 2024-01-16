// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements functions that manage the running of the commit process.
 * Each affected module has its own functions to accomplish their part in the
 * commit and those functions are called here.
 *
 * The commit is the process whereby all updates to the index and LEB properties
 * are written out together and the journal becomes empty. This keeps the
 * file system consistent - at all times the state can be recreated by reading
 * the index and LEB properties and then replaying the journal.
 *
 * The commit is split into two parts named "commit start" and "commit end".
 * During commit start, the commit process has exclusive access to the journal
 * by holding the commit semaphore down for writing. As few I/O operations as
 * possible are performed during commit start, instead the nodes that are to be
 * written are merely identified. During commit end, the commit semaphore is no
 * longer held and the journal is again in operation, allowing users to continue
 * to use the file system while the bulk of the commit I/O is performed. The
 * purpose of this two-step approach is to prevent the commit from causing any
 * latency blips. Note that in any case, the commit does not prevent lookups
 * (as permitted by the TNC mutex), or access to VFS data structures e.g. page
 * cache.
 */

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include "ubifs.h"

/*
 * nothing_to_commit - check if there is nothing to commit.
 * @c: UBIFS file-system description object
 *
 * This is a helper function which checks if there is anything to commit. It is
 * used as an optimization to avoid starting the commit if it is not really
 * necessary. Indeed, the commit operation always assumes flash I/O (e.g.,
 * writing the commit start node to the log), and it is better to avoid doing
 * this unnecessarily. E.g., 'ubifs_sync_fs()' runs the commit, but if there is
 * nothing to commit, it is more optimal to avoid any flash I/O.
 *
 * This function has to be called with @c->commit_sem locked for writing -
 * this function does not take LPT/TNC locks because the @c->commit_sem
 * guarantees that we have exclusive access to the TNC and LPT data structures.
 *
 * This function returns %1 if there is nothing to commit and %0 otherwise.
 */
static int nothing_to_commit(struct ubifs_info *c)
{
	/*
	 * During mounting or remounting from R/O mode to R/W mode we may
	 * commit for various recovery-related reasons.
	 */
	if (c->mounting || c->remounting_rw)
		return 0;

	/*
	 * If the root TNC node is dirty, we definitely have something to
	 * commit.
	 */
	if (c->zroot.znode && ubifs_zn_dirty(c->zroot.znode))
		return 0;

	/*
	 * Even though the TNC is clean, the LPT tree may have dirty nodes. For
	 * example, this may happen if the budgeting subsystem invoked GC to
	 * make some free space, and the GC found an LEB with only dirty and
	 * free space. In this case GC would just change the lprops of this
	 * LEB (by turning all space into free space) and unmap it.
	 */
	if (c->nroot && test_bit(DIRTY_CNODE, &c->nroot->flags))
		return 0;

	ubifs_assert(c, atomic_long_read(&c->dirty_zn_cnt) == 0);
	ubifs_assert(c, c->dirty_pn_cnt == 0);
	ubifs_assert(c, c->dirty_nn_cnt == 0);

	return 1;
}

/**
 * do_commit - commit the journal.
 * @c: UBIFS file-system description object
 *
 * This function implements UBIFS commit. It has to be called with commit lock
 * locked. Returns zero in case of success and a negative error code in case of
 * failure.
 */
static int do_commit(struct ubifs_info *c)
{
	int err, new_ltail_lnum, old_ltail_lnum, i;
	struct ubifs_zbranch zroot;
	struct ubifs_lp_stats lst;

	dbg_cmt("start");
	ubifs_assert(c, !c->ro_media && !c->ro_mount);

	if (c->ro_error) {
		err = -EROFS;
		goto out_up;
	}

	if (nothing_to_commit(c)) {
		up_write(&c->commit_sem);
		err = 0;
		goto out_cancel;
	}

	/* Sync all write buffers (necessary for recovery) */
	for (i = 0; i < c->jhead_cnt; i++) {
		err = ubifs_wbuf_sync(&c->jheads[i].wbuf);
		if (err)
			goto out_up;
	}

	c->cmt_no += 1;
	err = ubifs_gc_start_commit(c);
	if (err)
		goto out_up;
	err = dbg_check_lprops(c);
	if (err)
		goto out_up;
	err = ubifs_log_start_commit(c, &new_ltail_lnum);
	if (err)
		goto out_up;
	err = ubifs_tnc_start_commit(c, &zroot);
	if (err)
		goto out_up;
	err = ubifs_lpt_start_commit(c);
	if (err)
		goto out_up;
	err = ubifs_orphan_start_commit(c);
	if (err)
		goto out_up;

	ubifs_get_lp_stats(c, &lst);

	up_write(&c->commit_sem);

	err = ubifs_tnc_end_commit(c);
	if (err)
		goto out;
	err = ubifs_lpt_end_commit(c);
	if (err)
		goto out;
	err = ubifs_orphan_end_commit(c);
	if (err)
		goto out;
	err = dbg_check_old_index(c, &zroot);
	if (err)
		goto out;

	c->mst_node->cmt_no      = cpu_to_le64(c->cmt_no);
	c->mst_node->log_lnum    = cpu_to_le32(new_ltail_lnum);
	c->mst_node->root_lnum   = cpu_to_le32(zroot.lnum);
	c->mst_node->root_offs   = cpu_to_le32(zroot.offs);
	c->mst_node->root_len    = cpu_to_le32(zroot.len);
	c->mst_node->ihead_lnum  = cpu_to_le32(c->ihead_lnum);
	c->mst_node->ihead_offs  = cpu_to_le32(c->ihead_offs);
	c->mst_node->index_size  = cpu_to_le64(c->bi.old_idx_sz);
	c->mst_node->lpt_lnum    = cpu_to_le32(c->lpt_lnum);
	c->mst_node->lpt_offs    = cpu_to_le32(c->lpt_offs);
	c->mst_node->nhead_lnum  = cpu_to_le32(c->nhead_lnum);
	c->mst_node->nhead_offs  = cpu_to_le32(c->nhead_offs);
	c->mst_node->ltab_lnum   = cpu_to_le32(c->ltab_lnum);
	c->mst_node->ltab_offs   = cpu_to_le32(c->ltab_offs);
	c->mst_node->lsave_lnum  = cpu_to_le32(c->lsave_lnum);
	c->mst_node->lsave_offs  = cpu_to_le32(c->lsave_offs);
	c->mst_node->lscan_lnum  = cpu_to_le32(c->lscan_lnum);
	c->mst_node->empty_lebs  = cpu_to_le32(lst.empty_lebs);
	c->mst_node->idx_lebs    = cpu_to_le32(lst.idx_lebs);
	c->mst_node->total_free  = cpu_to_le64(lst.total_free);
	c->mst_node->total_dirty = cpu_to_le64(lst.total_dirty);
	c->mst_node->total_used  = cpu_to_le64(lst.total_used);
	c->mst_node->total_dead  = cpu_to_le64(lst.total_dead);
	c->mst_node->total_dark  = cpu_to_le64(lst.total_dark);
	if (c->no_orphs)
		c->mst_node->flags |= cpu_to_le32(UBIFS_MST_NO_ORPHS);
	else
		c->mst_node->flags &= ~cpu_to_le32(UBIFS_MST_NO_ORPHS);

	old_ltail_lnum = c->ltail_lnum;
	err = ubifs_log_end_commit(c, new_ltail_lnum);
	if (err)
		goto out;

	err = ubifs_log_post_commit(c, old_ltail_lnum);
	if (err)
		goto out;
	err = ubifs_gc_end_commit(c);
	if (err)
		goto out;
	err = ubifs_lpt_post_commit(c);
	if (err)
		goto out;

out_cancel:
	spin_lock(&c->cs_lock);
	c->cmt_state = COMMIT_RESTING;
	wake_up(&c->cmt_wq);
	dbg_cmt("commit end");
	spin_unlock(&c->cs_lock);
	return 0;

out_up:
	up_write(&c->commit_sem);
out:
	ubifs_err(c, "commit failed, error %d", err);
	spin_lock(&c->cs_lock);
	c->cmt_state = COMMIT_BROKEN;
	wake_up(&c->cmt_wq);
	spin_unlock(&c->cs_lock);
	ubifs_ro_mode(c, err);
	return err;
}

/**
 * run_bg_commit - run background commit if it is needed.
 * @c: UBIFS file-system description object
 *
 * This function runs background commit if it is needed. Returns zero in case
 * of success and a negative error code in case of failure.
 */
static int run_bg_commit(struct ubifs_info *c)
{
	spin_lock(&c->cs_lock);
	/*
	 * Run background commit only if background commit was requested or if
	 * commit is required.
	 */
	if (c->cmt_state != COMMIT_BACKGROUND &&
	    c->cmt_state != COMMIT_REQUIRED)
		goto out;
	spin_unlock(&c->cs_lock);

	down_write(&c->commit_sem);
	spin_lock(&c->cs_lock);
	if (c->cmt_state == COMMIT_REQUIRED)
		c->cmt_state = COMMIT_RUNNING_REQUIRED;
	else if (c->cmt_state == COMMIT_BACKGROUND)
		c->cmt_state = COMMIT_RUNNING_BACKGROUND;
	else
		goto out_cmt_unlock;
	spin_unlock(&c->cs_lock);

	return do_commit(c);

out_cmt_unlock:
	up_write(&c->commit_sem);
out:
	spin_unlock(&c->cs_lock);
	return 0;
}

/**
 * ubifs_bg_thread - UBIFS background thread function.
 * @info: points to the file-system description object
 *
 * This function implements various file-system background activities:
 * o when a write-buffer timer expires it synchronizes the appropriate
 *   write-buffer;
 * o when the journal is about to be full, it starts in-advance commit.
 *
 * Note, other stuff like background garbage collection may be added here in
 * future.
 */
int ubifs_bg_thread(void *info)
{
	int err;
	struct ubifs_info *c = info;

	ubifs_msg(c, "background thread \"%s\" started, PID %d",
		  c->bgt_name, current->pid);
	set_freezable();

	while (1) {
		if (kthread_should_stop())
			break;

		if (try_to_freeze())
			continue;

		set_current_state(TASK_INTERRUPTIBLE);
		/* Check if there is something to do */
		if (!c->need_bgt) {
			/*
			 * Nothing prevents us from going sleep now and
			 * be never woken up and block the task which
			 * could wait in 'kthread_stop()' forever.
			 */
			if (kthread_should_stop())
				break;
			schedule();
			continue;
		} else
			__set_current_state(TASK_RUNNING);

		c->need_bgt = 0;
		err = ubifs_bg_wbufs_sync(c);
		if (err)
			ubifs_ro_mode(c, err);

		run_bg_commit(c);
		cond_resched();
	}

	ubifs_msg(c, "background thread \"%s\" stops", c->bgt_name);
	return 0;
}

/**
 * ubifs_commit_required - set commit state to "required".
 * @c: UBIFS file-system description object
 *
 * This function is called if a commit is required but cannot be done from the
 * calling function, so it is just flagged instead.
 */
void ubifs_commit_required(struct ubifs_info *c)
{
	spin_lock(&c->cs_lock);
	switch (c->cmt_state) {
	case COMMIT_RESTING:
	case COMMIT_BACKGROUND:
		dbg_cmt("old: %s, new: %s", dbg_cstate(c->cmt_state),
			dbg_cstate(COMMIT_REQUIRED));
		c->cmt_state = COMMIT_REQUIRED;
		break;
	case COMMIT_RUNNING_BACKGROUND:
		dbg_cmt("old: %s, new: %s", dbg_cstate(c->cmt_state),
			dbg_cstate(COMMIT_RUNNING_REQUIRED));
		c->cmt_state = COMMIT_RUNNING_REQUIRED;
		break;
	case COMMIT_REQUIRED:
	case COMMIT_RUNNING_REQUIRED:
	case COMMIT_BROKEN:
		break;
	}
	spin_unlock(&c->cs_lock);
}

/**
 * ubifs_request_bg_commit - notify the background thread to do a commit.
 * @c: UBIFS file-system description object
 *
 * This function is called if the journal is full enough to make a commit
 * worthwhile, so background thread is kicked to start it.
 */
void ubifs_request_bg_commit(struct ubifs_info *c)
{
	spin_lock(&c->cs_lock);
	if (c->cmt_state == COMMIT_RESTING) {
		dbg_cmt("old: %s, new: %s", dbg_cstate(c->cmt_state),
			dbg_cstate(COMMIT_BACKGROUND));
		c->cmt_state = COMMIT_BACKGROUND;
		spin_unlock(&c->cs_lock);
		ubifs_wake_up_bgt(c);
	} else
		spin_unlock(&c->cs_lock);
}

/**
 * wait_for_commit - wait for commit.
 * @c: UBIFS file-system description object
 *
 * This function sleeps until the commit operation is no longer running.
 */
static int wait_for_commit(struct ubifs_info *c)
{
	dbg_cmt("pid %d goes sleep", current->pid);

	/*
	 * The following sleeps if the condition is false, and will be woken
	 * when the commit ends. It is possible, although very unlikely, that we
	 * will wake up and see the subsequent commit running, rather than the
	 * one we were waiting for, and go back to sleep.  However, we will be
	 * woken again, so there is no danger of sleeping forever.
	 */
	wait_event(c->cmt_wq, c->cmt_state != COMMIT_RUNNING_BACKGROUND &&
			      c->cmt_state != COMMIT_RUNNING_REQUIRED);
	dbg_cmt("commit finished, pid %d woke up", current->pid);
	return 0;
}

/**
 * ubifs_run_commit - run or wait for commit.
 * @c: UBIFS file-system description object
 *
 * This function runs commit and returns zero in case of success and a negative
 * error code in case of failure.
 */
int ubifs_run_commit(struct ubifs_info *c)
{
	int err = 0;

	spin_lock(&c->cs_lock);
	if (c->cmt_state == COMMIT_BROKEN) {
		err = -EROFS;
		goto out;
	}

	if (c->cmt_state == COMMIT_RUNNING_BACKGROUND)
		/*
		 * We set the commit state to 'running required' to indicate
		 * that we want it to complete as quickly as possible.
		 */
		c->cmt_state = COMMIT_RUNNING_REQUIRED;

	if (c->cmt_state == COMMIT_RUNNING_REQUIRED) {
		spin_unlock(&c->cs_lock);
		return wait_for_commit(c);
	}
	spin_unlock(&c->cs_lock);

	/* Ok, the commit is indeed needed */

	down_write(&c->commit_sem);
	spin_lock(&c->cs_lock);
	/*
	 * Since we unlocked 'c->cs_lock', the state may have changed, so
	 * re-check it.
	 */
	if (c->cmt_state == COMMIT_BROKEN) {
		err = -EROFS;
		goto out_cmt_unlock;
	}

	if (c->cmt_state == COMMIT_RUNNING_BACKGROUND)
		c->cmt_state = COMMIT_RUNNING_REQUIRED;

	if (c->cmt_state == COMMIT_RUNNING_REQUIRED) {
		up_write(&c->commit_sem);
		spin_unlock(&c->cs_lock);
		return wait_for_commit(c);
	}
	c->cmt_state = COMMIT_RUNNING_REQUIRED;
	spin_unlock(&c->cs_lock);

	err = do_commit(c);
	return err;

out_cmt_unlock:
	up_write(&c->commit_sem);
out:
	spin_unlock(&c->cs_lock);
	return err;
}

/**
 * ubifs_gc_should_commit - determine if it is time for GC to run commit.
 * @c: UBIFS file-system description object
 *
 * This function is called by garbage collection to determine if commit should
 * be run. If commit state is @COMMIT_BACKGROUND, which means that the journal
 * is full enough to start commit, this function returns true. It is not
 * absolutely necessary to commit yet, but it feels like this should be better
 * then to keep doing GC. This function returns %1 if GC has to initiate commit
 * and %0 if not.
 */
int ubifs_gc_should_commit(struct ubifs_info *c)
{
	int ret = 0;

	spin_lock(&c->cs_lock);
	if (c->cmt_state == COMMIT_BACKGROUND) {
		dbg_cmt("commit required now");
		c->cmt_state = COMMIT_REQUIRED;
	} else
		dbg_cmt("commit not requested");
	if (c->cmt_state == COMMIT_REQUIRED)
		ret = 1;
	spin_unlock(&c->cs_lock);
	return ret;
}

/*
 * Everything below is related to debugging.
 */

/**
 * struct idx_node - hold index nodes during index tree traversal.
 * @list: list
 * @iip: index in parent (slot number of this indexing node in the parent
 *       indexing node)
 * @upper_key: all keys in this indexing node have to be less or equivalent to
 *             this key
 * @idx: index node (8-byte aligned because all node structures must be 8-byte
 *       aligned)
 */
struct idx_node {
	struct list_head list;
	int iip;
	union ubifs_key upper_key;
	struct ubifs_idx_node idx __aligned(8);
};

/**
 * dbg_old_index_check_init - get information for the next old index check.
 * @c: UBIFS file-system description object
 * @zroot: root of the index
 *
 * This function records information about the index that will be needed for the
 * next old index check i.e. 'dbg_check_old_index()'.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_old_index_check_init(struct ubifs_info *c, struct ubifs_zbranch *zroot)
{
	struct ubifs_idx_node *idx;
	int lnum, offs, len, err = 0;
	struct ubifs_debug_info *d = c->dbg;

	d->old_zroot = *zroot;
	lnum = d->old_zroot.lnum;
	offs = d->old_zroot.offs;
	len = d->old_zroot.len;

	idx = kmalloc(c->max_idx_node_sz, GFP_NOFS);
	if (!idx)
		return -ENOMEM;

	err = ubifs_read_node(c, idx, UBIFS_IDX_NODE, len, lnum, offs);
	if (err)
		goto out;

	d->old_zroot_level = le16_to_cpu(idx->level);
	d->old_zroot_sqnum = le64_to_cpu(idx->ch.sqnum);
out:
	kfree(idx);
	return err;
}

/**
 * dbg_check_old_index - check the old copy of the index.
 * @c: UBIFS file-system description object
 * @zroot: root of the new index
 *
 * In order to be able to recover from an unclean unmount, a complete copy of
 * the index must exist on flash. This is the "old" index. The commit process
 * must write the "new" index to flash without overwriting or destroying any
 * part of the old index. This function is run at commit end in order to check
 * that the old index does indeed exist completely intact.
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int dbg_check_old_index(struct ubifs_info *c, struct ubifs_zbranch *zroot)
{
	int lnum, offs, len, err = 0, last_level, child_cnt;
	int first = 1, iip;
	struct ubifs_debug_info *d = c->dbg;
	union ubifs_key lower_key, upper_key, l_key, u_key;
	unsigned long long last_sqnum;
	struct ubifs_idx_node *idx;
	struct list_head list;
	struct idx_node *i;
	size_t sz;

	if (!dbg_is_chk_index(c))
		return 0;

	INIT_LIST_HEAD(&list);

	sz = sizeof(struct idx_node) + ubifs_idx_node_sz(c, c->fanout) -
	     UBIFS_IDX_NODE_SZ;

	/* Start at the old zroot */
	lnum = d->old_zroot.lnum;
	offs = d->old_zroot.offs;
	len = d->old_zroot.len;
	iip = 0;

	/*
	 * Traverse the index tree preorder depth-first i.e. do a node and then
	 * its subtrees from left to right.
	 */
	while (1) {
		struct ubifs_branch *br;

		/* Get the next index node */
		i = kmalloc(sz, GFP_NOFS);
		if (!i) {
			err = -ENOMEM;
			goto out_free;
		}
		i->iip = iip;
		/* Keep the index nodes on our path in a linked list */
		list_add_tail(&i->list, &list);
		/* Read the index node */
		idx = &i->idx;
		err = ubifs_read_node(c, idx, UBIFS_IDX_NODE, len, lnum, offs);
		if (err)
			goto out_free;
		/* Validate index node */
		child_cnt = le16_to_cpu(idx->child_cnt);
		if (child_cnt < 1 || child_cnt > c->fanout) {
			err = 1;
			goto out_dump;
		}
		if (first) {
			first = 0;
			/* Check root level and sqnum */
			if (le16_to_cpu(idx->level) != d->old_zroot_level) {
				err = 2;
				goto out_dump;
			}
			if (le64_to_cpu(idx->ch.sqnum) != d->old_zroot_sqnum) {
				err = 3;
				goto out_dump;
			}
			/* Set last values as though root had a parent */
			last_level = le16_to_cpu(idx->level) + 1;
			last_sqnum = le64_to_cpu(idx->ch.sqnum) + 1;
			key_read(c, ubifs_idx_key(c, idx), &lower_key);
			highest_ino_key(c, &upper_key, INUM_WATERMARK);
		}
		key_copy(c, &upper_key, &i->upper_key);
		if (le16_to_cpu(idx->level) != last_level - 1) {
			err = 3;
			goto out_dump;
		}
		/*
		 * The index is always written bottom up hence a child's sqnum
		 * is always less than the parents.
		 */
		if (le64_to_cpu(idx->ch.sqnum) >= last_sqnum) {
			err = 4;
			goto out_dump;
		}
		/* Check key range */
		key_read(c, ubifs_idx_key(c, idx), &l_key);
		br = ubifs_idx_branch(c, idx, child_cnt - 1);
		key_read(c, &br->key, &u_key);
		if (keys_cmp(c, &lower_key, &l_key) > 0) {
			err = 5;
			goto out_dump;
		}
		if (keys_cmp(c, &upper_key, &u_key) < 0) {
			err = 6;
			goto out_dump;
		}
		if (keys_cmp(c, &upper_key, &u_key) == 0)
			if (!is_hash_key(c, &u_key)) {
				err = 7;
				goto out_dump;
			}
		/* Go to next index node */
		if (le16_to_cpu(idx->level) == 0) {
			/* At the bottom, so go up until can go right */
			while (1) {
				/* Drop the bottom of the list */
				list_del(&i->list);
				kfree(i);
				/* No more list means we are done */
				if (list_empty(&list))
					goto out;
				/* Look at the new bottom */
				i = list_entry(list.prev, struct idx_node,
					       list);
				idx = &i->idx;
				/* Can we go right */
				if (iip + 1 < le16_to_cpu(idx->child_cnt)) {
					iip = iip + 1;
					break;
				} else
					/* Nope, so go up again */
					iip = i->iip;
			}
		} else
			/* Go down left */
			iip = 0;
		/*
		 * We have the parent in 'idx' and now we set up for reading the
		 * child pointed to by slot 'iip'.
		 */
		last_level = le16_to_cpu(idx->level);
		last_sqnum = le64_to_cpu(idx->ch.sqnum);
		br = ubifs_idx_branch(c, idx, iip);
		lnum = le32_to_cpu(br->lnum);
		offs = le32_to_cpu(br->offs);
		len = le32_to_cpu(br->len);
		key_read(c, &br->key, &lower_key);
		if (iip + 1 < le16_to_cpu(idx->child_cnt)) {
			br = ubifs_idx_branch(c, idx, iip + 1);
			key_read(c, &br->key, &upper_key);
		} else
			key_copy(c, &i->upper_key, &upper_key);
	}
out:
	err = dbg_old_index_check_init(c, zroot);
	if (err)
		goto out_free;

	return 0;

out_dump:
	ubifs_err(c, "dumping index node (iip=%d)", i->iip);
	ubifs_dump_node(c, idx, ubifs_idx_node_sz(c, c->fanout));
	list_del(&i->list);
	kfree(i);
	if (!list_empty(&list)) {
		i = list_entry(list.prev, struct idx_node, list);
		ubifs_err(c, "dumping parent index node");
		ubifs_dump_node(c, &i->idx, ubifs_idx_node_sz(c, c->fanout));
	}
out_free:
	while (!list_empty(&list)) {
		i = list_entry(list.next, struct idx_node, list);
		list_del(&i->list);
		kfree(i);
	}
	ubifs_err(c, "failed, error %d", err);
	if (err > 0)
		err = -EINVAL;
	return err;
}
