// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <linux/sort.h>
#include <linux/hash.h>
#include <linux/jhash.h>
#include <linux/kallsyms.h>
#include <linux/gfs2_ondisk.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/rcupdate.h>
#include <linux/rculist_bl.h>
#include <linux/bit_spinlock.h>
#include <linux/percpu.h>
#include <linux/list_sort.h>
#include <linux/lockref.h>
#include <linux/rhashtable.h>
#include <linux/pid_namespace.h>
#include <linux/file.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "glops.h"
#include "inode.h"
#include "lops.h"
#include "meta_io.h"
#include "quota.h"
#include "super.h"
#include "util.h"
#include "bmap.h"
#define CREATE_TRACE_POINTS
#include "trace_gfs2.h"

struct gfs2_glock_iter {
	struct gfs2_sbd *sdp;		/* incore superblock           */
	struct rhashtable_iter hti;	/* rhashtable iterator         */
	struct gfs2_glock *gl;		/* current glock struct        */
	loff_t last_pos;		/* last position               */
};

typedef void (*glock_examiner) (struct gfs2_glock * gl);

static void do_xmote(struct gfs2_glock *gl, struct gfs2_holder *gh, unsigned int target);
static void request_demote(struct gfs2_glock *gl, unsigned int state,
			   unsigned long delay, bool remote);

static struct dentry *gfs2_root;
static LIST_HEAD(lru_list);
static atomic_t lru_count = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(lru_lock);

#define GFS2_GL_HASH_SHIFT      15
#define GFS2_GL_HASH_SIZE       BIT(GFS2_GL_HASH_SHIFT)

static const struct rhashtable_params ht_parms = {
	.nelem_hint = GFS2_GL_HASH_SIZE * 3 / 4,
	.key_len = offsetofend(struct lm_lockname, ln_type),
	.key_offset = offsetof(struct gfs2_glock, gl_name),
	.head_offset = offsetof(struct gfs2_glock, gl_node),
};

static struct rhashtable gl_hash_table;

#define GLOCK_WAIT_TABLE_BITS 12
#define GLOCK_WAIT_TABLE_SIZE (1 << GLOCK_WAIT_TABLE_BITS)
static wait_queue_head_t glock_wait_table[GLOCK_WAIT_TABLE_SIZE] __cacheline_aligned;

struct wait_glock_queue {
	struct lm_lockname *name;
	wait_queue_entry_t wait;
};

static int glock_wake_function(wait_queue_entry_t *wait, unsigned int mode,
			       int sync, void *key)
{
	struct wait_glock_queue *wait_glock =
		container_of(wait, struct wait_glock_queue, wait);
	struct lm_lockname *wait_name = wait_glock->name;
	struct lm_lockname *wake_name = key;

	if (wake_name->ln_sbd != wait_name->ln_sbd ||
	    wake_name->ln_number != wait_name->ln_number ||
	    wake_name->ln_type != wait_name->ln_type)
		return 0;
	return autoremove_wake_function(wait, mode, sync, key);
}

static wait_queue_head_t *glock_waitqueue(struct lm_lockname *name)
{
	u32 hash = jhash2((u32 *)name, ht_parms.key_len / 4, 0);

	return glock_wait_table + hash_32(hash, GLOCK_WAIT_TABLE_BITS);
}

/**
 * wake_up_glock  -  Wake up waiters on a glock
 * @gl: the glock
 */
static void wake_up_glock(struct gfs2_glock *gl)
{
	wait_queue_head_t *wq = glock_waitqueue(&gl->gl_name);

	if (waitqueue_active(wq))
		__wake_up(wq, TASK_NORMAL, 1, &gl->gl_name);
}

static void gfs2_glock_dealloc(struct rcu_head *rcu)
{
	struct gfs2_glock *gl = container_of(rcu, struct gfs2_glock, gl_rcu);

	kfree(gl->gl_lksb.sb_lvbptr);
	if (gl->gl_ops->go_flags & GLOF_ASPACE) {
		struct gfs2_glock_aspace *gla =
			container_of(gl, struct gfs2_glock_aspace, glock);
		kmem_cache_free(gfs2_glock_aspace_cachep, gla);
	} else
		kmem_cache_free(gfs2_glock_cachep, gl);
}

/**
 * glock_blocked_by_withdraw - determine if we can still use a glock
 * @gl: the glock
 *
 * We need to allow some glocks to be enqueued, dequeued, promoted, and demoted
 * when we're withdrawn. For example, to maintain metadata integrity, we should
 * disallow the use of inode and rgrp glocks when withdrawn. Other glocks like
 * the iopen or freeze glock may be safely used because none of their
 * metadata goes through the journal. So in general, we should disallow all
 * glocks that are journaled, and allow all the others. One exception is:
 * we need to allow our active journal to be promoted and demoted so others
 * may recover it and we can reacquire it when they're done.
 */
static bool glock_blocked_by_withdraw(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	if (!gfs2_withdrawing_or_withdrawn(sdp))
		return false;
	if (gl->gl_ops->go_flags & GLOF_NONDISK)
		return false;
	if (!sdp->sd_jdesc ||
	    gl->gl_name.ln_number == sdp->sd_jdesc->jd_no_addr)
		return false;
	return true;
}

static void __gfs2_glock_free(struct gfs2_glock *gl)
{
	rhashtable_remove_fast(&gl_hash_table, &gl->gl_node, ht_parms);
	smp_mb();
	wake_up_glock(gl);
	call_rcu(&gl->gl_rcu, gfs2_glock_dealloc);
}

void gfs2_glock_free(struct gfs2_glock *gl) {
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	__gfs2_glock_free(gl);
	if (atomic_dec_and_test(&sdp->sd_glock_disposal))
		wake_up(&sdp->sd_kill_wait);
}

void gfs2_glock_free_later(struct gfs2_glock *gl) {
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	spin_lock(&lru_lock);
	list_add(&gl->gl_lru, &sdp->sd_dead_glocks);
	spin_unlock(&lru_lock);
	if (atomic_dec_and_test(&sdp->sd_glock_disposal))
		wake_up(&sdp->sd_kill_wait);
}

static void gfs2_free_dead_glocks(struct gfs2_sbd *sdp)
{
	struct list_head *list = &sdp->sd_dead_glocks;

	while(!list_empty(list)) {
		struct gfs2_glock *gl;

		gl = list_first_entry(list, struct gfs2_glock, gl_lru);
		list_del_init(&gl->gl_lru);
		__gfs2_glock_free(gl);
	}
}

/**
 * gfs2_glock_hold() - increment reference count on glock
 * @gl: The glock to hold
 *
 */

struct gfs2_glock *gfs2_glock_hold(struct gfs2_glock *gl)
{
	GLOCK_BUG_ON(gl, __lockref_is_dead(&gl->gl_lockref));
	lockref_get(&gl->gl_lockref);
	return gl;
}

static void gfs2_glock_add_to_lru(struct gfs2_glock *gl)
{
	spin_lock(&lru_lock);
	list_move_tail(&gl->gl_lru, &lru_list);

	if (!test_bit(GLF_LRU, &gl->gl_flags)) {
		set_bit(GLF_LRU, &gl->gl_flags);
		atomic_inc(&lru_count);
	}

	spin_unlock(&lru_lock);
}

static void gfs2_glock_remove_from_lru(struct gfs2_glock *gl)
{
	spin_lock(&lru_lock);
	if (test_bit(GLF_LRU, &gl->gl_flags)) {
		list_del_init(&gl->gl_lru);
		atomic_dec(&lru_count);
		clear_bit(GLF_LRU, &gl->gl_flags);
	}
	spin_unlock(&lru_lock);
}

/*
 * Enqueue the glock on the work queue.  Passes one glock reference on to the
 * work queue.
 */
static void gfs2_glock_queue_work(struct gfs2_glock *gl, unsigned long delay) {
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	if (!queue_delayed_work(sdp->sd_glock_wq, &gl->gl_work, delay)) {
		/*
		 * We are holding the lockref spinlock, and the work was still
		 * queued above.  The queued work (glock_work_func) takes that
		 * spinlock before dropping its glock reference(s), so it
		 * cannot have dropped them in the meantime.
		 */
		GLOCK_BUG_ON(gl, gl->gl_lockref.count < 2);
		gl->gl_lockref.count--;
	}
}

static void __gfs2_glock_put(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct address_space *mapping = gfs2_glock2aspace(gl);

	lockref_mark_dead(&gl->gl_lockref);
	spin_unlock(&gl->gl_lockref.lock);
	gfs2_glock_remove_from_lru(gl);
	GLOCK_BUG_ON(gl, !list_empty(&gl->gl_holders));
	if (mapping) {
		truncate_inode_pages_final(mapping);
		if (!gfs2_withdrawing_or_withdrawn(sdp))
			GLOCK_BUG_ON(gl, !mapping_empty(mapping));
	}
	trace_gfs2_glock_put(gl);
	sdp->sd_lockstruct.ls_ops->lm_put_lock(gl);
}

static bool __gfs2_glock_put_or_lock(struct gfs2_glock *gl)
{
	if (lockref_put_or_lock(&gl->gl_lockref))
		return true;
	GLOCK_BUG_ON(gl, gl->gl_lockref.count != 1);
	if (gl->gl_state != LM_ST_UNLOCKED) {
		gl->gl_lockref.count--;
		gfs2_glock_add_to_lru(gl);
		spin_unlock(&gl->gl_lockref.lock);
		return true;
	}
	return false;
}

/**
 * gfs2_glock_put() - Decrement reference count on glock
 * @gl: The glock to put
 *
 */

void gfs2_glock_put(struct gfs2_glock *gl)
{
	if (__gfs2_glock_put_or_lock(gl))
		return;

	__gfs2_glock_put(gl);
}

/*
 * gfs2_glock_put_async - Decrement reference count without sleeping
 * @gl: The glock to put
 *
 * Decrement the reference count on glock immediately unless it is the last
 * reference.  Defer putting the last reference to work queue context.
 */
void gfs2_glock_put_async(struct gfs2_glock *gl)
{
	if (__gfs2_glock_put_or_lock(gl))
		return;

	gfs2_glock_queue_work(gl, 0);
	spin_unlock(&gl->gl_lockref.lock);
}

/**
 * may_grant - check if it's ok to grant a new lock
 * @gl: The glock
 * @current_gh: One of the current holders of @gl
 * @gh: The lock request which we wish to grant
 *
 * With our current compatibility rules, if a glock has one or more active
 * holders (HIF_HOLDER flag set), any of those holders can be passed in as
 * @current_gh; they are all the same as far as compatibility with the new @gh
 * goes.
 *
 * Returns true if it's ok to grant the lock.
 */

static inline bool may_grant(struct gfs2_glock *gl,
			     struct gfs2_holder *current_gh,
			     struct gfs2_holder *gh)
{
	if (current_gh) {
		GLOCK_BUG_ON(gl, !test_bit(HIF_HOLDER, &current_gh->gh_iflags));

		switch(current_gh->gh_state) {
		case LM_ST_EXCLUSIVE:
			/*
			 * Here we make a special exception to grant holders
			 * who agree to share the EX lock with other holders
			 * who also have the bit set. If the original holder
			 * has the LM_FLAG_NODE_SCOPE bit set, we grant more
			 * holders with the bit set.
			 */
			return gh->gh_state == LM_ST_EXCLUSIVE &&
			       (current_gh->gh_flags & LM_FLAG_NODE_SCOPE) &&
			       (gh->gh_flags & LM_FLAG_NODE_SCOPE);

		case LM_ST_SHARED:
		case LM_ST_DEFERRED:
			return gh->gh_state == current_gh->gh_state;

		default:
			return false;
		}
	}

	if (gl->gl_state == gh->gh_state)
		return true;
	if (gh->gh_flags & GL_EXACT)
		return false;
	if (gl->gl_state == LM_ST_EXCLUSIVE) {
		return gh->gh_state == LM_ST_SHARED ||
		       gh->gh_state == LM_ST_DEFERRED;
	}
	if (gh->gh_flags & LM_FLAG_ANY)
		return gl->gl_state != LM_ST_UNLOCKED;
	return false;
}

static void gfs2_holder_wake(struct gfs2_holder *gh)
{
	clear_bit(HIF_WAIT, &gh->gh_iflags);
	smp_mb__after_atomic();
	wake_up_bit(&gh->gh_iflags, HIF_WAIT);
	if (gh->gh_flags & GL_ASYNC) {
		struct gfs2_sbd *sdp = gh->gh_gl->gl_name.ln_sbd;

		wake_up(&sdp->sd_async_glock_wait);
	}
}

/**
 * do_error - Something unexpected has happened during a lock request
 * @gl: The glock
 * @ret: The status from the DLM
 */

static void do_error(struct gfs2_glock *gl, const int ret)
{
	struct gfs2_holder *gh, *tmp;

	list_for_each_entry_safe(gh, tmp, &gl->gl_holders, gh_list) {
		if (test_bit(HIF_HOLDER, &gh->gh_iflags))
			continue;
		if (ret & LM_OUT_ERROR)
			gh->gh_error = -EIO;
		else if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB))
			gh->gh_error = GLR_TRYFAILED;
		else
			continue;
		list_del_init(&gh->gh_list);
		trace_gfs2_glock_queue(gh, 0);
		gfs2_holder_wake(gh);
	}
}

/**
 * find_first_holder - find the first "holder" gh
 * @gl: the glock
 */

static inline struct gfs2_holder *find_first_holder(const struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;

	if (!list_empty(&gl->gl_holders)) {
		gh = list_first_entry(&gl->gl_holders, struct gfs2_holder,
				      gh_list);
		if (test_bit(HIF_HOLDER, &gh->gh_iflags))
			return gh;
	}
	return NULL;
}

/*
 * gfs2_instantiate - Call the glops instantiate function
 * @gh: The glock holder
 *
 * Returns: 0 if instantiate was successful, or error.
 */
int gfs2_instantiate(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	int ret;

again:
	if (!test_bit(GLF_INSTANTIATE_NEEDED, &gl->gl_flags))
		goto done;

	/*
	 * Since we unlock the lockref lock, we set a flag to indicate
	 * instantiate is in progress.
	 */
	if (test_and_set_bit(GLF_INSTANTIATE_IN_PROG, &gl->gl_flags)) {
		wait_on_bit(&gl->gl_flags, GLF_INSTANTIATE_IN_PROG,
			    TASK_UNINTERRUPTIBLE);
		/*
		 * Here we just waited for a different instantiate to finish.
		 * But that may not have been successful, as when a process
		 * locks an inode glock _before_ it has an actual inode to
		 * instantiate into. So we check again. This process might
		 * have an inode to instantiate, so might be successful.
		 */
		goto again;
	}

	ret = glops->go_instantiate(gl);
	if (!ret)
		clear_bit(GLF_INSTANTIATE_NEEDED, &gl->gl_flags);
	clear_and_wake_up_bit(GLF_INSTANTIATE_IN_PROG, &gl->gl_flags);
	if (ret)
		return ret;

done:
	if (glops->go_held)
		return glops->go_held(gh);
	return 0;
}

/**
 * do_promote - promote as many requests as possible on the current queue
 * @gl: The glock
 * 
 * Returns true on success (i.e., progress was made or there are no waiters).
 */

static bool do_promote(struct gfs2_glock *gl)
{
	struct gfs2_holder *gh, *current_gh;

	current_gh = find_first_holder(gl);
	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		if (test_bit(HIF_HOLDER, &gh->gh_iflags))
			continue;
		if (!may_grant(gl, current_gh, gh)) {
			/*
			 * If we get here, it means we may not grant this
			 * holder for some reason. If this holder is at the
			 * head of the list, it means we have a blocked holder
			 * at the head, so return false.
			 */
			if (list_is_first(&gh->gh_list, &gl->gl_holders))
				return false;
			do_error(gl, 0);
			break;
		}
		set_bit(HIF_HOLDER, &gh->gh_iflags);
		trace_gfs2_promote(gh);
		gfs2_holder_wake(gh);
		if (!current_gh)
			current_gh = gh;
	}
	return true;
}

/**
 * find_first_waiter - find the first gh that's waiting for the glock
 * @gl: the glock
 */

static inline struct gfs2_holder *find_first_waiter(const struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;

	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		if (!test_bit(HIF_HOLDER, &gh->gh_iflags))
			return gh;
	}
	return NULL;
}

/**
 * find_last_waiter - find the last gh that's waiting for the glock
 * @gl: the glock
 *
 * This also is a fast way of finding out if there are any waiters.
 */

static inline struct gfs2_holder *find_last_waiter(const struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;

	if (list_empty(&gl->gl_holders))
		return NULL;
	gh = list_last_entry(&gl->gl_holders, struct gfs2_holder, gh_list);
	return test_bit(HIF_HOLDER, &gh->gh_iflags) ? NULL : gh;
}

/**
 * state_change - record that the glock is now in a different state
 * @gl: the glock
 * @new_state: the new state
 */

static void state_change(struct gfs2_glock *gl, unsigned int new_state)
{
	if (new_state != gl->gl_target)
		/* shorten our minimum hold time */
		gl->gl_hold_time = max(gl->gl_hold_time - GL_GLOCK_HOLD_DECR,
				       GL_GLOCK_MIN_HOLD);
	gl->gl_state = new_state;
	gl->gl_tchange = jiffies;
}

static void gfs2_set_demote(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	set_bit(GLF_DEMOTE, &gl->gl_flags);
	smp_mb();
	wake_up(&sdp->sd_async_glock_wait);
}

static void gfs2_demote_wake(struct gfs2_glock *gl)
{
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
	clear_bit(GLF_DEMOTE, &gl->gl_flags);
	smp_mb__after_atomic();
	wake_up_bit(&gl->gl_flags, GLF_DEMOTE);
}

/**
 * finish_xmote - The DLM has replied to one of our lock requests
 * @gl: The glock
 * @ret: The status from the DLM
 *
 */

static void finish_xmote(struct gfs2_glock *gl, unsigned int ret)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_holder *gh;
	unsigned state = ret & LM_OUT_ST_MASK;

	trace_gfs2_glock_state_change(gl, state);
	state_change(gl, state);
	gh = find_first_waiter(gl);

	/* Demote to UN request arrived during demote to SH or DF */
	if (test_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags) &&
	    state != LM_ST_UNLOCKED && gl->gl_demote_state == LM_ST_UNLOCKED)
		gl->gl_target = LM_ST_UNLOCKED;

	/* Check for state != intended state */
	if (unlikely(state != gl->gl_target)) {
		if (gh && (ret & LM_OUT_CANCELED))
			gfs2_holder_wake(gh);
		if (gh && !test_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags)) {
			/* move to back of queue and try next entry */
			if (ret & LM_OUT_CANCELED) {
				list_move_tail(&gh->gh_list, &gl->gl_holders);
				gh = find_first_waiter(gl);
				gl->gl_target = gh->gh_state;
				if (do_promote(gl))
					goto out;
				goto retry;
			}
			/* Some error or failed "try lock" - report it */
			if ((ret & LM_OUT_ERROR) ||
			    (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB))) {
				gl->gl_target = gl->gl_state;
				do_error(gl, ret);
				goto out;
			}
		}
		switch(state) {
		/* Unlocked due to conversion deadlock, try again */
		case LM_ST_UNLOCKED:
retry:
			do_xmote(gl, gh, gl->gl_target);
			break;
		/* Conversion fails, unlock and try again */
		case LM_ST_SHARED:
		case LM_ST_DEFERRED:
			do_xmote(gl, gh, LM_ST_UNLOCKED);
			break;
		default: /* Everything else */
			fs_err(gl->gl_name.ln_sbd, "wanted %u got %u\n",
			       gl->gl_target, state);
			GLOCK_BUG_ON(gl, 1);
		}
		return;
	}

	/* Fast path - we got what we asked for */
	if (test_and_clear_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags))
		gfs2_demote_wake(gl);
	if (state != LM_ST_UNLOCKED) {
		if (glops->go_xmote_bh) {
			int rv;

			spin_unlock(&gl->gl_lockref.lock);
			rv = glops->go_xmote_bh(gl);
			spin_lock(&gl->gl_lockref.lock);
			if (rv) {
				do_error(gl, rv);
				goto out;
			}
		}
		do_promote(gl);
	}
out:
	clear_bit(GLF_LOCK, &gl->gl_flags);
}

static bool is_system_glock(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct gfs2_inode *m_ip = GFS2_I(sdp->sd_statfs_inode);

	if (gl == m_ip->i_gl)
		return true;
	return false;
}

/**
 * do_xmote - Calls the DLM to change the state of a lock
 * @gl: The lock state
 * @gh: The holder (only for promotes)
 * @target: The target lock state
 *
 */

static void do_xmote(struct gfs2_glock *gl, struct gfs2_holder *gh,
					 unsigned int target)
__releases(&gl->gl_lockref.lock)
__acquires(&gl->gl_lockref.lock)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct lm_lockstruct *ls = &sdp->sd_lockstruct;
	unsigned int lck_flags = (unsigned int)(gh ? gh->gh_flags : 0);
	int ret;

	if (target != LM_ST_UNLOCKED && glock_blocked_by_withdraw(gl) &&
	    gh && !(gh->gh_flags & LM_FLAG_NOEXP))
		goto skip_inval;

	lck_flags &= (LM_FLAG_TRY | LM_FLAG_TRY_1CB | LM_FLAG_NOEXP);
	GLOCK_BUG_ON(gl, gl->gl_state == target);
	GLOCK_BUG_ON(gl, gl->gl_state == gl->gl_target);
	if ((target == LM_ST_UNLOCKED || target == LM_ST_DEFERRED) &&
	    glops->go_inval) {
		/*
		 * If another process is already doing the invalidate, let that
		 * finish first.  The glock state machine will get back to this
		 * holder again later.
		 */
		if (test_and_set_bit(GLF_INVALIDATE_IN_PROGRESS,
				     &gl->gl_flags))
			return;
		do_error(gl, 0); /* Fail queued try locks */
	}
	gl->gl_req = target;
	set_bit(GLF_BLOCKING, &gl->gl_flags);
	if ((gl->gl_req == LM_ST_UNLOCKED) ||
	    (gl->gl_state == LM_ST_EXCLUSIVE) ||
	    (lck_flags & (LM_FLAG_TRY|LM_FLAG_TRY_1CB)))
		clear_bit(GLF_BLOCKING, &gl->gl_flags);
	if (!glops->go_inval && !glops->go_sync)
		goto skip_inval;

	spin_unlock(&gl->gl_lockref.lock);
	if (glops->go_sync) {
		ret = glops->go_sync(gl);
		/* If we had a problem syncing (due to io errors or whatever,
		 * we should not invalidate the metadata or tell dlm to
		 * release the glock to other nodes.
		 */
		if (ret) {
			if (cmpxchg(&sdp->sd_log_error, 0, ret)) {
				fs_err(sdp, "Error %d syncing glock \n", ret);
				gfs2_dump_glock(NULL, gl, true);
			}
			spin_lock(&gl->gl_lockref.lock);
			goto skip_inval;
		}
	}
	if (test_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags)) {
		/*
		 * The call to go_sync should have cleared out the ail list.
		 * If there are still items, we have a problem. We ought to
		 * withdraw, but we can't because the withdraw code also uses
		 * glocks. Warn about the error, dump the glock, then fall
		 * through and wait for logd to do the withdraw for us.
		 */
		if ((atomic_read(&gl->gl_ail_count) != 0) &&
		    (!cmpxchg(&sdp->sd_log_error, 0, -EIO))) {
			gfs2_glock_assert_warn(gl,
					       !atomic_read(&gl->gl_ail_count));
			gfs2_dump_glock(NULL, gl, true);
		}
		glops->go_inval(gl, target == LM_ST_DEFERRED ? 0 : DIO_METADATA);
		clear_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags);
	}
	spin_lock(&gl->gl_lockref.lock);

skip_inval:
	gl->gl_lockref.count++;
	/*
	 * Check for an error encountered since we called go_sync and go_inval.
	 * If so, we can't withdraw from the glock code because the withdraw
	 * code itself uses glocks (see function signal_our_withdraw) to
	 * change the mount to read-only. Most importantly, we must not call
	 * dlm to unlock the glock until the journal is in a known good state
	 * (after journal replay) otherwise other nodes may use the object
	 * (rgrp or dinode) and then later, journal replay will corrupt the
	 * file system. The best we can do here is wait for the logd daemon
	 * to see sd_log_error and withdraw, and in the meantime, requeue the
	 * work for later.
	 *
	 * We make a special exception for some system glocks, such as the
	 * system statfs inode glock, which needs to be granted before the
	 * gfs2_quotad daemon can exit, and that exit needs to finish before
	 * we can unmount the withdrawn file system.
	 *
	 * However, if we're just unlocking the lock (say, for unmount, when
	 * gfs2_gl_hash_clear calls clear_glock) and recovery is complete
	 * then it's okay to tell dlm to unlock it.
	 */
	if (unlikely(sdp->sd_log_error) && !gfs2_withdrawing_or_withdrawn(sdp))
		gfs2_withdraw_delayed(sdp);
	if (glock_blocked_by_withdraw(gl) &&
	    (target != LM_ST_UNLOCKED ||
	     test_bit(SDF_WITHDRAW_RECOVERY, &sdp->sd_flags))) {
		if (!is_system_glock(gl)) {
			request_demote(gl, LM_ST_UNLOCKED, 0, false);
			/*
			 * Ordinarily, we would call dlm and its callback would call
			 * finish_xmote, which would call state_change() to the new state.
			 * Since we withdrew, we won't call dlm, so call state_change
			 * manually, but to the UNLOCKED state we desire.
			 */
			state_change(gl, LM_ST_UNLOCKED);
			/*
			 * We skip telling dlm to do the locking, so we won't get a
			 * reply that would otherwise clear GLF_LOCK. So we clear it here.
			 */
			clear_bit(GLF_LOCK, &gl->gl_flags);
			clear_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags);
			gfs2_glock_queue_work(gl, GL_GLOCK_DFT_HOLD);
			return;
		} else {
			clear_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags);
		}
	}

	if (ls->ls_ops->lm_lock) {
		spin_unlock(&gl->gl_lockref.lock);
		ret = ls->ls_ops->lm_lock(gl, target, lck_flags);
		spin_lock(&gl->gl_lockref.lock);

		if (ret == -EINVAL && gl->gl_target == LM_ST_UNLOCKED &&
		    target == LM_ST_UNLOCKED &&
		    test_bit(DFL_UNMOUNT, &ls->ls_recover_flags)) {
			/*
			 * The lockspace has been released and the lock has
			 * been unlocked implicitly.
			 */
		} else if (ret) {
			fs_err(sdp, "lm_lock ret %d\n", ret);
			target = gl->gl_state | LM_OUT_ERROR;
		} else {
			/* The operation will be completed asynchronously. */
			return;
		}
	}

	/* Complete the operation now. */
	finish_xmote(gl, target);
	gfs2_glock_queue_work(gl, 0);
}

/**
 * run_queue - do all outstanding tasks related to a glock
 * @gl: The glock in question
 * @nonblock: True if we must not block in run_queue
 *
 */

static void run_queue(struct gfs2_glock *gl, const int nonblock)
__releases(&gl->gl_lockref.lock)
__acquires(&gl->gl_lockref.lock)
{
	struct gfs2_holder *gh = NULL;

	if (test_bit(GLF_LOCK, &gl->gl_flags))
		return;
	set_bit(GLF_LOCK, &gl->gl_flags);

	GLOCK_BUG_ON(gl, test_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags));

	if (test_bit(GLF_DEMOTE, &gl->gl_flags) &&
	    gl->gl_demote_state != gl->gl_state) {
		if (find_first_holder(gl))
			goto out_unlock;
		if (nonblock)
			goto out_sched;
		set_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags);
		GLOCK_BUG_ON(gl, gl->gl_demote_state == LM_ST_EXCLUSIVE);
		gl->gl_target = gl->gl_demote_state;
	} else {
		if (test_bit(GLF_DEMOTE, &gl->gl_flags))
			gfs2_demote_wake(gl);
		if (do_promote(gl))
			goto out_unlock;
		gh = find_first_waiter(gl);
		gl->gl_target = gh->gh_state;
		if (!(gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)))
			do_error(gl, 0); /* Fail queued try locks */
	}
	do_xmote(gl, gh, gl->gl_target);
	return;

out_sched:
	clear_bit(GLF_LOCK, &gl->gl_flags);
	smp_mb__after_atomic();
	gl->gl_lockref.count++;
	gfs2_glock_queue_work(gl, 0);
	return;

out_unlock:
	clear_bit(GLF_LOCK, &gl->gl_flags);
	smp_mb__after_atomic();
}

/**
 * glock_set_object - set the gl_object field of a glock
 * @gl: the glock
 * @object: the object
 */
void glock_set_object(struct gfs2_glock *gl, void *object)
{
	void *prev_object;

	spin_lock(&gl->gl_lockref.lock);
	prev_object = gl->gl_object;
	gl->gl_object = object;
	spin_unlock(&gl->gl_lockref.lock);
	if (gfs2_assert_warn(gl->gl_name.ln_sbd, prev_object == NULL)) {
		pr_warn("glock=%u/%llx\n",
			gl->gl_name.ln_type,
			(unsigned long long)gl->gl_name.ln_number);
		gfs2_dump_glock(NULL, gl, true);
	}
}

/**
 * glock_clear_object - clear the gl_object field of a glock
 * @gl: the glock
 * @object: object the glock currently points at
 */
void glock_clear_object(struct gfs2_glock *gl, void *object)
{
	void *prev_object;

	spin_lock(&gl->gl_lockref.lock);
	prev_object = gl->gl_object;
	gl->gl_object = NULL;
	spin_unlock(&gl->gl_lockref.lock);
	if (gfs2_assert_warn(gl->gl_name.ln_sbd, prev_object == object)) {
		pr_warn("glock=%u/%llx\n",
			gl->gl_name.ln_type,
			(unsigned long long)gl->gl_name.ln_number);
		gfs2_dump_glock(NULL, gl, true);
	}
}

void gfs2_inode_remember_delete(struct gfs2_glock *gl, u64 generation)
{
	struct gfs2_inode_lvb *ri = (void *)gl->gl_lksb.sb_lvbptr;

	if (ri->ri_magic == 0)
		ri->ri_magic = cpu_to_be32(GFS2_MAGIC);
	if (ri->ri_magic == cpu_to_be32(GFS2_MAGIC))
		ri->ri_generation_deleted = cpu_to_be64(generation);
}

bool gfs2_inode_already_deleted(struct gfs2_glock *gl, u64 generation)
{
	struct gfs2_inode_lvb *ri = (void *)gl->gl_lksb.sb_lvbptr;

	if (ri->ri_magic != cpu_to_be32(GFS2_MAGIC))
		return false;
	return generation <= be64_to_cpu(ri->ri_generation_deleted);
}

static void gfs2_glock_poke(struct gfs2_glock *gl)
{
	int flags = LM_FLAG_TRY_1CB | LM_FLAG_ANY | GL_SKIP;
	struct gfs2_holder gh;
	int error;

	__gfs2_holder_init(gl, LM_ST_SHARED, flags, &gh, _RET_IP_);
	error = gfs2_glock_nq(&gh);
	if (!error)
		gfs2_glock_dq(&gh);
	gfs2_holder_uninit(&gh);
}

static bool gfs2_try_evict(struct gfs2_glock *gl)
{
	struct gfs2_inode *ip;
	bool evicted = false;

	/*
	 * If there is contention on the iopen glock and we have an inode, try
	 * to grab and release the inode so that it can be evicted.  This will
	 * allow the remote node to go ahead and delete the inode without us
	 * having to do it, which will avoid rgrp glock thrashing.
	 *
	 * The remote node is likely still holding the corresponding inode
	 * glock, so it will run before we get to verify that the delete has
	 * happened below.
	 */
	spin_lock(&gl->gl_lockref.lock);
	ip = gl->gl_object;
	if (ip && !igrab(&ip->i_inode))
		ip = NULL;
	spin_unlock(&gl->gl_lockref.lock);
	if (ip) {
		gl->gl_no_formal_ino = ip->i_no_formal_ino;
		set_bit(GIF_DEFERRED_DELETE, &ip->i_flags);
		d_prune_aliases(&ip->i_inode);
		iput(&ip->i_inode);

		/* If the inode was evicted, gl->gl_object will now be NULL. */
		spin_lock(&gl->gl_lockref.lock);
		ip = gl->gl_object;
		if (ip) {
			clear_bit(GIF_DEFERRED_DELETE, &ip->i_flags);
			if (!igrab(&ip->i_inode))
				ip = NULL;
		}
		spin_unlock(&gl->gl_lockref.lock);
		if (ip) {
			gfs2_glock_poke(ip->i_gl);
			iput(&ip->i_inode);
		}
		evicted = !ip;
	}
	return evicted;
}

bool gfs2_queue_try_to_evict(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	if (test_and_set_bit(GLF_TRY_TO_EVICT, &gl->gl_flags))
		return false;
	return queue_delayed_work(sdp->sd_delete_wq,
				  &gl->gl_delete, 0);
}

static bool gfs2_queue_verify_evict(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	if (test_and_set_bit(GLF_VERIFY_EVICT, &gl->gl_flags))
		return false;
	return queue_delayed_work(sdp->sd_delete_wq,
				  &gl->gl_delete, 5 * HZ);
}

static void delete_work_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct gfs2_glock *gl = container_of(dwork, struct gfs2_glock, gl_delete);
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct inode *inode;
	u64 no_addr = gl->gl_name.ln_number;

	if (test_and_clear_bit(GLF_TRY_TO_EVICT, &gl->gl_flags)) {
		/*
		 * If we can evict the inode, give the remote node trying to
		 * delete the inode some time before verifying that the delete
		 * has happened.  Otherwise, if we cause contention on the inode glock
		 * immediately, the remote node will think that we still have
		 * the inode in use, and so it will give up waiting.
		 *
		 * If we can't evict the inode, signal to the remote node that
		 * the inode is still in use.  We'll later try to delete the
		 * inode locally in gfs2_evict_inode.
		 *
		 * FIXME: We only need to verify that the remote node has
		 * deleted the inode because nodes before this remote delete
		 * rework won't cooperate.  At a later time, when we no longer
		 * care about compatibility with such nodes, we can skip this
		 * step entirely.
		 */
		if (gfs2_try_evict(gl)) {
			if (test_bit(SDF_KILL, &sdp->sd_flags))
				goto out;
			if (gfs2_queue_verify_evict(gl))
				return;
		}
		goto out;
	}

	if (test_and_clear_bit(GLF_VERIFY_EVICT, &gl->gl_flags)) {
		inode = gfs2_lookup_by_inum(sdp, no_addr, gl->gl_no_formal_ino,
					    GFS2_BLKST_UNLINKED);
		if (IS_ERR(inode)) {
			if (PTR_ERR(inode) == -EAGAIN &&
			    !test_bit(SDF_KILL, &sdp->sd_flags) &&
			    gfs2_queue_verify_evict(gl))
				return;
		} else {
			d_prune_aliases(inode);
			iput(inode);
		}
	}

out:
	gfs2_glock_put(gl);
}

static void glock_work_func(struct work_struct *work)
{
	unsigned long delay = 0;
	struct gfs2_glock *gl = container_of(work, struct gfs2_glock, gl_work.work);
	unsigned int drop_refs = 1;

	spin_lock(&gl->gl_lockref.lock);
	if (test_bit(GLF_HAVE_REPLY, &gl->gl_flags)) {
		clear_bit(GLF_HAVE_REPLY, &gl->gl_flags);
		finish_xmote(gl, gl->gl_reply);
		drop_refs++;
	}
	if (test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
	    gl->gl_state != LM_ST_UNLOCKED &&
	    gl->gl_demote_state != LM_ST_EXCLUSIVE) {
		if (gl->gl_name.ln_type == LM_TYPE_INODE) {
			unsigned long holdtime, now = jiffies;

			holdtime = gl->gl_tchange + gl->gl_hold_time;
			if (time_before(now, holdtime))
				delay = holdtime - now;
		}

		if (!delay) {
			clear_bit(GLF_PENDING_DEMOTE, &gl->gl_flags);
			gfs2_set_demote(gl);
		}
	}
	run_queue(gl, 0);
	if (delay) {
		/* Keep one glock reference for the work we requeue. */
		drop_refs--;
		gfs2_glock_queue_work(gl, delay);
	}

	/* Drop the remaining glock references manually. */
	GLOCK_BUG_ON(gl, gl->gl_lockref.count < drop_refs);
	gl->gl_lockref.count -= drop_refs;
	if (!gl->gl_lockref.count) {
		if (gl->gl_state == LM_ST_UNLOCKED) {
			__gfs2_glock_put(gl);
			return;
		}
		gfs2_glock_add_to_lru(gl);
	}
	spin_unlock(&gl->gl_lockref.lock);
}

static struct gfs2_glock *find_insert_glock(struct lm_lockname *name,
					    struct gfs2_glock *new)
{
	struct wait_glock_queue wait;
	wait_queue_head_t *wq = glock_waitqueue(name);
	struct gfs2_glock *gl;

	wait.name = name;
	init_wait(&wait.wait);
	wait.wait.func = glock_wake_function;

again:
	prepare_to_wait(wq, &wait.wait, TASK_UNINTERRUPTIBLE);
	rcu_read_lock();
	if (new) {
		gl = rhashtable_lookup_get_insert_fast(&gl_hash_table,
			&new->gl_node, ht_parms);
		if (IS_ERR(gl))
			goto out;
	} else {
		gl = rhashtable_lookup_fast(&gl_hash_table,
			name, ht_parms);
	}
	if (gl && !lockref_get_not_dead(&gl->gl_lockref)) {
		rcu_read_unlock();
		schedule();
		goto again;
	}
out:
	rcu_read_unlock();
	finish_wait(wq, &wait.wait);
	if (gl)
		gfs2_glock_remove_from_lru(gl);
	return gl;
}

/**
 * gfs2_glock_get() - Get a glock, or create one if one doesn't exist
 * @sdp: The GFS2 superblock
 * @number: the lock number
 * @glops: The glock_operations to use
 * @create: If 0, don't create the glock if it doesn't exist
 * @glp: the glock is returned here
 *
 * This does not lock a glock, just finds/creates structures for one.
 *
 * Returns: errno
 */

int gfs2_glock_get(struct gfs2_sbd *sdp, u64 number,
		   const struct gfs2_glock_operations *glops, int create,
		   struct gfs2_glock **glp)
{
	struct super_block *s = sdp->sd_vfs;
	struct lm_lockname name = { .ln_number = number,
				    .ln_type = glops->go_type,
				    .ln_sbd = sdp };
	struct gfs2_glock *gl, *tmp;
	struct address_space *mapping;

	gl = find_insert_glock(&name, NULL);
	if (gl)
		goto found;
	if (!create)
		return -ENOENT;

	if (glops->go_flags & GLOF_ASPACE) {
		struct gfs2_glock_aspace *gla =
			kmem_cache_alloc(gfs2_glock_aspace_cachep, GFP_NOFS);
		if (!gla)
			return -ENOMEM;
		gl = &gla->glock;
	} else {
		gl = kmem_cache_alloc(gfs2_glock_cachep, GFP_NOFS);
		if (!gl)
			return -ENOMEM;
	}
	memset(&gl->gl_lksb, 0, sizeof(struct dlm_lksb));
	gl->gl_ops = glops;

	if (glops->go_flags & GLOF_LVB) {
		gl->gl_lksb.sb_lvbptr = kzalloc(GDLM_LVB_SIZE, GFP_NOFS);
		if (!gl->gl_lksb.sb_lvbptr) {
			gfs2_glock_dealloc(&gl->gl_rcu);
			return -ENOMEM;
		}
	}

	atomic_inc(&sdp->sd_glock_disposal);
	gl->gl_node.next = NULL;
	gl->gl_flags = BIT(GLF_INITIAL);
	if (glops->go_instantiate)
		gl->gl_flags |= BIT(GLF_INSTANTIATE_NEEDED);
	gl->gl_name = name;
	lockdep_set_subclass(&gl->gl_lockref.lock, glops->go_subclass);
	gl->gl_lockref.count = 1;
	gl->gl_state = LM_ST_UNLOCKED;
	gl->gl_target = LM_ST_UNLOCKED;
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
	gl->gl_dstamp = 0;
	preempt_disable();
	/* We use the global stats to estimate the initial per-glock stats */
	gl->gl_stats = this_cpu_ptr(sdp->sd_lkstats)->lkstats[glops->go_type];
	preempt_enable();
	gl->gl_stats.stats[GFS2_LKS_DCOUNT] = 0;
	gl->gl_stats.stats[GFS2_LKS_QCOUNT] = 0;
	gl->gl_tchange = jiffies;
	gl->gl_object = NULL;
	gl->gl_hold_time = GL_GLOCK_DFT_HOLD;
	INIT_DELAYED_WORK(&gl->gl_work, glock_work_func);
	if (gl->gl_name.ln_type == LM_TYPE_IOPEN)
		INIT_DELAYED_WORK(&gl->gl_delete, delete_work_func);

	mapping = gfs2_glock2aspace(gl);
	if (mapping) {
                mapping->a_ops = &gfs2_meta_aops;
		mapping->host = s->s_bdev->bd_mapping->host;
		mapping->flags = 0;
		mapping_set_gfp_mask(mapping, GFP_NOFS);
		mapping->i_private_data = NULL;
		mapping->writeback_index = 0;
	}

	tmp = find_insert_glock(&name, gl);
	if (tmp) {
		gfs2_glock_dealloc(&gl->gl_rcu);
		if (atomic_dec_and_test(&sdp->sd_glock_disposal))
			wake_up(&sdp->sd_kill_wait);

		if (IS_ERR(tmp))
			return PTR_ERR(tmp);
		gl = tmp;
	}

found:
	*glp = gl;
	return 0;
}

/**
 * __gfs2_holder_init - initialize a struct gfs2_holder in the default way
 * @gl: the glock
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 */

void __gfs2_holder_init(struct gfs2_glock *gl, unsigned int state, u16 flags,
			struct gfs2_holder *gh, unsigned long ip)
{
	INIT_LIST_HEAD(&gh->gh_list);
	gh->gh_gl = gfs2_glock_hold(gl);
	gh->gh_ip = ip;
	gh->gh_owner_pid = get_pid(task_pid(current));
	gh->gh_state = state;
	gh->gh_flags = flags;
	gh->gh_iflags = 0;
}

/**
 * gfs2_holder_reinit - reinitialize a struct gfs2_holder so we can requeue it
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 * Don't mess with the glock.
 *
 */

void gfs2_holder_reinit(unsigned int state, u16 flags, struct gfs2_holder *gh)
{
	gh->gh_state = state;
	gh->gh_flags = flags;
	gh->gh_iflags = 0;
	gh->gh_ip = _RET_IP_;
	put_pid(gh->gh_owner_pid);
	gh->gh_owner_pid = get_pid(task_pid(current));
}

/**
 * gfs2_holder_uninit - uninitialize a holder structure (drop glock reference)
 * @gh: the holder structure
 *
 */

void gfs2_holder_uninit(struct gfs2_holder *gh)
{
	put_pid(gh->gh_owner_pid);
	gfs2_glock_put(gh->gh_gl);
	gfs2_holder_mark_uninitialized(gh);
	gh->gh_ip = 0;
}

static void gfs2_glock_update_hold_time(struct gfs2_glock *gl,
					unsigned long start_time)
{
	/* Have we waited longer that a second? */
	if (time_after(jiffies, start_time + HZ)) {
		/* Lengthen the minimum hold time. */
		gl->gl_hold_time = min(gl->gl_hold_time + GL_GLOCK_HOLD_INCR,
				       GL_GLOCK_MAX_HOLD);
	}
}

/**
 * gfs2_glock_holder_ready - holder is ready and its error code can be collected
 * @gh: the glock holder
 *
 * Called when a glock holder no longer needs to be waited for because it is
 * now either held (HIF_HOLDER set; gh_error == 0), or acquiring the lock has
 * failed (gh_error != 0).
 */

int gfs2_glock_holder_ready(struct gfs2_holder *gh)
{
	if (gh->gh_error || (gh->gh_flags & GL_SKIP))
		return gh->gh_error;
	gh->gh_error = gfs2_instantiate(gh);
	if (gh->gh_error)
		gfs2_glock_dq(gh);
	return gh->gh_error;
}

/**
 * gfs2_glock_wait - wait on a glock acquisition
 * @gh: the glock holder
 *
 * Returns: 0 on success
 */

int gfs2_glock_wait(struct gfs2_holder *gh)
{
	unsigned long start_time = jiffies;

	might_sleep();
	wait_on_bit(&gh->gh_iflags, HIF_WAIT, TASK_UNINTERRUPTIBLE);
	gfs2_glock_update_hold_time(gh->gh_gl, start_time);
	return gfs2_glock_holder_ready(gh);
}

static int glocks_pending(unsigned int num_gh, struct gfs2_holder *ghs)
{
	int i;

	for (i = 0; i < num_gh; i++)
		if (test_bit(HIF_WAIT, &ghs[i].gh_iflags))
			return 1;
	return 0;
}

/**
 * gfs2_glock_async_wait - wait on multiple asynchronous glock acquisitions
 * @num_gh: the number of holders in the array
 * @ghs: the glock holder array
 *
 * Returns: 0 on success, meaning all glocks have been granted and are held.
 *          -ESTALE if the request timed out, meaning all glocks were released,
 *          and the caller should retry the operation.
 */

int gfs2_glock_async_wait(unsigned int num_gh, struct gfs2_holder *ghs)
{
	struct gfs2_sbd *sdp = ghs[0].gh_gl->gl_name.ln_sbd;
	int i, ret = 0, timeout = 0;
	unsigned long start_time = jiffies;

	might_sleep();
	/*
	 * Total up the (minimum hold time * 2) of all glocks and use that to
	 * determine the max amount of time we should wait.
	 */
	for (i = 0; i < num_gh; i++)
		timeout += ghs[i].gh_gl->gl_hold_time << 1;

	if (!wait_event_timeout(sdp->sd_async_glock_wait,
				!glocks_pending(num_gh, ghs), timeout)) {
		ret = -ESTALE; /* request timed out. */
		goto out;
	}

	for (i = 0; i < num_gh; i++) {
		struct gfs2_holder *gh = &ghs[i];
		int ret2;

		if (test_bit(HIF_HOLDER, &gh->gh_iflags)) {
			gfs2_glock_update_hold_time(gh->gh_gl,
						    start_time);
		}
		ret2 = gfs2_glock_holder_ready(gh);
		if (!ret)
			ret = ret2;
	}

out:
	if (ret) {
		for (i = 0; i < num_gh; i++) {
			struct gfs2_holder *gh = &ghs[i];

			gfs2_glock_dq(gh);
		}
	}
	return ret;
}

/**
 * request_demote - process a demote request
 * @gl: the glock
 * @state: the state the caller wants us to change to
 * @delay: zero to demote immediately; otherwise pending demote
 * @remote: true if this came from a different cluster node
 *
 * There are only two requests that we are going to see in actual
 * practise: LM_ST_SHARED and LM_ST_UNLOCKED
 */

static void request_demote(struct gfs2_glock *gl, unsigned int state,
			   unsigned long delay, bool remote)
{
	if (delay)
		set_bit(GLF_PENDING_DEMOTE, &gl->gl_flags);
	else
		gfs2_set_demote(gl);
	if (gl->gl_demote_state == LM_ST_EXCLUSIVE) {
		gl->gl_demote_state = state;
		gl->gl_demote_time = jiffies;
	} else if (gl->gl_demote_state != LM_ST_UNLOCKED &&
			gl->gl_demote_state != state) {
		gl->gl_demote_state = LM_ST_UNLOCKED;
	}
	if (gl->gl_ops->go_callback)
		gl->gl_ops->go_callback(gl, remote);
	trace_gfs2_demote_rq(gl, remote);
}

void gfs2_print_dbg(struct seq_file *seq, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	if (seq) {
		seq_vprintf(seq, fmt, args);
	} else {
		vaf.fmt = fmt;
		vaf.va = &args;

		pr_err("%pV", &vaf);
	}

	va_end(args);
}

static inline bool pid_is_meaningful(const struct gfs2_holder *gh)
{
        if (!(gh->gh_flags & GL_NOPID))
                return true;
        if (gh->gh_state == LM_ST_UNLOCKED)
                return true;
        return false;
}

/**
 * add_to_queue - Add a holder to the wait queue (but look for recursion)
 * @gh: the holder structure to add
 *
 * Eventually we should move the recursive locking trap to a
 * debugging option or something like that. This is the fast
 * path and needs to have the minimum number of distractions.
 * 
 */

static inline void add_to_queue(struct gfs2_holder *gh)
__releases(&gl->gl_lockref.lock)
__acquires(&gl->gl_lockref.lock)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct list_head *insert_pt = NULL;
	struct gfs2_holder *gh2;
	int try_futile = 0;

	GLOCK_BUG_ON(gl, gh->gh_owner_pid == NULL);
	if (test_and_set_bit(HIF_WAIT, &gh->gh_iflags))
		GLOCK_BUG_ON(gl, true);

	if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		if (test_bit(GLF_LOCK, &gl->gl_flags)) {
			struct gfs2_holder *current_gh;

			current_gh = find_first_holder(gl);
			try_futile = !may_grant(gl, current_gh, gh);
		}
		if (test_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags))
			goto fail;
	}

	list_for_each_entry(gh2, &gl->gl_holders, gh_list) {
		if (likely(gh2->gh_owner_pid != gh->gh_owner_pid))
			continue;
		if (gh->gh_gl->gl_ops->go_type == LM_TYPE_FLOCK)
			continue;
		if (!pid_is_meaningful(gh2))
			continue;
		goto trap_recursive;
	}
	list_for_each_entry(gh2, &gl->gl_holders, gh_list) {
		if (try_futile &&
		    !(gh2->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB))) {
fail:
			gh->gh_error = GLR_TRYFAILED;
			gfs2_holder_wake(gh);
			return;
		}
		if (test_bit(HIF_HOLDER, &gh2->gh_iflags))
			continue;
	}
	trace_gfs2_glock_queue(gh, 1);
	gfs2_glstats_inc(gl, GFS2_LKS_QCOUNT);
	gfs2_sbstats_inc(gl, GFS2_LKS_QCOUNT);
	if (likely(insert_pt == NULL)) {
		list_add_tail(&gh->gh_list, &gl->gl_holders);
		return;
	}
	list_add_tail(&gh->gh_list, insert_pt);
	spin_unlock(&gl->gl_lockref.lock);
	if (sdp->sd_lockstruct.ls_ops->lm_cancel)
		sdp->sd_lockstruct.ls_ops->lm_cancel(gl);
	spin_lock(&gl->gl_lockref.lock);
	return;

trap_recursive:
	fs_err(sdp, "original: %pSR\n", (void *)gh2->gh_ip);
	fs_err(sdp, "pid: %d\n", pid_nr(gh2->gh_owner_pid));
	fs_err(sdp, "lock type: %d req lock state : %d\n",
	       gh2->gh_gl->gl_name.ln_type, gh2->gh_state);
	fs_err(sdp, "new: %pSR\n", (void *)gh->gh_ip);
	fs_err(sdp, "pid: %d\n", pid_nr(gh->gh_owner_pid));
	fs_err(sdp, "lock type: %d req lock state : %d\n",
	       gh->gh_gl->gl_name.ln_type, gh->gh_state);
	gfs2_dump_glock(NULL, gl, true);
	BUG();
}

/**
 * gfs2_glock_nq - enqueue a struct gfs2_holder onto a glock (acquire a glock)
 * @gh: the holder structure
 *
 * if (gh->gh_flags & GL_ASYNC), this never returns an error
 *
 * Returns: 0, GLR_TRYFAILED, or errno on failure
 */

int gfs2_glock_nq(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	int error;

	if (glock_blocked_by_withdraw(gl) && !(gh->gh_flags & LM_FLAG_NOEXP))
		return -EIO;

	if (gh->gh_flags & GL_NOBLOCK) {
		struct gfs2_holder *current_gh;

		error = -ECHILD;
		spin_lock(&gl->gl_lockref.lock);
		if (find_last_waiter(gl))
			goto unlock;
		current_gh = find_first_holder(gl);
		if (!may_grant(gl, current_gh, gh))
			goto unlock;
		set_bit(HIF_HOLDER, &gh->gh_iflags);
		list_add_tail(&gh->gh_list, &gl->gl_holders);
		trace_gfs2_promote(gh);
		error = 0;
unlock:
		spin_unlock(&gl->gl_lockref.lock);
		return error;
	}

	gh->gh_error = 0;
	spin_lock(&gl->gl_lockref.lock);
	add_to_queue(gh);
	if (unlikely((LM_FLAG_NOEXP & gh->gh_flags) &&
		     test_and_clear_bit(GLF_HAVE_FROZEN_REPLY, &gl->gl_flags))) {
		set_bit(GLF_HAVE_REPLY, &gl->gl_flags);
		gl->gl_lockref.count++;
		gfs2_glock_queue_work(gl, 0);
	}
	run_queue(gl, 1);
	spin_unlock(&gl->gl_lockref.lock);

	error = 0;
	if (!(gh->gh_flags & GL_ASYNC))
		error = gfs2_glock_wait(gh);

	return error;
}

/**
 * gfs2_glock_poll - poll to see if an async request has been completed
 * @gh: the holder
 *
 * Returns: 1 if the request is ready to be gfs2_glock_wait()ed on
 */

int gfs2_glock_poll(struct gfs2_holder *gh)
{
	return test_bit(HIF_WAIT, &gh->gh_iflags) ? 0 : 1;
}

static inline bool needs_demote(struct gfs2_glock *gl)
{
	return (test_bit(GLF_DEMOTE, &gl->gl_flags) ||
		test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags));
}

static void __gfs2_glock_dq(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	unsigned delay = 0;
	int fast_path = 0;

	/*
	 * This holder should not be cached, so mark it for demote.
	 * Note: this should be done before the check for needs_demote
	 * below.
	 */
	if (gh->gh_flags & GL_NOCACHE)
		request_demote(gl, LM_ST_UNLOCKED, 0, false);

	list_del_init(&gh->gh_list);
	clear_bit(HIF_HOLDER, &gh->gh_iflags);
	trace_gfs2_glock_queue(gh, 0);

	/*
	 * If there hasn't been a demote request we are done.
	 * (Let the remaining holders, if any, keep holding it.)
	 */
	if (!needs_demote(gl)) {
		if (list_empty(&gl->gl_holders))
			fast_path = 1;
	}

	if (unlikely(!fast_path)) {
		gl->gl_lockref.count++;
		if (test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
		    !test_bit(GLF_DEMOTE, &gl->gl_flags) &&
		    gl->gl_name.ln_type == LM_TYPE_INODE)
			delay = gl->gl_hold_time;
		gfs2_glock_queue_work(gl, delay);
	}
}

/**
 * gfs2_glock_dq - dequeue a struct gfs2_holder from a glock (release a glock)
 * @gh: the glock holder
 *
 */
void gfs2_glock_dq(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	spin_lock(&gl->gl_lockref.lock);
	if (!gfs2_holder_queued(gh)) {
		/*
		 * May have already been dequeued because the locking request
		 * was GL_ASYNC and it has failed in the meantime.
		 */
		goto out;
	}

	if (list_is_first(&gh->gh_list, &gl->gl_holders) &&
	    !test_bit(HIF_HOLDER, &gh->gh_iflags)) {
		spin_unlock(&gl->gl_lockref.lock);
		gl->gl_name.ln_sbd->sd_lockstruct.ls_ops->lm_cancel(gl);
		wait_on_bit(&gh->gh_iflags, HIF_WAIT, TASK_UNINTERRUPTIBLE);
		spin_lock(&gl->gl_lockref.lock);
	}

	/*
	 * If we're in the process of file system withdraw, we cannot just
	 * dequeue any glocks until our journal is recovered, lest we introduce
	 * file system corruption. We need two exceptions to this rule: We need
	 * to allow unlocking of nondisk glocks and the glock for our own
	 * journal that needs recovery.
	 */
	if (test_bit(SDF_WITHDRAW_RECOVERY, &sdp->sd_flags) &&
	    glock_blocked_by_withdraw(gl) &&
	    gh->gh_gl != sdp->sd_jinode_gl) {
		sdp->sd_glock_dqs_held++;
		spin_unlock(&gl->gl_lockref.lock);
		might_sleep();
		wait_on_bit(&sdp->sd_flags, SDF_WITHDRAW_RECOVERY,
			    TASK_UNINTERRUPTIBLE);
		spin_lock(&gl->gl_lockref.lock);
	}

	__gfs2_glock_dq(gh);
out:
	spin_unlock(&gl->gl_lockref.lock);
}

void gfs2_glock_dq_wait(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	gfs2_glock_dq(gh);
	might_sleep();
	wait_on_bit(&gl->gl_flags, GLF_DEMOTE, TASK_UNINTERRUPTIBLE);
}

/**
 * gfs2_glock_dq_uninit - dequeue a holder from a glock and initialize it
 * @gh: the holder structure
 *
 */

void gfs2_glock_dq_uninit(struct gfs2_holder *gh)
{
	gfs2_glock_dq(gh);
	gfs2_holder_uninit(gh);
}

/**
 * gfs2_glock_nq_num - acquire a glock based on lock number
 * @sdp: the filesystem
 * @number: the lock number
 * @glops: the glock operations for the type of glock
 * @state: the state to acquire the glock in
 * @flags: modifier flags for the acquisition
 * @gh: the struct gfs2_holder
 *
 * Returns: errno
 */

int gfs2_glock_nq_num(struct gfs2_sbd *sdp, u64 number,
		      const struct gfs2_glock_operations *glops,
		      unsigned int state, u16 flags, struct gfs2_holder *gh)
{
	struct gfs2_glock *gl;
	int error;

	error = gfs2_glock_get(sdp, number, glops, CREATE, &gl);
	if (!error) {
		error = gfs2_glock_nq_init(gl, state, flags, gh);
		gfs2_glock_put(gl);
	}

	return error;
}

/**
 * glock_compare - Compare two struct gfs2_glock structures for sorting
 * @arg_a: the first structure
 * @arg_b: the second structure
 *
 */

static int glock_compare(const void *arg_a, const void *arg_b)
{
	const struct gfs2_holder *gh_a = *(const struct gfs2_holder **)arg_a;
	const struct gfs2_holder *gh_b = *(const struct gfs2_holder **)arg_b;
	const struct lm_lockname *a = &gh_a->gh_gl->gl_name;
	const struct lm_lockname *b = &gh_b->gh_gl->gl_name;

	if (a->ln_number > b->ln_number)
		return 1;
	if (a->ln_number < b->ln_number)
		return -1;
	BUG_ON(gh_a->gh_gl->gl_ops->go_type == gh_b->gh_gl->gl_ops->go_type);
	return 0;
}

/**
 * nq_m_sync - synchronously acquire more than one glock in deadlock free order
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 * @p: placeholder for the holder structure to pass back
 *
 * Returns: 0 on success (all glocks acquired),
 *          errno on failure (no glocks acquired)
 */

static int nq_m_sync(unsigned int num_gh, struct gfs2_holder *ghs,
		     struct gfs2_holder **p)
{
	unsigned int x;
	int error = 0;

	for (x = 0; x < num_gh; x++)
		p[x] = &ghs[x];

	sort(p, num_gh, sizeof(struct gfs2_holder *), glock_compare, NULL);

	for (x = 0; x < num_gh; x++) {
		error = gfs2_glock_nq(p[x]);
		if (error) {
			while (x--)
				gfs2_glock_dq(p[x]);
			break;
		}
	}

	return error;
}

/**
 * gfs2_glock_nq_m - acquire multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 * Returns: 0 on success (all glocks acquired),
 *          errno on failure (no glocks acquired)
 */

int gfs2_glock_nq_m(unsigned int num_gh, struct gfs2_holder *ghs)
{
	struct gfs2_holder *tmp[4];
	struct gfs2_holder **pph = tmp;
	int error = 0;

	switch(num_gh) {
	case 0:
		return 0;
	case 1:
		return gfs2_glock_nq(ghs);
	default:
		if (num_gh <= 4)
			break;
		pph = kmalloc_array(num_gh, sizeof(struct gfs2_holder *),
				    GFP_NOFS);
		if (!pph)
			return -ENOMEM;
	}

	error = nq_m_sync(num_gh, ghs, pph);

	if (pph != tmp)
		kfree(pph);

	return error;
}

/**
 * gfs2_glock_dq_m - release multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 */

void gfs2_glock_dq_m(unsigned int num_gh, struct gfs2_holder *ghs)
{
	while (num_gh--)
		gfs2_glock_dq(&ghs[num_gh]);
}

void gfs2_glock_cb(struct gfs2_glock *gl, unsigned int state)
{
	unsigned long delay = 0;

	gfs2_glock_hold(gl);
	spin_lock(&gl->gl_lockref.lock);
	if (!list_empty(&gl->gl_holders) &&
	    gl->gl_name.ln_type == LM_TYPE_INODE) {
		unsigned long now = jiffies;
		unsigned long holdtime;

		holdtime = gl->gl_tchange + gl->gl_hold_time;

		if (time_before(now, holdtime))
			delay = holdtime - now;
		if (test_bit(GLF_HAVE_REPLY, &gl->gl_flags))
			delay = gl->gl_hold_time;
	}
	request_demote(gl, state, delay, true);
	gfs2_glock_queue_work(gl, delay);
	spin_unlock(&gl->gl_lockref.lock);
}

/**
 * gfs2_should_freeze - Figure out if glock should be frozen
 * @gl: The glock in question
 *
 * Glocks are not frozen if (a) the result of the dlm operation is
 * an error, (b) the locking operation was an unlock operation or
 * (c) if there is a "noexp" flagged request anywhere in the queue
 *
 * Returns: 1 if freezing should occur, 0 otherwise
 */

static int gfs2_should_freeze(const struct gfs2_glock *gl)
{
	const struct gfs2_holder *gh;

	if (gl->gl_reply & ~LM_OUT_ST_MASK)
		return 0;
	if (gl->gl_target == LM_ST_UNLOCKED)
		return 0;

	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		if (test_bit(HIF_HOLDER, &gh->gh_iflags))
			continue;
		if (LM_FLAG_NOEXP & gh->gh_flags)
			return 0;
	}

	return 1;
}

/**
 * gfs2_glock_complete - Callback used by locking
 * @gl: Pointer to the glock
 * @ret: The return value from the dlm
 *
 * The gl_reply field is under the gl_lockref.lock lock so that it is ok
 * to use a bitfield shared with other glock state fields.
 */

void gfs2_glock_complete(struct gfs2_glock *gl, int ret)
{
	struct lm_lockstruct *ls = &gl->gl_name.ln_sbd->sd_lockstruct;

	spin_lock(&gl->gl_lockref.lock);
	gl->gl_reply = ret;

	if (unlikely(test_bit(DFL_BLOCK_LOCKS, &ls->ls_recover_flags))) {
		if (gfs2_should_freeze(gl)) {
			set_bit(GLF_HAVE_FROZEN_REPLY, &gl->gl_flags);
			spin_unlock(&gl->gl_lockref.lock);
			return;
		}
	}

	gl->gl_lockref.count++;
	set_bit(GLF_HAVE_REPLY, &gl->gl_flags);
	gfs2_glock_queue_work(gl, 0);
	spin_unlock(&gl->gl_lockref.lock);
}

static int glock_cmp(void *priv, const struct list_head *a,
		     const struct list_head *b)
{
	struct gfs2_glock *gla, *glb;

	gla = list_entry(a, struct gfs2_glock, gl_lru);
	glb = list_entry(b, struct gfs2_glock, gl_lru);

	if (gla->gl_name.ln_number > glb->gl_name.ln_number)
		return 1;
	if (gla->gl_name.ln_number < glb->gl_name.ln_number)
		return -1;

	return 0;
}

static bool can_free_glock(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	return !test_bit(GLF_LOCK, &gl->gl_flags) &&
	       !gl->gl_lockref.count &&
	       (!test_bit(GLF_LFLUSH, &gl->gl_flags) ||
		test_bit(SDF_KILL, &sdp->sd_flags));
}

/**
 * gfs2_dispose_glock_lru - Demote a list of glocks
 * @list: The list to dispose of
 *
 * Disposing of glocks may involve disk accesses, so that here we sort
 * the glocks by number (i.e. disk location of the inodes) so that if
 * there are any such accesses, they'll be sent in order (mostly).
 *
 * Must be called under the lru_lock, but may drop and retake this
 * lock. While the lru_lock is dropped, entries may vanish from the
 * list, but no new entries will appear on the list (since it is
 * private)
 */

static unsigned long gfs2_dispose_glock_lru(struct list_head *list)
__releases(&lru_lock)
__acquires(&lru_lock)
{
	struct gfs2_glock *gl;
	unsigned long freed = 0;

	list_sort(NULL, list, glock_cmp);

	while(!list_empty(list)) {
		gl = list_first_entry(list, struct gfs2_glock, gl_lru);
		if (!spin_trylock(&gl->gl_lockref.lock)) {
add_back_to_lru:
			list_move(&gl->gl_lru, &lru_list);
			continue;
		}
		if (!can_free_glock(gl)) {
			spin_unlock(&gl->gl_lockref.lock);
			goto add_back_to_lru;
		}
		list_del_init(&gl->gl_lru);
		atomic_dec(&lru_count);
		clear_bit(GLF_LRU, &gl->gl_flags);
		freed++;
		gl->gl_lockref.count++;
		if (gl->gl_state != LM_ST_UNLOCKED)
			request_demote(gl, LM_ST_UNLOCKED, 0, false);
		gfs2_glock_queue_work(gl, 0);
		spin_unlock(&gl->gl_lockref.lock);
		cond_resched_lock(&lru_lock);
	}
	return freed;
}

/**
 * gfs2_scan_glock_lru - Scan the LRU looking for locks to demote
 * @nr: The number of entries to scan
 *
 * This function selects the entries on the LRU which are able to
 * be demoted, and then kicks off the process by calling
 * gfs2_dispose_glock_lru() above.
 */

static unsigned long gfs2_scan_glock_lru(unsigned long nr)
{
	struct gfs2_glock *gl, *next;
	LIST_HEAD(dispose);
	unsigned long freed = 0;

	spin_lock(&lru_lock);
	list_for_each_entry_safe(gl, next, &lru_list, gl_lru) {
		if (!nr--)
			break;
		if (can_free_glock(gl))
			list_move(&gl->gl_lru, &dispose);
	}
	if (!list_empty(&dispose))
		freed = gfs2_dispose_glock_lru(&dispose);
	spin_unlock(&lru_lock);

	return freed;
}

static unsigned long gfs2_glock_shrink_scan(struct shrinker *shrink,
					    struct shrink_control *sc)
{
	if (!(sc->gfp_mask & __GFP_FS))
		return SHRINK_STOP;
	return gfs2_scan_glock_lru(sc->nr_to_scan);
}

static unsigned long gfs2_glock_shrink_count(struct shrinker *shrink,
					     struct shrink_control *sc)
{
	return vfs_pressure_ratio(atomic_read(&lru_count));
}

static struct shrinker *glock_shrinker;

/**
 * glock_hash_walk - Call a function for glock in a hash bucket
 * @examiner: the function
 * @sdp: the filesystem
 *
 * Note that the function can be called multiple times on the same
 * object.  So the user must ensure that the function can cope with
 * that.
 */

static void glock_hash_walk(glock_examiner examiner, const struct gfs2_sbd *sdp)
{
	struct gfs2_glock *gl;
	struct rhashtable_iter iter;

	rhashtable_walk_enter(&gl_hash_table, &iter);

	do {
		rhashtable_walk_start(&iter);

		while ((gl = rhashtable_walk_next(&iter)) && !IS_ERR(gl)) {
			if (gl->gl_name.ln_sbd == sdp)
				examiner(gl);
		}

		rhashtable_walk_stop(&iter);
	} while (cond_resched(), gl == ERR_PTR(-EAGAIN));

	rhashtable_walk_exit(&iter);
}

void gfs2_cancel_delete_work(struct gfs2_glock *gl)
{
	clear_bit(GLF_TRY_TO_EVICT, &gl->gl_flags);
	clear_bit(GLF_VERIFY_EVICT, &gl->gl_flags);
	if (cancel_delayed_work(&gl->gl_delete))
		gfs2_glock_put(gl);
}

static void flush_delete_work(struct gfs2_glock *gl)
{
	if (gl->gl_name.ln_type == LM_TYPE_IOPEN) {
		struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

		if (cancel_delayed_work(&gl->gl_delete)) {
			queue_delayed_work(sdp->sd_delete_wq,
					   &gl->gl_delete, 0);
		}
	}
}

void gfs2_flush_delete_work(struct gfs2_sbd *sdp)
{
	glock_hash_walk(flush_delete_work, sdp);
	flush_workqueue(sdp->sd_delete_wq);
}

/**
 * thaw_glock - thaw out a glock which has an unprocessed reply waiting
 * @gl: The glock to thaw
 *
 */

static void thaw_glock(struct gfs2_glock *gl)
{
	if (!test_and_clear_bit(GLF_HAVE_FROZEN_REPLY, &gl->gl_flags))
		return;
	if (!lockref_get_not_dead(&gl->gl_lockref))
		return;

	gfs2_glock_remove_from_lru(gl);
	spin_lock(&gl->gl_lockref.lock);
	set_bit(GLF_HAVE_REPLY, &gl->gl_flags);
	gfs2_glock_queue_work(gl, 0);
	spin_unlock(&gl->gl_lockref.lock);
}

/**
 * clear_glock - look at a glock and see if we can free it from glock cache
 * @gl: the glock to look at
 *
 */

static void clear_glock(struct gfs2_glock *gl)
{
	gfs2_glock_remove_from_lru(gl);

	spin_lock(&gl->gl_lockref.lock);
	if (!__lockref_is_dead(&gl->gl_lockref)) {
		gl->gl_lockref.count++;
		if (gl->gl_state != LM_ST_UNLOCKED)
			request_demote(gl, LM_ST_UNLOCKED, 0, false);
		gfs2_glock_queue_work(gl, 0);
	}
	spin_unlock(&gl->gl_lockref.lock);
}

/**
 * gfs2_glock_thaw - Thaw any frozen glocks
 * @sdp: The super block
 *
 */

void gfs2_glock_thaw(struct gfs2_sbd *sdp)
{
	glock_hash_walk(thaw_glock, sdp);
}

static void dump_glock(struct seq_file *seq, struct gfs2_glock *gl, bool fsid)
{
	spin_lock(&gl->gl_lockref.lock);
	gfs2_dump_glock(seq, gl, fsid);
	spin_unlock(&gl->gl_lockref.lock);
}

static void dump_glock_func(struct gfs2_glock *gl)
{
	dump_glock(NULL, gl, true);
}

static void withdraw_dq(struct gfs2_glock *gl)
{
	spin_lock(&gl->gl_lockref.lock);
	if (!__lockref_is_dead(&gl->gl_lockref) &&
	    glock_blocked_by_withdraw(gl))
		do_error(gl, LM_OUT_ERROR); /* remove pending waiters */
	spin_unlock(&gl->gl_lockref.lock);
}

void gfs2_gl_dq_holders(struct gfs2_sbd *sdp)
{
	glock_hash_walk(withdraw_dq, sdp);
}

/**
 * gfs2_gl_hash_clear - Empty out the glock hash table
 * @sdp: the filesystem
 *
 * Called when unmounting the filesystem.
 */

void gfs2_gl_hash_clear(struct gfs2_sbd *sdp)
{
	unsigned long start = jiffies;
	bool timed_out = false;

	set_bit(SDF_SKIP_DLM_UNLOCK, &sdp->sd_flags);
	flush_workqueue(sdp->sd_glock_wq);
	glock_hash_walk(clear_glock, sdp);
	flush_workqueue(sdp->sd_glock_wq);

	while (!timed_out) {
		wait_event_timeout(sdp->sd_kill_wait,
				   !atomic_read(&sdp->sd_glock_disposal),
				   HZ * 60);
		if (!atomic_read(&sdp->sd_glock_disposal))
			break;
		timed_out = time_after(jiffies, start + (HZ * 600));
		fs_warn(sdp, "%u glocks left after %u seconds%s\n",
			atomic_read(&sdp->sd_glock_disposal),
			jiffies_to_msecs(jiffies - start) / 1000,
			timed_out ? ":" : "; still waiting");
	}
	gfs2_lm_unmount(sdp);
	gfs2_free_dead_glocks(sdp);
	glock_hash_walk(dump_glock_func, sdp);
	destroy_workqueue(sdp->sd_glock_wq);
	sdp->sd_glock_wq = NULL;
}

static const char *state2str(unsigned state)
{
	switch(state) {
	case LM_ST_UNLOCKED:
		return "UN";
	case LM_ST_SHARED:
		return "SH";
	case LM_ST_DEFERRED:
		return "DF";
	case LM_ST_EXCLUSIVE:
		return "EX";
	}
	return "??";
}

static const char *hflags2str(char *buf, u16 flags, unsigned long iflags)
{
	char *p = buf;
	if (flags & LM_FLAG_TRY)
		*p++ = 't';
	if (flags & LM_FLAG_TRY_1CB)
		*p++ = 'T';
	if (flags & LM_FLAG_NOEXP)
		*p++ = 'e';
	if (flags & LM_FLAG_ANY)
		*p++ = 'A';
	if (flags & LM_FLAG_NODE_SCOPE)
		*p++ = 'n';
	if (flags & GL_ASYNC)
		*p++ = 'a';
	if (flags & GL_EXACT)
		*p++ = 'E';
	if (flags & GL_NOCACHE)
		*p++ = 'c';
	if (test_bit(HIF_HOLDER, &iflags))
		*p++ = 'H';
	if (test_bit(HIF_WAIT, &iflags))
		*p++ = 'W';
	if (flags & GL_SKIP)
		*p++ = 's';
	*p = 0;
	return buf;
}

/**
 * dump_holder - print information about a glock holder
 * @seq: the seq_file struct
 * @gh: the glock holder
 * @fs_id_buf: pointer to file system id (if requested)
 *
 */

static void dump_holder(struct seq_file *seq, const struct gfs2_holder *gh,
			const char *fs_id_buf)
{
	const char *comm = "(none)";
	pid_t owner_pid = 0;
	char flags_buf[32];

	rcu_read_lock();
	if (pid_is_meaningful(gh)) {
		struct task_struct *gh_owner;

		comm = "(ended)";
		owner_pid = pid_nr(gh->gh_owner_pid);
		gh_owner = pid_task(gh->gh_owner_pid, PIDTYPE_PID);
		if (gh_owner)
			comm = gh_owner->comm;
	}
	gfs2_print_dbg(seq, "%s H: s:%s f:%s e:%d p:%ld [%s] %pS\n",
		       fs_id_buf, state2str(gh->gh_state),
		       hflags2str(flags_buf, gh->gh_flags, gh->gh_iflags),
		       gh->gh_error, (long)owner_pid, comm, (void *)gh->gh_ip);
	rcu_read_unlock();
}

static const char *gflags2str(char *buf, const struct gfs2_glock *gl)
{
	const unsigned long *gflags = &gl->gl_flags;
	char *p = buf;

	if (test_bit(GLF_LOCK, gflags))
		*p++ = 'l';
	if (test_bit(GLF_DEMOTE, gflags))
		*p++ = 'D';
	if (test_bit(GLF_PENDING_DEMOTE, gflags))
		*p++ = 'd';
	if (test_bit(GLF_DEMOTE_IN_PROGRESS, gflags))
		*p++ = 'p';
	if (test_bit(GLF_DIRTY, gflags))
		*p++ = 'y';
	if (test_bit(GLF_LFLUSH, gflags))
		*p++ = 'f';
	if (test_bit(GLF_INVALIDATE_IN_PROGRESS, gflags))
		*p++ = 'i';
	if (test_bit(GLF_HAVE_REPLY, gflags))
		*p++ = 'r';
	if (test_bit(GLF_INITIAL, gflags))
		*p++ = 'a';
	if (test_bit(GLF_HAVE_FROZEN_REPLY, gflags))
		*p++ = 'F';
	if (!list_empty(&gl->gl_holders))
		*p++ = 'q';
	if (test_bit(GLF_LRU, gflags))
		*p++ = 'L';
	if (gl->gl_object)
		*p++ = 'o';
	if (test_bit(GLF_BLOCKING, gflags))
		*p++ = 'b';
	if (test_bit(GLF_UNLOCKED, gflags))
		*p++ = 'x';
	if (test_bit(GLF_INSTANTIATE_NEEDED, gflags))
		*p++ = 'n';
	if (test_bit(GLF_INSTANTIATE_IN_PROG, gflags))
		*p++ = 'N';
	if (test_bit(GLF_TRY_TO_EVICT, gflags))
		*p++ = 'e';
	if (test_bit(GLF_VERIFY_EVICT, gflags))
		*p++ = 'E';
	*p = 0;
	return buf;
}

/**
 * gfs2_dump_glock - print information about a glock
 * @seq: The seq_file struct
 * @gl: the glock
 * @fsid: If true, also dump the file system id
 *
 * The file format is as follows:
 * One line per object, capital letters are used to indicate objects
 * G = glock, I = Inode, R = rgrp, H = holder. Glocks are not indented,
 * other objects are indented by a single space and follow the glock to
 * which they are related. Fields are indicated by lower case letters
 * followed by a colon and the field value, except for strings which are in
 * [] so that its possible to see if they are composed of spaces for
 * example. The field's are n = number (id of the object), f = flags,
 * t = type, s = state, r = refcount, e = error, p = pid.
 *
 */

void gfs2_dump_glock(struct seq_file *seq, struct gfs2_glock *gl, bool fsid)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	unsigned long long dtime;
	const struct gfs2_holder *gh;
	char gflags_buf[32];
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	char fs_id_buf[sizeof(sdp->sd_fsname) + 7];
	unsigned long nrpages = 0;

	if (gl->gl_ops->go_flags & GLOF_ASPACE) {
		struct address_space *mapping = gfs2_glock2aspace(gl);

		nrpages = mapping->nrpages;
	}
	memset(fs_id_buf, 0, sizeof(fs_id_buf));
	if (fsid && sdp) /* safety precaution */
		sprintf(fs_id_buf, "fsid=%s: ", sdp->sd_fsname);
	dtime = jiffies - gl->gl_demote_time;
	dtime *= 1000000/HZ; /* demote time in uSec */
	if (!test_bit(GLF_DEMOTE, &gl->gl_flags))
		dtime = 0;
	gfs2_print_dbg(seq, "%sG:  s:%s n:%u/%llx f:%s t:%s d:%s/%llu a:%d "
		       "v:%d r:%d m:%ld p:%lu\n",
		       fs_id_buf, state2str(gl->gl_state),
		       gl->gl_name.ln_type,
		       (unsigned long long)gl->gl_name.ln_number,
		       gflags2str(gflags_buf, gl),
		       state2str(gl->gl_target),
		       state2str(gl->gl_demote_state), dtime,
		       atomic_read(&gl->gl_ail_count),
		       atomic_read(&gl->gl_revokes),
		       (int)gl->gl_lockref.count, gl->gl_hold_time, nrpages);

	list_for_each_entry(gh, &gl->gl_holders, gh_list)
		dump_holder(seq, gh, fs_id_buf);

	if (gl->gl_state != LM_ST_UNLOCKED && glops->go_dump)
		glops->go_dump(seq, gl, fs_id_buf);
}

static int gfs2_glstats_seq_show(struct seq_file *seq, void *iter_ptr)
{
	struct gfs2_glock *gl = iter_ptr;

	seq_printf(seq, "G: n:%u/%llx rtt:%llu/%llu rttb:%llu/%llu irt:%llu/%llu dcnt: %llu qcnt: %llu\n",
		   gl->gl_name.ln_type,
		   (unsigned long long)gl->gl_name.ln_number,
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_SRTT],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_SRTTVAR],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_SRTTB],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_SRTTVARB],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_SIRT],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_SIRTVAR],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_DCOUNT],
		   (unsigned long long)gl->gl_stats.stats[GFS2_LKS_QCOUNT]);
	return 0;
}

static const char *gfs2_gltype[] = {
	"type",
	"reserved",
	"nondisk",
	"inode",
	"rgrp",
	"meta",
	"iopen",
	"flock",
	"plock",
	"quota",
	"journal",
};

static const char *gfs2_stype[] = {
	[GFS2_LKS_SRTT]		= "srtt",
	[GFS2_LKS_SRTTVAR]	= "srttvar",
	[GFS2_LKS_SRTTB]	= "srttb",
	[GFS2_LKS_SRTTVARB]	= "srttvarb",
	[GFS2_LKS_SIRT]		= "sirt",
	[GFS2_LKS_SIRTVAR]	= "sirtvar",
	[GFS2_LKS_DCOUNT]	= "dlm",
	[GFS2_LKS_QCOUNT]	= "queue",
};

#define GFS2_NR_SBSTATS (ARRAY_SIZE(gfs2_gltype) * ARRAY_SIZE(gfs2_stype))

static int gfs2_sbstats_seq_show(struct seq_file *seq, void *iter_ptr)
{
	struct gfs2_sbd *sdp = seq->private;
	loff_t pos = *(loff_t *)iter_ptr;
	unsigned index = pos >> 3;
	unsigned subindex = pos & 0x07;
	int i;

	if (index == 0 && subindex != 0)
		return 0;

	seq_printf(seq, "%-10s %8s:", gfs2_gltype[index],
		   (index == 0) ? "cpu": gfs2_stype[subindex]);

	for_each_possible_cpu(i) {
                const struct gfs2_pcpu_lkstats *lkstats = per_cpu_ptr(sdp->sd_lkstats, i);

		if (index == 0)
			seq_printf(seq, " %15u", i);
		else
			seq_printf(seq, " %15llu", (unsigned long long)lkstats->
				   lkstats[index - 1].stats[subindex]);
	}
	seq_putc(seq, '\n');
	return 0;
}

int __init gfs2_glock_init(void)
{
	int i, ret;

	ret = rhashtable_init(&gl_hash_table, &ht_parms);
	if (ret < 0)
		return ret;

	glock_shrinker = shrinker_alloc(0, "gfs2-glock");
	if (!glock_shrinker) {
		rhashtable_destroy(&gl_hash_table);
		return -ENOMEM;
	}

	glock_shrinker->count_objects = gfs2_glock_shrink_count;
	glock_shrinker->scan_objects = gfs2_glock_shrink_scan;

	shrinker_register(glock_shrinker);

	for (i = 0; i < GLOCK_WAIT_TABLE_SIZE; i++)
		init_waitqueue_head(glock_wait_table + i);

	return 0;
}

void gfs2_glock_exit(void)
{
	shrinker_free(glock_shrinker);
	rhashtable_destroy(&gl_hash_table);
}

static void gfs2_glock_iter_next(struct gfs2_glock_iter *gi, loff_t n)
{
	struct gfs2_glock *gl = gi->gl;

	if (gl) {
		if (n == 0)
			return;
		gfs2_glock_put_async(gl);
	}
	for (;;) {
		gl = rhashtable_walk_next(&gi->hti);
		if (IS_ERR_OR_NULL(gl)) {
			if (gl == ERR_PTR(-EAGAIN)) {
				n = 1;
				continue;
			}
			gl = NULL;
			break;
		}
		if (gl->gl_name.ln_sbd != gi->sdp)
			continue;
		if (n <= 1) {
			if (!lockref_get_not_dead(&gl->gl_lockref))
				continue;
			break;
		} else {
			if (__lockref_is_dead(&gl->gl_lockref))
				continue;
			n--;
		}
	}
	gi->gl = gl;
}

static void *gfs2_glock_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct gfs2_glock_iter *gi = seq->private;
	loff_t n;

	/*
	 * We can either stay where we are, skip to the next hash table
	 * entry, or start from the beginning.
	 */
	if (*pos < gi->last_pos) {
		rhashtable_walk_exit(&gi->hti);
		rhashtable_walk_enter(&gl_hash_table, &gi->hti);
		n = *pos + 1;
	} else {
		n = *pos - gi->last_pos;
	}

	rhashtable_walk_start(&gi->hti);

	gfs2_glock_iter_next(gi, n);
	gi->last_pos = *pos;
	return gi->gl;
}

static void *gfs2_glock_seq_next(struct seq_file *seq, void *iter_ptr,
				 loff_t *pos)
{
	struct gfs2_glock_iter *gi = seq->private;

	(*pos)++;
	gi->last_pos = *pos;
	gfs2_glock_iter_next(gi, 1);
	return gi->gl;
}

static void gfs2_glock_seq_stop(struct seq_file *seq, void *iter_ptr)
	__releases(RCU)
{
	struct gfs2_glock_iter *gi = seq->private;

	rhashtable_walk_stop(&gi->hti);
}

static int gfs2_glock_seq_show(struct seq_file *seq, void *iter_ptr)
{
	dump_glock(seq, iter_ptr, false);
	return 0;
}

static void *gfs2_sbstats_seq_start(struct seq_file *seq, loff_t *pos)
{
	preempt_disable();
	if (*pos >= GFS2_NR_SBSTATS)
		return NULL;
	return pos;
}

static void *gfs2_sbstats_seq_next(struct seq_file *seq, void *iter_ptr,
				   loff_t *pos)
{
	(*pos)++;
	if (*pos >= GFS2_NR_SBSTATS)
		return NULL;
	return pos;
}

static void gfs2_sbstats_seq_stop(struct seq_file *seq, void *iter_ptr)
{
	preempt_enable();
}

static const struct seq_operations gfs2_glock_seq_ops = {
	.start = gfs2_glock_seq_start,
	.next  = gfs2_glock_seq_next,
	.stop  = gfs2_glock_seq_stop,
	.show  = gfs2_glock_seq_show,
};

static const struct seq_operations gfs2_glstats_seq_ops = {
	.start = gfs2_glock_seq_start,
	.next  = gfs2_glock_seq_next,
	.stop  = gfs2_glock_seq_stop,
	.show  = gfs2_glstats_seq_show,
};

static const struct seq_operations gfs2_sbstats_sops = {
	.start = gfs2_sbstats_seq_start,
	.next  = gfs2_sbstats_seq_next,
	.stop  = gfs2_sbstats_seq_stop,
	.show  = gfs2_sbstats_seq_show,
};

#define GFS2_SEQ_GOODSIZE min(PAGE_SIZE << PAGE_ALLOC_COSTLY_ORDER, 65536UL)

static int __gfs2_glocks_open(struct inode *inode, struct file *file,
			      const struct seq_operations *ops)
{
	int ret = seq_open_private(file, ops, sizeof(struct gfs2_glock_iter));
	if (ret == 0) {
		struct seq_file *seq = file->private_data;
		struct gfs2_glock_iter *gi = seq->private;

		gi->sdp = inode->i_private;
		seq->buf = kmalloc(GFS2_SEQ_GOODSIZE, GFP_KERNEL | __GFP_NOWARN);
		if (seq->buf)
			seq->size = GFS2_SEQ_GOODSIZE;
		/*
		 * Initially, we are "before" the first hash table entry; the
		 * first call to rhashtable_walk_next gets us the first entry.
		 */
		gi->last_pos = -1;
		gi->gl = NULL;
		rhashtable_walk_enter(&gl_hash_table, &gi->hti);
	}
	return ret;
}

static int gfs2_glocks_open(struct inode *inode, struct file *file)
{
	return __gfs2_glocks_open(inode, file, &gfs2_glock_seq_ops);
}

static int gfs2_glocks_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq = file->private_data;
	struct gfs2_glock_iter *gi = seq->private;

	if (gi->gl)
		gfs2_glock_put(gi->gl);
	rhashtable_walk_exit(&gi->hti);
	return seq_release_private(inode, file);
}

static int gfs2_glstats_open(struct inode *inode, struct file *file)
{
	return __gfs2_glocks_open(inode, file, &gfs2_glstats_seq_ops);
}

static const struct file_operations gfs2_glocks_fops = {
	.owner   = THIS_MODULE,
	.open    = gfs2_glocks_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = gfs2_glocks_release,
};

static const struct file_operations gfs2_glstats_fops = {
	.owner   = THIS_MODULE,
	.open    = gfs2_glstats_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = gfs2_glocks_release,
};

struct gfs2_glockfd_iter {
	struct super_block *sb;
	unsigned int tgid;
	struct task_struct *task;
	unsigned int fd;
	struct file *file;
};

static struct task_struct *gfs2_glockfd_next_task(struct gfs2_glockfd_iter *i)
{
	struct pid_namespace *ns = task_active_pid_ns(current);
	struct pid *pid;

	if (i->task)
		put_task_struct(i->task);

	rcu_read_lock();
retry:
	i->task = NULL;
	pid = find_ge_pid(i->tgid, ns);
	if (pid) {
		i->tgid = pid_nr_ns(pid, ns);
		i->task = pid_task(pid, PIDTYPE_TGID);
		if (!i->task) {
			i->tgid++;
			goto retry;
		}
		get_task_struct(i->task);
	}
	rcu_read_unlock();
	return i->task;
}

static struct file *gfs2_glockfd_next_file(struct gfs2_glockfd_iter *i)
{
	if (i->file) {
		fput(i->file);
		i->file = NULL;
	}

	for(;; i->fd++) {
		i->file = fget_task_next(i->task, &i->fd);
		if (!i->file) {
			i->fd = 0;
			break;
		}

		if (file_inode(i->file)->i_sb == i->sb)
			break;

		fput(i->file);
	}
	return i->file;
}

static void *gfs2_glockfd_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct gfs2_glockfd_iter *i = seq->private;

	if (*pos)
		return NULL;
	while (gfs2_glockfd_next_task(i)) {
		if (gfs2_glockfd_next_file(i))
			return i;
		i->tgid++;
	}
	return NULL;
}

static void *gfs2_glockfd_seq_next(struct seq_file *seq, void *iter_ptr,
				   loff_t *pos)
{
	struct gfs2_glockfd_iter *i = seq->private;

	(*pos)++;
	i->fd++;
	do {
		if (gfs2_glockfd_next_file(i))
			return i;
		i->tgid++;
	} while (gfs2_glockfd_next_task(i));
	return NULL;
}

static void gfs2_glockfd_seq_stop(struct seq_file *seq, void *iter_ptr)
{
	struct gfs2_glockfd_iter *i = seq->private;

	if (i->file)
		fput(i->file);
	if (i->task)
		put_task_struct(i->task);
}

static void gfs2_glockfd_seq_show_flock(struct seq_file *seq,
					struct gfs2_glockfd_iter *i)
{
	struct gfs2_file *fp = i->file->private_data;
	struct gfs2_holder *fl_gh = &fp->f_fl_gh;
	struct lm_lockname gl_name = { .ln_type = LM_TYPE_RESERVED };

	if (!READ_ONCE(fl_gh->gh_gl))
		return;

	spin_lock(&i->file->f_lock);
	if (gfs2_holder_initialized(fl_gh))
		gl_name = fl_gh->gh_gl->gl_name;
	spin_unlock(&i->file->f_lock);

	if (gl_name.ln_type != LM_TYPE_RESERVED) {
		seq_printf(seq, "%d %u %u/%llx\n",
			   i->tgid, i->fd, gl_name.ln_type,
			   (unsigned long long)gl_name.ln_number);
	}
}

static int gfs2_glockfd_seq_show(struct seq_file *seq, void *iter_ptr)
{
	struct gfs2_glockfd_iter *i = seq->private;
	struct inode *inode = file_inode(i->file);
	struct gfs2_glock *gl;

	inode_lock_shared(inode);
	gl = GFS2_I(inode)->i_iopen_gh.gh_gl;
	if (gl) {
		seq_printf(seq, "%d %u %u/%llx\n",
			   i->tgid, i->fd, gl->gl_name.ln_type,
			   (unsigned long long)gl->gl_name.ln_number);
	}
	gfs2_glockfd_seq_show_flock(seq, i);
	inode_unlock_shared(inode);
	return 0;
}

static const struct seq_operations gfs2_glockfd_seq_ops = {
	.start = gfs2_glockfd_seq_start,
	.next  = gfs2_glockfd_seq_next,
	.stop  = gfs2_glockfd_seq_stop,
	.show  = gfs2_glockfd_seq_show,
};

static int gfs2_glockfd_open(struct inode *inode, struct file *file)
{
	struct gfs2_glockfd_iter *i;
	struct gfs2_sbd *sdp = inode->i_private;

	i = __seq_open_private(file, &gfs2_glockfd_seq_ops,
			       sizeof(struct gfs2_glockfd_iter));
	if (!i)
		return -ENOMEM;
	i->sb = sdp->sd_vfs;
	return 0;
}

static const struct file_operations gfs2_glockfd_fops = {
	.owner   = THIS_MODULE,
	.open    = gfs2_glockfd_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

DEFINE_SEQ_ATTRIBUTE(gfs2_sbstats);

void gfs2_create_debugfs_file(struct gfs2_sbd *sdp)
{
	sdp->debugfs_dir = debugfs_create_dir(sdp->sd_table_name, gfs2_root);

	debugfs_create_file("glocks", S_IFREG | S_IRUGO, sdp->debugfs_dir, sdp,
			    &gfs2_glocks_fops);

	debugfs_create_file("glockfd", S_IFREG | S_IRUGO, sdp->debugfs_dir, sdp,
			    &gfs2_glockfd_fops);

	debugfs_create_file("glstats", S_IFREG | S_IRUGO, sdp->debugfs_dir, sdp,
			    &gfs2_glstats_fops);

	debugfs_create_file("sbstats", S_IFREG | S_IRUGO, sdp->debugfs_dir, sdp,
			    &gfs2_sbstats_fops);
}

void gfs2_delete_debugfs_file(struct gfs2_sbd *sdp)
{
	debugfs_remove_recursive(sdp->debugfs_dir);
	sdp->debugfs_dir = NULL;
}

void gfs2_register_debugfs(void)
{
	gfs2_root = debugfs_create_dir("gfs2", NULL);
}

void gfs2_unregister_debugfs(void)
{
	debugfs_remove(gfs2_root);
	gfs2_root = NULL;
}
