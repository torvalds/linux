/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
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

static struct dentry *gfs2_root;
static struct workqueue_struct *glock_workqueue;
struct workqueue_struct *gfs2_delete_workqueue;
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
	u32 hash = jhash2((u32 *)name, sizeof(*name) / 4, 0);

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

	if (gl->gl_ops->go_flags & GLOF_ASPACE) {
		kmem_cache_free(gfs2_glock_aspace_cachep, gl);
	} else {
		kfree(gl->gl_lksb.sb_lvbptr);
		kmem_cache_free(gfs2_glock_cachep, gl);
	}
}

void gfs2_glock_free(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;

	rhashtable_remove_fast(&gl_hash_table, &gl->gl_node, ht_parms);
	smp_mb();
	wake_up_glock(gl);
	call_rcu(&gl->gl_rcu, gfs2_glock_dealloc);
	if (atomic_dec_and_test(&sdp->sd_glock_disposal))
		wake_up(&sdp->sd_glock_wait);
}

/**
 * gfs2_glock_hold() - increment reference count on glock
 * @gl: The glock to hold
 *
 */

void gfs2_glock_hold(struct gfs2_glock *gl)
{
	GLOCK_BUG_ON(gl, __lockref_is_dead(&gl->gl_lockref));
	lockref_get(&gl->gl_lockref);
}

/**
 * demote_ok - Check to see if it's ok to unlock a glock
 * @gl: the glock
 *
 * Returns: 1 if it's ok
 */

static int demote_ok(const struct gfs2_glock *gl)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;

	if (gl->gl_state == LM_ST_UNLOCKED)
		return 0;
	if (!list_empty(&gl->gl_holders))
		return 0;
	if (glops->go_demote_ok)
		return glops->go_demote_ok(gl);
	return 1;
}


void gfs2_glock_add_to_lru(struct gfs2_glock *gl)
{
	spin_lock(&lru_lock);

	if (!list_empty(&gl->gl_lru))
		list_del_init(&gl->gl_lru);
	else
		atomic_inc(&lru_count);

	list_add_tail(&gl->gl_lru, &lru_list);
	set_bit(GLF_LRU, &gl->gl_flags);
	spin_unlock(&lru_lock);
}

static void gfs2_glock_remove_from_lru(struct gfs2_glock *gl)
{
	if (!(gl->gl_ops->go_flags & GLOF_LRU))
		return;

	spin_lock(&lru_lock);
	if (!list_empty(&gl->gl_lru)) {
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
static void __gfs2_glock_queue_work(struct gfs2_glock *gl, unsigned long delay) {
	if (!queue_delayed_work(glock_workqueue, &gl->gl_work, delay)) {
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

static void gfs2_glock_queue_work(struct gfs2_glock *gl, unsigned long delay) {
	spin_lock(&gl->gl_lockref.lock);
	__gfs2_glock_queue_work(gl, delay);
	spin_unlock(&gl->gl_lockref.lock);
}

static void __gfs2_glock_put(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct address_space *mapping = gfs2_glock2aspace(gl);

	lockref_mark_dead(&gl->gl_lockref);

	gfs2_glock_remove_from_lru(gl);
	spin_unlock(&gl->gl_lockref.lock);
	GLOCK_BUG_ON(gl, !list_empty(&gl->gl_holders));
	GLOCK_BUG_ON(gl, mapping && mapping->nrpages);
	trace_gfs2_glock_put(gl);
	sdp->sd_lockstruct.ls_ops->lm_put_lock(gl);
}

/*
 * Cause the glock to be put in work queue context.
 */
void gfs2_glock_queue_put(struct gfs2_glock *gl)
{
	gfs2_glock_queue_work(gl, 0);
}

/**
 * gfs2_glock_put() - Decrement reference count on glock
 * @gl: The glock to put
 *
 */

void gfs2_glock_put(struct gfs2_glock *gl)
{
	if (lockref_put_or_lock(&gl->gl_lockref))
		return;

	__gfs2_glock_put(gl);
}

/**
 * may_grant - check if its ok to grant a new lock
 * @gl: The glock
 * @gh: The lock request which we wish to grant
 *
 * Returns: true if its ok to grant the lock
 */

static inline int may_grant(const struct gfs2_glock *gl, const struct gfs2_holder *gh)
{
	const struct gfs2_holder *gh_head = list_entry(gl->gl_holders.next, const struct gfs2_holder, gh_list);
	if ((gh->gh_state == LM_ST_EXCLUSIVE ||
	     gh_head->gh_state == LM_ST_EXCLUSIVE) && gh != gh_head)
		return 0;
	if (gl->gl_state == gh->gh_state)
		return 1;
	if (gh->gh_flags & GL_EXACT)
		return 0;
	if (gl->gl_state == LM_ST_EXCLUSIVE) {
		if (gh->gh_state == LM_ST_SHARED && gh_head->gh_state == LM_ST_SHARED)
			return 1;
		if (gh->gh_state == LM_ST_DEFERRED && gh_head->gh_state == LM_ST_DEFERRED)
			return 1;
	}
	if (gl->gl_state != LM_ST_UNLOCKED && (gh->gh_flags & LM_FLAG_ANY))
		return 1;
	return 0;
}

static void gfs2_holder_wake(struct gfs2_holder *gh)
{
	clear_bit(HIF_WAIT, &gh->gh_iflags);
	smp_mb__after_atomic();
	wake_up_bit(&gh->gh_iflags, HIF_WAIT);
}

/**
 * do_error - Something unexpected has happened during a lock request
 *
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
 * do_promote - promote as many requests as possible on the current queue
 * @gl: The glock
 * 
 * Returns: 1 if there is a blocked holder at the head of the list, or 2
 *          if a type specific operation is underway.
 */

static int do_promote(struct gfs2_glock *gl)
__releases(&gl->gl_lockref.lock)
__acquires(&gl->gl_lockref.lock)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_holder *gh, *tmp;
	int ret;

restart:
	list_for_each_entry_safe(gh, tmp, &gl->gl_holders, gh_list) {
		if (test_bit(HIF_HOLDER, &gh->gh_iflags))
			continue;
		if (may_grant(gl, gh)) {
			if (gh->gh_list.prev == &gl->gl_holders &&
			    glops->go_lock) {
				spin_unlock(&gl->gl_lockref.lock);
				/* FIXME: eliminate this eventually */
				ret = glops->go_lock(gh);
				spin_lock(&gl->gl_lockref.lock);
				if (ret) {
					if (ret == 1)
						return 2;
					gh->gh_error = ret;
					list_del_init(&gh->gh_list);
					trace_gfs2_glock_queue(gh, 0);
					gfs2_holder_wake(gh);
					goto restart;
				}
				set_bit(HIF_HOLDER, &gh->gh_iflags);
				trace_gfs2_promote(gh, 1);
				gfs2_holder_wake(gh);
				goto restart;
			}
			set_bit(HIF_HOLDER, &gh->gh_iflags);
			trace_gfs2_promote(gh, 0);
			gfs2_holder_wake(gh);
			continue;
		}
		if (gh->gh_list.prev == &gl->gl_holders)
			return 1;
		do_error(gl, 0);
		break;
	}
	return 0;
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
 * state_change - record that the glock is now in a different state
 * @gl: the glock
 * @new_state the new state
 *
 */

static void state_change(struct gfs2_glock *gl, unsigned int new_state)
{
	int held1, held2;

	held1 = (gl->gl_state != LM_ST_UNLOCKED);
	held2 = (new_state != LM_ST_UNLOCKED);

	if (held1 != held2) {
		GLOCK_BUG_ON(gl, __lockref_is_dead(&gl->gl_lockref));
		if (held2)
			gl->gl_lockref.count++;
		else
			gl->gl_lockref.count--;
	}
	if (held1 && held2 && list_empty(&gl->gl_holders))
		clear_bit(GLF_QUEUED, &gl->gl_flags);

	if (new_state != gl->gl_target)
		/* shorten our minimum hold time */
		gl->gl_hold_time = max(gl->gl_hold_time - GL_GLOCK_HOLD_DECR,
				       GL_GLOCK_MIN_HOLD);
	gl->gl_state = new_state;
	gl->gl_tchange = jiffies;
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
	int rv;

	spin_lock(&gl->gl_lockref.lock);
	trace_gfs2_glock_state_change(gl, state);
	state_change(gl, state);
	gh = find_first_waiter(gl);

	/* Demote to UN request arrived during demote to SH or DF */
	if (test_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags) &&
	    state != LM_ST_UNLOCKED && gl->gl_demote_state == LM_ST_UNLOCKED)
		gl->gl_target = LM_ST_UNLOCKED;

	/* Check for state != intended state */
	if (unlikely(state != gl->gl_target)) {
		if (gh && !test_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags)) {
			/* move to back of queue and try next entry */
			if (ret & LM_OUT_CANCELED) {
				if ((gh->gh_flags & LM_FLAG_PRIORITY) == 0)
					list_move_tail(&gh->gh_list, &gl->gl_holders);
				gh = find_first_waiter(gl);
				gl->gl_target = gh->gh_state;
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
			pr_err("wanted %u got %u\n", gl->gl_target, state);
			GLOCK_BUG_ON(gl, 1);
		}
		spin_unlock(&gl->gl_lockref.lock);
		return;
	}

	/* Fast path - we got what we asked for */
	if (test_and_clear_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags))
		gfs2_demote_wake(gl);
	if (state != LM_ST_UNLOCKED) {
		if (glops->go_xmote_bh) {
			spin_unlock(&gl->gl_lockref.lock);
			rv = glops->go_xmote_bh(gl, gh);
			spin_lock(&gl->gl_lockref.lock);
			if (rv) {
				do_error(gl, rv);
				goto out;
			}
		}
		rv = do_promote(gl);
		if (rv == 2)
			goto out_locked;
	}
out:
	clear_bit(GLF_LOCK, &gl->gl_flags);
out_locked:
	spin_unlock(&gl->gl_lockref.lock);
}

/**
 * do_xmote - Calls the DLM to change the state of a lock
 * @gl: The lock state
 * @gh: The holder (only for promotes)
 * @target: The target lock state
 *
 */

static void do_xmote(struct gfs2_glock *gl, struct gfs2_holder *gh, unsigned int target)
__releases(&gl->gl_lockref.lock)
__acquires(&gl->gl_lockref.lock)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	unsigned int lck_flags = (unsigned int)(gh ? gh->gh_flags : 0);
	int ret;

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)) &&
	    target != LM_ST_UNLOCKED)
		return;
	lck_flags &= (LM_FLAG_TRY | LM_FLAG_TRY_1CB | LM_FLAG_NOEXP |
		      LM_FLAG_PRIORITY);
	GLOCK_BUG_ON(gl, gl->gl_state == target);
	GLOCK_BUG_ON(gl, gl->gl_state == gl->gl_target);
	if ((target == LM_ST_UNLOCKED || target == LM_ST_DEFERRED) &&
	    glops->go_inval) {
		set_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags);
		do_error(gl, 0); /* Fail queued try locks */
	}
	gl->gl_req = target;
	set_bit(GLF_BLOCKING, &gl->gl_flags);
	if ((gl->gl_req == LM_ST_UNLOCKED) ||
	    (gl->gl_state == LM_ST_EXCLUSIVE) ||
	    (lck_flags & (LM_FLAG_TRY|LM_FLAG_TRY_1CB)))
		clear_bit(GLF_BLOCKING, &gl->gl_flags);
	spin_unlock(&gl->gl_lockref.lock);
	if (glops->go_sync)
		glops->go_sync(gl);
	if (test_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags))
		glops->go_inval(gl, target == LM_ST_DEFERRED ? 0 : DIO_METADATA);
	clear_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags);

	gfs2_glock_hold(gl);
	if (sdp->sd_lockstruct.ls_ops->lm_lock)	{
		/* lock_dlm */
		ret = sdp->sd_lockstruct.ls_ops->lm_lock(gl, target, lck_flags);
		if (ret == -EINVAL && gl->gl_target == LM_ST_UNLOCKED &&
		    target == LM_ST_UNLOCKED &&
		    test_bit(SDF_SKIP_DLM_UNLOCK, &sdp->sd_flags)) {
			finish_xmote(gl, target);
			gfs2_glock_queue_work(gl, 0);
		}
		else if (ret) {
			pr_err("lm_lock ret %d\n", ret);
			GLOCK_BUG_ON(gl, !test_bit(SDF_SHUTDOWN,
						   &sdp->sd_flags));
		}
	} else { /* lock_nolock */
		finish_xmote(gl, target);
		gfs2_glock_queue_work(gl, 0);
	}

	spin_lock(&gl->gl_lockref.lock);
}

/**
 * find_first_holder - find the first "holder" gh
 * @gl: the glock
 */

static inline struct gfs2_holder *find_first_holder(const struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;

	if (!list_empty(&gl->gl_holders)) {
		gh = list_entry(gl->gl_holders.next, struct gfs2_holder, gh_list);
		if (test_bit(HIF_HOLDER, &gh->gh_iflags))
			return gh;
	}
	return NULL;
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
	int ret;

	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags))
		return;

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
		ret = do_promote(gl);
		if (ret == 0)
			goto out_unlock;
		if (ret == 2)
			goto out;
		gh = find_first_waiter(gl);
		gl->gl_target = gh->gh_state;
		if (!(gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)))
			do_error(gl, 0); /* Fail queued try locks */
	}
	do_xmote(gl, gh, gl->gl_target);
out:
	return;

out_sched:
	clear_bit(GLF_LOCK, &gl->gl_flags);
	smp_mb__after_atomic();
	gl->gl_lockref.count++;
	__gfs2_glock_queue_work(gl, 0);
	return;

out_unlock:
	clear_bit(GLF_LOCK, &gl->gl_flags);
	smp_mb__after_atomic();
	return;
}

static void delete_work_func(struct work_struct *work)
{
	struct gfs2_glock *gl = container_of(work, struct gfs2_glock, gl_delete);
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	struct inode *inode;
	u64 no_addr = gl->gl_name.ln_number;

	/* If someone's using this glock to create a new dinode, the block must
	   have been freed by another node, then re-used, in which case our
	   iopen callback is too late after the fact. Ignore it. */
	if (test_bit(GLF_INODE_CREATING, &gl->gl_flags))
		goto out;

	inode = gfs2_lookup_by_inum(sdp, no_addr, NULL, GFS2_BLKST_UNLINKED);
	if (inode && !IS_ERR(inode)) {
		d_prune_aliases(inode);
		iput(inode);
	}
out:
	gfs2_glock_put(gl);
}

static void glock_work_func(struct work_struct *work)
{
	unsigned long delay = 0;
	struct gfs2_glock *gl = container_of(work, struct gfs2_glock, gl_work.work);
	unsigned int drop_refs = 1;

	if (test_and_clear_bit(GLF_REPLY_PENDING, &gl->gl_flags)) {
		finish_xmote(gl, gl->gl_reply);
		drop_refs++;
	}
	spin_lock(&gl->gl_lockref.lock);
	if (test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
	    gl->gl_state != LM_ST_UNLOCKED &&
	    gl->gl_demote_state != LM_ST_EXCLUSIVE) {
		unsigned long holdtime, now = jiffies;

		holdtime = gl->gl_tchange + gl->gl_hold_time;
		if (time_before(now, holdtime))
			delay = holdtime - now;

		if (!delay) {
			clear_bit(GLF_PENDING_DEMOTE, &gl->gl_flags);
			set_bit(GLF_DEMOTE, &gl->gl_flags);
		}
	}
	run_queue(gl, 0);
	if (delay) {
		/* Keep one glock reference for the work we requeue. */
		drop_refs--;
		if (gl->gl_name.ln_type != LM_TYPE_INODE)
			delay = 0;
		__gfs2_glock_queue_work(gl, delay);
	}

	/*
	 * Drop the remaining glock references manually here. (Mind that
	 * __gfs2_glock_queue_work depends on the lockref spinlock begin held
	 * here as well.)
	 */
	gl->gl_lockref.count -= drop_refs;
	if (!gl->gl_lockref.count) {
		__gfs2_glock_put(gl);
		return;
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
	struct kmem_cache *cachep;
	int ret = 0;

	gl = find_insert_glock(&name, NULL);
	if (gl) {
		*glp = gl;
		return 0;
	}
	if (!create)
		return -ENOENT;

	if (glops->go_flags & GLOF_ASPACE)
		cachep = gfs2_glock_aspace_cachep;
	else
		cachep = gfs2_glock_cachep;
	gl = kmem_cache_alloc(cachep, GFP_NOFS);
	if (!gl)
		return -ENOMEM;

	memset(&gl->gl_lksb, 0, sizeof(struct dlm_lksb));

	if (glops->go_flags & GLOF_LVB) {
		gl->gl_lksb.sb_lvbptr = kzalloc(GFS2_MIN_LVB_SIZE, GFP_NOFS);
		if (!gl->gl_lksb.sb_lvbptr) {
			kmem_cache_free(cachep, gl);
			return -ENOMEM;
		}
	}

	atomic_inc(&sdp->sd_glock_disposal);
	gl->gl_node.next = NULL;
	gl->gl_flags = 0;
	gl->gl_name = name;
	gl->gl_lockref.count = 1;
	gl->gl_state = LM_ST_UNLOCKED;
	gl->gl_target = LM_ST_UNLOCKED;
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
	gl->gl_ops = glops;
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
	INIT_WORK(&gl->gl_delete, delete_work_func);

	mapping = gfs2_glock2aspace(gl);
	if (mapping) {
                mapping->a_ops = &gfs2_meta_aops;
		mapping->host = s->s_bdev->bd_inode;
		mapping->flags = 0;
		mapping_set_gfp_mask(mapping, GFP_NOFS);
		mapping->private_data = NULL;
		mapping->writeback_index = 0;
	}

	tmp = find_insert_glock(&name, gl);
	if (!tmp) {
		*glp = gl;
		goto out;
	}
	if (IS_ERR(tmp)) {
		ret = PTR_ERR(tmp);
		goto out_free;
	}
	*glp = tmp;

out_free:
	kfree(gl->gl_lksb.sb_lvbptr);
	kmem_cache_free(cachep, gl);
	atomic_dec(&sdp->sd_glock_disposal);

out:
	return ret;
}

/**
 * gfs2_holder_init - initialize a struct gfs2_holder in the default way
 * @gl: the glock
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 */

void gfs2_holder_init(struct gfs2_glock *gl, unsigned int state, u16 flags,
		      struct gfs2_holder *gh)
{
	INIT_LIST_HEAD(&gh->gh_list);
	gh->gh_gl = gl;
	gh->gh_ip = _RET_IP_;
	gh->gh_owner_pid = get_pid(task_pid(current));
	gh->gh_state = state;
	gh->gh_flags = flags;
	gh->gh_error = 0;
	gh->gh_iflags = 0;
	gfs2_glock_hold(gl);
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

/**
 * gfs2_glock_wait - wait on a glock acquisition
 * @gh: the glock holder
 *
 * Returns: 0 on success
 */

int gfs2_glock_wait(struct gfs2_holder *gh)
{
	unsigned long time1 = jiffies;

	might_sleep();
	wait_on_bit(&gh->gh_iflags, HIF_WAIT, TASK_UNINTERRUPTIBLE);
	if (time_after(jiffies, time1 + HZ)) /* have we waited > a second? */
		/* Lengthen the minimum hold time. */
		gh->gh_gl->gl_hold_time = min(gh->gh_gl->gl_hold_time +
					      GL_GLOCK_HOLD_INCR,
					      GL_GLOCK_MAX_HOLD);
	return gh->gh_error;
}

/**
 * handle_callback - process a demote request
 * @gl: the glock
 * @state: the state the caller wants us to change to
 *
 * There are only two requests that we are going to see in actual
 * practise: LM_ST_SHARED and LM_ST_UNLOCKED
 */

static void handle_callback(struct gfs2_glock *gl, unsigned int state,
			    unsigned long delay, bool remote)
{
	int bit = delay ? GLF_PENDING_DEMOTE : GLF_DEMOTE;

	set_bit(bit, &gl->gl_flags);
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

	BUG_ON(gh->gh_owner_pid == NULL);
	if (test_and_set_bit(HIF_WAIT, &gh->gh_iflags))
		BUG();

	if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		if (test_bit(GLF_LOCK, &gl->gl_flags))
			try_futile = !may_grant(gl, gh);
		if (test_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags))
			goto fail;
	}

	list_for_each_entry(gh2, &gl->gl_holders, gh_list) {
		if (unlikely(gh2->gh_owner_pid == gh->gh_owner_pid &&
		    (gh->gh_gl->gl_ops->go_type != LM_TYPE_FLOCK)))
			goto trap_recursive;
		if (try_futile &&
		    !(gh2->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB))) {
fail:
			gh->gh_error = GLR_TRYFAILED;
			gfs2_holder_wake(gh);
			return;
		}
		if (test_bit(HIF_HOLDER, &gh2->gh_iflags))
			continue;
		if (unlikely((gh->gh_flags & LM_FLAG_PRIORITY) && !insert_pt))
			insert_pt = &gh2->gh_list;
	}
	set_bit(GLF_QUEUED, &gl->gl_flags);
	trace_gfs2_glock_queue(gh, 1);
	gfs2_glstats_inc(gl, GFS2_LKS_QCOUNT);
	gfs2_sbstats_inc(gl, GFS2_LKS_QCOUNT);
	if (likely(insert_pt == NULL)) {
		list_add_tail(&gh->gh_list, &gl->gl_holders);
		if (unlikely(gh->gh_flags & LM_FLAG_PRIORITY))
			goto do_cancel;
		return;
	}
	list_add_tail(&gh->gh_list, insert_pt);
do_cancel:
	gh = list_entry(gl->gl_holders.next, struct gfs2_holder, gh_list);
	if (!(gh->gh_flags & LM_FLAG_PRIORITY)) {
		spin_unlock(&gl->gl_lockref.lock);
		if (sdp->sd_lockstruct.ls_ops->lm_cancel)
			sdp->sd_lockstruct.ls_ops->lm_cancel(gl);
		spin_lock(&gl->gl_lockref.lock);
	}
	return;

trap_recursive:
	pr_err("original: %pSR\n", (void *)gh2->gh_ip);
	pr_err("pid: %d\n", pid_nr(gh2->gh_owner_pid));
	pr_err("lock type: %d req lock state : %d\n",
	       gh2->gh_gl->gl_name.ln_type, gh2->gh_state);
	pr_err("new: %pSR\n", (void *)gh->gh_ip);
	pr_err("pid: %d\n", pid_nr(gh->gh_owner_pid));
	pr_err("lock type: %d req lock state : %d\n",
	       gh->gh_gl->gl_name.ln_type, gh->gh_state);
	gfs2_dump_glock(NULL, gl);
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
	struct gfs2_sbd *sdp = gl->gl_name.ln_sbd;
	int error = 0;

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	if (test_bit(GLF_LRU, &gl->gl_flags))
		gfs2_glock_remove_from_lru(gl);

	spin_lock(&gl->gl_lockref.lock);
	add_to_queue(gh);
	if (unlikely((LM_FLAG_NOEXP & gh->gh_flags) &&
		     test_and_clear_bit(GLF_FROZEN, &gl->gl_flags))) {
		set_bit(GLF_REPLY_PENDING, &gl->gl_flags);
		gl->gl_lockref.count++;
		__gfs2_glock_queue_work(gl, 0);
	}
	run_queue(gl, 1);
	spin_unlock(&gl->gl_lockref.lock);

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

/**
 * gfs2_glock_dq - dequeue a struct gfs2_holder from a glock (release a glock)
 * @gh: the glock holder
 *
 */

void gfs2_glock_dq(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	unsigned delay = 0;
	int fast_path = 0;

	spin_lock(&gl->gl_lockref.lock);
	if (gh->gh_flags & GL_NOCACHE)
		handle_callback(gl, LM_ST_UNLOCKED, 0, false);

	list_del_init(&gh->gh_list);
	clear_bit(HIF_HOLDER, &gh->gh_iflags);
	if (find_first_holder(gl) == NULL) {
		if (glops->go_unlock) {
			GLOCK_BUG_ON(gl, test_and_set_bit(GLF_LOCK, &gl->gl_flags));
			spin_unlock(&gl->gl_lockref.lock);
			glops->go_unlock(gh);
			spin_lock(&gl->gl_lockref.lock);
			clear_bit(GLF_LOCK, &gl->gl_flags);
		}
		if (list_empty(&gl->gl_holders) &&
		    !test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
		    !test_bit(GLF_DEMOTE, &gl->gl_flags))
			fast_path = 1;
	}
	if (!test_bit(GLF_LFLUSH, &gl->gl_flags) && demote_ok(gl) &&
	    (glops->go_flags & GLOF_LRU))
		gfs2_glock_add_to_lru(gl);

	trace_gfs2_glock_queue(gh, 0);
	if (unlikely(!fast_path)) {
		gl->gl_lockref.count++;
		if (test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
		    !test_bit(GLF_DEMOTE, &gl->gl_flags) &&
		    gl->gl_name.ln_type == LM_TYPE_INODE)
			delay = gl->gl_hold_time;
		__gfs2_glock_queue_work(gl, delay);
	}
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
 * nq_m_sync - synchonously acquire more than one glock in deadlock free order
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
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
		p[x]->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);

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
		ghs->gh_flags &= ~(LM_FLAG_TRY | GL_ASYNC);
		return gfs2_glock_nq(ghs);
	default:
		if (num_gh <= 4)
			break;
		pph = kmalloc(num_gh * sizeof(struct gfs2_holder *), GFP_NOFS);
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
	unsigned long holdtime;
	unsigned long now = jiffies;

	gfs2_glock_hold(gl);
	holdtime = gl->gl_tchange + gl->gl_hold_time;
	if (test_bit(GLF_QUEUED, &gl->gl_flags) &&
	    gl->gl_name.ln_type == LM_TYPE_INODE) {
		if (time_before(now, holdtime))
			delay = holdtime - now;
		if (test_bit(GLF_REPLY_PENDING, &gl->gl_flags))
			delay = gl->gl_hold_time;
	}

	spin_lock(&gl->gl_lockref.lock);
	handle_callback(gl, state, delay, true);
	__gfs2_glock_queue_work(gl, delay);
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
			set_bit(GLF_FROZEN, &gl->gl_flags);
			spin_unlock(&gl->gl_lockref.lock);
			return;
		}
	}

	gl->gl_lockref.count++;
	set_bit(GLF_REPLY_PENDING, &gl->gl_flags);
	__gfs2_glock_queue_work(gl, 0);
	spin_unlock(&gl->gl_lockref.lock);
}

static int glock_cmp(void *priv, struct list_head *a, struct list_head *b)
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

static void gfs2_dispose_glock_lru(struct list_head *list)
__releases(&lru_lock)
__acquires(&lru_lock)
{
	struct gfs2_glock *gl;

	list_sort(NULL, list, glock_cmp);

	while(!list_empty(list)) {
		gl = list_entry(list->next, struct gfs2_glock, gl_lru);
		list_del_init(&gl->gl_lru);
		if (!spin_trylock(&gl->gl_lockref.lock)) {
add_back_to_lru:
			list_add(&gl->gl_lru, &lru_list);
			atomic_inc(&lru_count);
			continue;
		}
		if (test_and_set_bit(GLF_LOCK, &gl->gl_flags)) {
			spin_unlock(&gl->gl_lockref.lock);
			goto add_back_to_lru;
		}
		clear_bit(GLF_LRU, &gl->gl_flags);
		gl->gl_lockref.count++;
		if (demote_ok(gl))
			handle_callback(gl, LM_ST_UNLOCKED, 0, false);
		WARN_ON(!test_and_clear_bit(GLF_LOCK, &gl->gl_flags));
		__gfs2_glock_queue_work(gl, 0);
		spin_unlock(&gl->gl_lockref.lock);
		cond_resched_lock(&lru_lock);
	}
}

/**
 * gfs2_scan_glock_lru - Scan the LRU looking for locks to demote
 * @nr: The number of entries to scan
 *
 * This function selects the entries on the LRU which are able to
 * be demoted, and then kicks off the process by calling
 * gfs2_dispose_glock_lru() above.
 */

static long gfs2_scan_glock_lru(int nr)
{
	struct gfs2_glock *gl;
	LIST_HEAD(skipped);
	LIST_HEAD(dispose);
	long freed = 0;

	spin_lock(&lru_lock);
	while ((nr-- >= 0) && !list_empty(&lru_list)) {
		gl = list_entry(lru_list.next, struct gfs2_glock, gl_lru);

		/* Test for being demotable */
		if (!test_bit(GLF_LOCK, &gl->gl_flags)) {
			list_move(&gl->gl_lru, &dispose);
			atomic_dec(&lru_count);
			freed++;
			continue;
		}

		list_move(&gl->gl_lru, &skipped);
	}
	list_splice(&skipped, &lru_list);
	if (!list_empty(&dispose))
		gfs2_dispose_glock_lru(&dispose);
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

static struct shrinker glock_shrinker = {
	.seeks = DEFAULT_SEEKS,
	.count_objects = gfs2_glock_shrink_count,
	.scan_objects = gfs2_glock_shrink_scan,
};

/**
 * examine_bucket - Call a function for glock in a hash bucket
 * @examiner: the function
 * @sdp: the filesystem
 * @bucket: the bucket
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

		while ((gl = rhashtable_walk_next(&iter)) && !IS_ERR(gl))
			if (gl->gl_name.ln_sbd == sdp &&
			    lockref_get_not_dead(&gl->gl_lockref))
				examiner(gl);

		rhashtable_walk_stop(&iter);
	} while (cond_resched(), gl == ERR_PTR(-EAGAIN));

	rhashtable_walk_exit(&iter);
}

/**
 * thaw_glock - thaw out a glock which has an unprocessed reply waiting
 * @gl: The glock to thaw
 *
 */

static void thaw_glock(struct gfs2_glock *gl)
{
	if (!test_and_clear_bit(GLF_FROZEN, &gl->gl_flags)) {
		gfs2_glock_put(gl);
		return;
	}
	set_bit(GLF_REPLY_PENDING, &gl->gl_flags);
	gfs2_glock_queue_work(gl, 0);
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
	if (gl->gl_state != LM_ST_UNLOCKED)
		handle_callback(gl, LM_ST_UNLOCKED, 0, false);
	__gfs2_glock_queue_work(gl, 0);
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

static void dump_glock(struct seq_file *seq, struct gfs2_glock *gl)
{
	spin_lock(&gl->gl_lockref.lock);
	gfs2_dump_glock(seq, gl);
	spin_unlock(&gl->gl_lockref.lock);
}

static void dump_glock_func(struct gfs2_glock *gl)
{
	dump_glock(NULL, gl);
}

/**
 * gfs2_gl_hash_clear - Empty out the glock hash table
 * @sdp: the filesystem
 * @wait: wait until it's all gone
 *
 * Called when unmounting the filesystem.
 */

void gfs2_gl_hash_clear(struct gfs2_sbd *sdp)
{
	set_bit(SDF_SKIP_DLM_UNLOCK, &sdp->sd_flags);
	flush_workqueue(glock_workqueue);
	glock_hash_walk(clear_glock, sdp);
	flush_workqueue(glock_workqueue);
	wait_event_timeout(sdp->sd_glock_wait,
			   atomic_read(&sdp->sd_glock_disposal) == 0,
			   HZ * 600);
	glock_hash_walk(dump_glock_func, sdp);
}

void gfs2_glock_finish_truncate(struct gfs2_inode *ip)
{
	struct gfs2_glock *gl = ip->i_gl;
	int ret;

	ret = gfs2_truncatei_resume(ip);
	gfs2_assert_withdraw(gl->gl_name.ln_sbd, ret == 0);

	spin_lock(&gl->gl_lockref.lock);
	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl, 1);
	spin_unlock(&gl->gl_lockref.lock);
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
	if (flags & LM_FLAG_PRIORITY)
		*p++ = 'p';
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
	if (test_bit(HIF_FIRST, &iflags))
		*p++ = 'F';
	*p = 0;
	return buf;
}

/**
 * dump_holder - print information about a glock holder
 * @seq: the seq_file struct
 * @gh: the glock holder
 *
 */

static void dump_holder(struct seq_file *seq, const struct gfs2_holder *gh)
{
	struct task_struct *gh_owner = NULL;
	char flags_buf[32];

	rcu_read_lock();
	if (gh->gh_owner_pid)
		gh_owner = pid_task(gh->gh_owner_pid, PIDTYPE_PID);
	gfs2_print_dbg(seq, " H: s:%s f:%s e:%d p:%ld [%s] %pS\n",
		       state2str(gh->gh_state),
		       hflags2str(flags_buf, gh->gh_flags, gh->gh_iflags),
		       gh->gh_error,
		       gh->gh_owner_pid ? (long)pid_nr(gh->gh_owner_pid) : -1,
		       gh_owner ? gh_owner->comm : "(ended)",
		       (void *)gh->gh_ip);
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
	if (test_bit(GLF_REPLY_PENDING, gflags))
		*p++ = 'r';
	if (test_bit(GLF_INITIAL, gflags))
		*p++ = 'I';
	if (test_bit(GLF_FROZEN, gflags))
		*p++ = 'F';
	if (test_bit(GLF_QUEUED, gflags))
		*p++ = 'q';
	if (test_bit(GLF_LRU, gflags))
		*p++ = 'L';
	if (gl->gl_object)
		*p++ = 'o';
	if (test_bit(GLF_BLOCKING, gflags))
		*p++ = 'b';
	*p = 0;
	return buf;
}

/**
 * gfs2_dump_glock - print information about a glock
 * @seq: The seq_file struct
 * @gl: the glock
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

void gfs2_dump_glock(struct seq_file *seq, const struct gfs2_glock *gl)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	unsigned long long dtime;
	const struct gfs2_holder *gh;
	char gflags_buf[32];

	dtime = jiffies - gl->gl_demote_time;
	dtime *= 1000000/HZ; /* demote time in uSec */
	if (!test_bit(GLF_DEMOTE, &gl->gl_flags))
		dtime = 0;
	gfs2_print_dbg(seq, "G:  s:%s n:%u/%llx f:%s t:%s d:%s/%llu a:%d v:%d r:%d m:%ld\n",
		  state2str(gl->gl_state),
		  gl->gl_name.ln_type,
		  (unsigned long long)gl->gl_name.ln_number,
		  gflags2str(gflags_buf, gl),
		  state2str(gl->gl_target),
		  state2str(gl->gl_demote_state), dtime,
		  atomic_read(&gl->gl_ail_count),
		  atomic_read(&gl->gl_revokes),
		  (int)gl->gl_lockref.count, gl->gl_hold_time);

	list_for_each_entry(gh, &gl->gl_holders, gh_list)
		dump_holder(seq, gh);

	if (gl->gl_state != LM_ST_UNLOCKED && glops->go_dump)
		glops->go_dump(seq, gl);
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

	glock_workqueue = alloc_workqueue("glock_workqueue", WQ_MEM_RECLAIM |
					  WQ_HIGHPRI | WQ_FREEZABLE, 0);
	if (!glock_workqueue) {
		rhashtable_destroy(&gl_hash_table);
		return -ENOMEM;
	}
	gfs2_delete_workqueue = alloc_workqueue("delete_workqueue",
						WQ_MEM_RECLAIM | WQ_FREEZABLE,
						0);
	if (!gfs2_delete_workqueue) {
		destroy_workqueue(glock_workqueue);
		rhashtable_destroy(&gl_hash_table);
		return -ENOMEM;
	}

	ret = register_shrinker(&glock_shrinker);
	if (ret) {
		destroy_workqueue(gfs2_delete_workqueue);
		destroy_workqueue(glock_workqueue);
		rhashtable_destroy(&gl_hash_table);
		return ret;
	}

	for (i = 0; i < GLOCK_WAIT_TABLE_SIZE; i++)
		init_waitqueue_head(glock_wait_table + i);

	return 0;
}

void gfs2_glock_exit(void)
{
	unregister_shrinker(&glock_shrinker);
	rhashtable_destroy(&gl_hash_table);
	destroy_workqueue(glock_workqueue);
	destroy_workqueue(gfs2_delete_workqueue);
}

static void gfs2_glock_iter_next(struct gfs2_glock_iter *gi)
{
	while ((gi->gl = rhashtable_walk_next(&gi->hti))) {
		if (IS_ERR(gi->gl)) {
			if (PTR_ERR(gi->gl) == -EAGAIN)
				continue;
			gi->gl = NULL;
			return;
		}
		/* Skip entries for other sb and dead entries */
		if (gi->sdp == gi->gl->gl_name.ln_sbd &&
		    !__lockref_is_dead(&gi->gl->gl_lockref))
			return;
	}
}

static void *gfs2_glock_seq_start(struct seq_file *seq, loff_t *pos)
	__acquires(RCU)
{
	struct gfs2_glock_iter *gi = seq->private;
	loff_t n = *pos;

	rhashtable_walk_enter(&gl_hash_table, &gi->hti);
	if (rhashtable_walk_start_check(&gi->hti) != 0)
		return NULL;

	do {
		gfs2_glock_iter_next(gi);
	} while (gi->gl && n--);

	gi->last_pos = *pos;

	return gi->gl;
}

static void *gfs2_glock_seq_next(struct seq_file *seq, void *iter_ptr,
				 loff_t *pos)
{
	struct gfs2_glock_iter *gi = seq->private;

	(*pos)++;
	gi->last_pos = *pos;
	gfs2_glock_iter_next(gi);

	return gi->gl;
}

static void gfs2_glock_seq_stop(struct seq_file *seq, void *iter_ptr)
	__releases(RCU)
{
	struct gfs2_glock_iter *gi = seq->private;

	gi->gl = NULL;
	rhashtable_walk_stop(&gi->hti);
	rhashtable_walk_exit(&gi->hti);
}

static int gfs2_glock_seq_show(struct seq_file *seq, void *iter_ptr)
{
	dump_glock(seq, iter_ptr);
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

static const struct seq_operations gfs2_sbstats_seq_ops = {
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
		gi->gl = NULL;
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

	gi->gl = NULL;
	return seq_release_private(inode, file);
}

static int gfs2_glstats_open(struct inode *inode, struct file *file)
{
	return __gfs2_glocks_open(inode, file, &gfs2_glstats_seq_ops);
}

static int gfs2_sbstats_open(struct inode *inode, struct file *file)
{
	int ret = seq_open(file, &gfs2_sbstats_seq_ops);
	if (ret == 0) {
		struct seq_file *seq = file->private_data;
		seq->private = inode->i_private;  /* sdp */
	}
	return ret;
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

static const struct file_operations gfs2_sbstats_fops = {
	.owner   = THIS_MODULE,
	.open	 = gfs2_sbstats_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

int gfs2_create_debugfs_file(struct gfs2_sbd *sdp)
{
	struct dentry *dent;

	dent = debugfs_create_dir(sdp->sd_table_name, gfs2_root);
	if (IS_ERR_OR_NULL(dent))
		goto fail;
	sdp->debugfs_dir = dent;

	dent = debugfs_create_file("glocks",
				   S_IFREG | S_IRUGO,
				   sdp->debugfs_dir, sdp,
				   &gfs2_glocks_fops);
	if (IS_ERR_OR_NULL(dent))
		goto fail;
	sdp->debugfs_dentry_glocks = dent;

	dent = debugfs_create_file("glstats",
				   S_IFREG | S_IRUGO,
				   sdp->debugfs_dir, sdp,
				   &gfs2_glstats_fops);
	if (IS_ERR_OR_NULL(dent))
		goto fail;
	sdp->debugfs_dentry_glstats = dent;

	dent = debugfs_create_file("sbstats",
				   S_IFREG | S_IRUGO,
				   sdp->debugfs_dir, sdp,
				   &gfs2_sbstats_fops);
	if (IS_ERR_OR_NULL(dent))
		goto fail;
	sdp->debugfs_dentry_sbstats = dent;

	return 0;
fail:
	gfs2_delete_debugfs_file(sdp);
	return dent ? PTR_ERR(dent) : -ENOMEM;
}

void gfs2_delete_debugfs_file(struct gfs2_sbd *sdp)
{
	if (sdp->debugfs_dir) {
		if (sdp->debugfs_dentry_glocks) {
			debugfs_remove(sdp->debugfs_dentry_glocks);
			sdp->debugfs_dentry_glocks = NULL;
		}
		if (sdp->debugfs_dentry_glstats) {
			debugfs_remove(sdp->debugfs_dentry_glstats);
			sdp->debugfs_dentry_glstats = NULL;
		}
		if (sdp->debugfs_dentry_sbstats) {
			debugfs_remove(sdp->debugfs_dentry_sbstats);
			sdp->debugfs_dentry_sbstats = NULL;
		}
		debugfs_remove(sdp->debugfs_dir);
		sdp->debugfs_dir = NULL;
	}
}

int gfs2_register_debugfs(void)
{
	gfs2_root = debugfs_create_dir("gfs2", NULL);
	if (IS_ERR(gfs2_root))
		return PTR_ERR(gfs2_root);
	return gfs2_root ? 0 : -ENOMEM;
}

void gfs2_unregister_debugfs(void)
{
	debugfs_remove(gfs2_root);
	gfs2_root = NULL;
}
