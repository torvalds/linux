/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <linux/sort.h>
#include <linux/jhash.h>
#include <linux/kallsyms.h>
#include <linux/gfs2_ondisk.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>

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

struct gfs2_gl_hash_bucket {
        struct hlist_head hb_list;
};

struct gfs2_glock_iter {
	int hash;			/* hash bucket index         */
	struct gfs2_sbd *sdp;		/* incore superblock         */
	struct gfs2_glock *gl;		/* current glock struct      */
	char string[512];		/* scratch space             */
};

typedef void (*glock_examiner) (struct gfs2_glock * gl);

static int gfs2_dump_lockstate(struct gfs2_sbd *sdp);
static int __dump_glock(struct seq_file *seq, const struct gfs2_glock *gl);
#define GLOCK_BUG_ON(gl,x) do { if (unlikely(x)) { __dump_glock(NULL, gl); BUG(); } } while(0)
static void do_xmote(struct gfs2_glock *gl, struct gfs2_holder *gh, unsigned int target);

static struct dentry *gfs2_root;
static struct workqueue_struct *glock_workqueue;
struct workqueue_struct *gfs2_delete_workqueue;
static LIST_HEAD(lru_list);
static atomic_t lru_count = ATOMIC_INIT(0);
static DEFINE_SPINLOCK(lru_lock);

#define GFS2_GL_HASH_SHIFT      15
#define GFS2_GL_HASH_SIZE       (1 << GFS2_GL_HASH_SHIFT)
#define GFS2_GL_HASH_MASK       (GFS2_GL_HASH_SIZE - 1)

static struct gfs2_gl_hash_bucket gl_hash_table[GFS2_GL_HASH_SIZE];
static struct dentry *gfs2_root;

/*
 * Despite what you might think, the numbers below are not arbitrary :-)
 * They are taken from the ipv4 routing hash code, which is well tested
 * and thus should be nearly optimal. Later on we might tweek the numbers
 * but for now this should be fine.
 *
 * The reason for putting the locks in a separate array from the list heads
 * is that we can have fewer locks than list heads and save memory. We use
 * the same hash function for both, but with a different hash mask.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK) || \
	defined(CONFIG_PROVE_LOCKING)

#ifdef CONFIG_LOCKDEP
# define GL_HASH_LOCK_SZ        256
#else
# if NR_CPUS >= 32
#  define GL_HASH_LOCK_SZ       4096
# elif NR_CPUS >= 16
#  define GL_HASH_LOCK_SZ       2048
# elif NR_CPUS >= 8
#  define GL_HASH_LOCK_SZ       1024
# elif NR_CPUS >= 4
#  define GL_HASH_LOCK_SZ       512
# else
#  define GL_HASH_LOCK_SZ       256
# endif
#endif

/* We never want more locks than chains */
#if GFS2_GL_HASH_SIZE < GL_HASH_LOCK_SZ
# undef GL_HASH_LOCK_SZ
# define GL_HASH_LOCK_SZ GFS2_GL_HASH_SIZE
#endif

static rwlock_t gl_hash_locks[GL_HASH_LOCK_SZ];

static inline rwlock_t *gl_lock_addr(unsigned int x)
{
	return &gl_hash_locks[x & (GL_HASH_LOCK_SZ-1)];
}
#else /* not SMP, so no spinlocks required */
static inline rwlock_t *gl_lock_addr(unsigned int x)
{
	return NULL;
}
#endif

/**
 * gl_hash() - Turn glock number into hash bucket number
 * @lock: The glock number
 *
 * Returns: The number of the corresponding hash bucket
 */

static unsigned int gl_hash(const struct gfs2_sbd *sdp,
			    const struct lm_lockname *name)
{
	unsigned int h;

	h = jhash(&name->ln_number, sizeof(u64), 0);
	h = jhash(&name->ln_type, sizeof(unsigned int), h);
	h = jhash(&sdp, sizeof(struct gfs2_sbd *), h);
	h &= GFS2_GL_HASH_MASK;

	return h;
}

/**
 * glock_free() - Perform a few checks and then release struct gfs2_glock
 * @gl: The glock to release
 *
 * Also calls lock module to release its internal structure for this glock.
 *
 */

static void glock_free(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct address_space *mapping = gfs2_glock2aspace(gl);
	struct kmem_cache *cachep = gfs2_glock_cachep;

	GLOCK_BUG_ON(gl, mapping && mapping->nrpages);
	trace_gfs2_glock_put(gl);
	if (mapping)
		cachep = gfs2_glock_aspace_cachep;
	sdp->sd_lockstruct.ls_ops->lm_put_lock(cachep, gl);
}

/**
 * gfs2_glock_hold() - increment reference count on glock
 * @gl: The glock to hold
 *
 */

void gfs2_glock_hold(struct gfs2_glock *gl)
{
	GLOCK_BUG_ON(gl, atomic_read(&gl->gl_ref) == 0);
	atomic_inc(&gl->gl_ref);
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

/**
 * gfs2_glock_schedule_for_reclaim - Add a glock to the reclaim list
 * @gl: the glock
 *
 */

static void gfs2_glock_schedule_for_reclaim(struct gfs2_glock *gl)
{
	int may_reclaim;
	may_reclaim = (demote_ok(gl) &&
		       (atomic_read(&gl->gl_ref) == 1 ||
			(gl->gl_name.ln_type == LM_TYPE_INODE &&
			 atomic_read(&gl->gl_ref) <= 2)));
	spin_lock(&lru_lock);
	if (list_empty(&gl->gl_lru) && may_reclaim) {
		list_add_tail(&gl->gl_lru, &lru_list);
		atomic_inc(&lru_count);
	}
	spin_unlock(&lru_lock);
}

/**
 * gfs2_glock_put_nolock() - Decrement reference count on glock
 * @gl: The glock to put
 *
 * This function should only be used if the caller has its own reference
 * to the glock, in addition to the one it is dropping.
 */

void gfs2_glock_put_nolock(struct gfs2_glock *gl)
{
	if (atomic_dec_and_test(&gl->gl_ref))
		GLOCK_BUG_ON(gl, 1);
	gfs2_glock_schedule_for_reclaim(gl);
}

/**
 * gfs2_glock_put() - Decrement reference count on glock
 * @gl: The glock to put
 *
 */

int gfs2_glock_put(struct gfs2_glock *gl)
{
	int rv = 0;

	write_lock(gl_lock_addr(gl->gl_hash));
	if (atomic_dec_and_lock(&gl->gl_ref, &lru_lock)) {
		hlist_del(&gl->gl_list);
		if (!list_empty(&gl->gl_lru)) {
			list_del_init(&gl->gl_lru);
			atomic_dec(&lru_count);
		}
		spin_unlock(&lru_lock);
		write_unlock(gl_lock_addr(gl->gl_hash));
		GLOCK_BUG_ON(gl, !list_empty(&gl->gl_holders));
		glock_free(gl);
		rv = 1;
		goto out;
	}
	spin_lock(&gl->gl_spin);
	gfs2_glock_schedule_for_reclaim(gl);
	spin_unlock(&gl->gl_spin);
	write_unlock(gl_lock_addr(gl->gl_hash));
out:
	return rv;
}

/**
 * search_bucket() - Find struct gfs2_glock by lock number
 * @bucket: the bucket to search
 * @name: The lock name
 *
 * Returns: NULL, or the struct gfs2_glock with the requested number
 */

static struct gfs2_glock *search_bucket(unsigned int hash,
					const struct gfs2_sbd *sdp,
					const struct lm_lockname *name)
{
	struct gfs2_glock *gl;
	struct hlist_node *h;

	hlist_for_each_entry(gl, h, &gl_hash_table[hash].hb_list, gl_list) {
		if (!lm_name_equal(&gl->gl_name, name))
			continue;
		if (gl->gl_sbd != sdp)
			continue;

		atomic_inc(&gl->gl_ref);

		return gl;
	}

	return NULL;
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
	smp_mb__after_clear_bit();
	wake_up_bit(&gh->gh_iflags, HIF_WAIT);
}

/**
 * do_error - Something unexpected has happened during a lock request
 *
 */

static inline void do_error(struct gfs2_glock *gl, const int ret)
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
__releases(&gl->gl_spin)
__acquires(&gl->gl_spin)
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
				spin_unlock(&gl->gl_spin);
				/* FIXME: eliminate this eventually */
				ret = glops->go_lock(gh);
				spin_lock(&gl->gl_spin);
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
		if (held2)
			gfs2_glock_hold(gl);
		else
			gfs2_glock_put_nolock(gl);
	}

	gl->gl_state = new_state;
	gl->gl_tchange = jiffies;
}

static void gfs2_demote_wake(struct gfs2_glock *gl)
{
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
	clear_bit(GLF_DEMOTE, &gl->gl_flags);
	smp_mb__after_clear_bit();
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

	spin_lock(&gl->gl_spin);
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
			printk(KERN_ERR "GFS2: wanted %u got %u\n", gl->gl_target, state);
			GLOCK_BUG_ON(gl, 1);
		}
		spin_unlock(&gl->gl_spin);
		return;
	}

	/* Fast path - we got what we asked for */
	if (test_and_clear_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags))
		gfs2_demote_wake(gl);
	if (state != LM_ST_UNLOCKED) {
		if (glops->go_xmote_bh) {
			spin_unlock(&gl->gl_spin);
			rv = glops->go_xmote_bh(gl, gh);
			spin_lock(&gl->gl_spin);
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
	spin_unlock(&gl->gl_spin);
}

static unsigned int gfs2_lm_lock(struct gfs2_sbd *sdp, void *lock,
				 unsigned int req_state,
				 unsigned int flags)
{
	int ret = LM_OUT_ERROR;

	if (!sdp->sd_lockstruct.ls_ops->lm_lock)
		return req_state == LM_ST_UNLOCKED ? 0 : req_state;

	if (likely(!test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		ret = sdp->sd_lockstruct.ls_ops->lm_lock(lock,
							 req_state, flags);
	return ret;
}

/**
 * do_xmote - Calls the DLM to change the state of a lock
 * @gl: The lock state
 * @gh: The holder (only for promotes)
 * @target: The target lock state
 *
 */

static void do_xmote(struct gfs2_glock *gl, struct gfs2_holder *gh, unsigned int target)
__releases(&gl->gl_spin)
__acquires(&gl->gl_spin)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	unsigned int lck_flags = gh ? gh->gh_flags : 0;
	int ret;

	lck_flags &= (LM_FLAG_TRY | LM_FLAG_TRY_1CB | LM_FLAG_NOEXP |
		      LM_FLAG_PRIORITY);
	BUG_ON(gl->gl_state == target);
	BUG_ON(gl->gl_state == gl->gl_target);
	if ((target == LM_ST_UNLOCKED || target == LM_ST_DEFERRED) &&
	    glops->go_inval) {
		set_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags);
		do_error(gl, 0); /* Fail queued try locks */
	}
	spin_unlock(&gl->gl_spin);
	if (glops->go_xmote_th)
		glops->go_xmote_th(gl);
	if (test_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags))
		glops->go_inval(gl, target == LM_ST_DEFERRED ? 0 : DIO_METADATA);
	clear_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags);

	gfs2_glock_hold(gl);
	if (target != LM_ST_UNLOCKED && (gl->gl_state == LM_ST_SHARED ||
	    gl->gl_state == LM_ST_DEFERRED) &&
	    !(lck_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)))
		lck_flags |= LM_FLAG_TRY_1CB;
	ret = gfs2_lm_lock(sdp, gl, target, lck_flags);

	if (!(ret & LM_OUT_ASYNC)) {
		finish_xmote(gl, ret);
		if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
			gfs2_glock_put(gl);
	} else {
		GLOCK_BUG_ON(gl, ret != LM_OUT_ASYNC);
	}
	spin_lock(&gl->gl_spin);
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
__releases(&gl->gl_spin)
__acquires(&gl->gl_spin)
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
	smp_mb__after_clear_bit();
	gfs2_glock_hold(gl);
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
		gfs2_glock_put_nolock(gl);
	return;

out_unlock:
	clear_bit(GLF_LOCK, &gl->gl_flags);
	smp_mb__after_clear_bit();
	return;
}

static void delete_work_func(struct work_struct *work)
{
	struct gfs2_glock *gl = container_of(work, struct gfs2_glock, gl_delete);
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct gfs2_inode *ip = NULL;
	struct inode *inode;
	u64 no_addr = 0;

	spin_lock(&gl->gl_spin);
	ip = (struct gfs2_inode *)gl->gl_object;
	if (ip)
		no_addr = ip->i_no_addr;
	spin_unlock(&gl->gl_spin);
	if (ip) {
		inode = gfs2_ilookup(sdp->sd_vfs, no_addr);
		if (inode) {
			d_prune_aliases(inode);
			iput(inode);
		}
	}
	gfs2_glock_put(gl);
}

static void glock_work_func(struct work_struct *work)
{
	unsigned long delay = 0;
	struct gfs2_glock *gl = container_of(work, struct gfs2_glock, gl_work.work);
	int drop_ref = 0;

	if (test_and_clear_bit(GLF_REPLY_PENDING, &gl->gl_flags)) {
		finish_xmote(gl, gl->gl_reply);
		drop_ref = 1;
	}
	spin_lock(&gl->gl_spin);
	if (test_and_clear_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
	    gl->gl_state != LM_ST_UNLOCKED &&
	    gl->gl_demote_state != LM_ST_EXCLUSIVE) {
		unsigned long holdtime, now = jiffies;
		holdtime = gl->gl_tchange + gl->gl_ops->go_min_hold_time;
		if (time_before(now, holdtime))
			delay = holdtime - now;
		set_bit(delay ? GLF_PENDING_DEMOTE : GLF_DEMOTE, &gl->gl_flags);
	}
	run_queue(gl, 0);
	spin_unlock(&gl->gl_spin);
	if (!delay ||
	    queue_delayed_work(glock_workqueue, &gl->gl_work, delay) == 0)
		gfs2_glock_put(gl);
	if (drop_ref)
		gfs2_glock_put(gl);
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
	struct lm_lockname name = { .ln_number = number, .ln_type = glops->go_type };
	struct gfs2_glock *gl, *tmp;
	unsigned int hash = gl_hash(sdp, &name);
	struct address_space *mapping;

	read_lock(gl_lock_addr(hash));
	gl = search_bucket(hash, sdp, &name);
	read_unlock(gl_lock_addr(hash));

	*glp = gl;
	if (gl)
		return 0;
	if (!create)
		return -ENOENT;

	if (glops->go_flags & GLOF_ASPACE)
		gl = kmem_cache_alloc(gfs2_glock_aspace_cachep, GFP_KERNEL);
	else
		gl = kmem_cache_alloc(gfs2_glock_cachep, GFP_KERNEL);
	if (!gl)
		return -ENOMEM;

	atomic_inc(&sdp->sd_glock_disposal);
	gl->gl_flags = 0;
	gl->gl_name = name;
	atomic_set(&gl->gl_ref, 1);
	gl->gl_state = LM_ST_UNLOCKED;
	gl->gl_target = LM_ST_UNLOCKED;
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
	gl->gl_hash = hash;
	gl->gl_ops = glops;
	snprintf(gl->gl_strname, GDLM_STRNAME_BYTES, "%8x%16llx", name.ln_type, (unsigned long long)number);
	memset(&gl->gl_lksb, 0, sizeof(struct dlm_lksb));
	gl->gl_lksb.sb_lvbptr = gl->gl_lvb;
	gl->gl_tchange = jiffies;
	gl->gl_object = NULL;
	gl->gl_sbd = sdp;
	INIT_DELAYED_WORK(&gl->gl_work, glock_work_func);
	INIT_WORK(&gl->gl_delete, delete_work_func);

	mapping = gfs2_glock2aspace(gl);
	if (mapping) {
                mapping->a_ops = &gfs2_meta_aops;
		mapping->host = s->s_bdev->bd_inode;
		mapping->flags = 0;
		mapping_set_gfp_mask(mapping, GFP_NOFS);
		mapping->assoc_mapping = NULL;
		mapping->backing_dev_info = s->s_bdi;
		mapping->writeback_index = 0;
	}

	write_lock(gl_lock_addr(hash));
	tmp = search_bucket(hash, sdp, &name);
	if (tmp) {
		write_unlock(gl_lock_addr(hash));
		glock_free(gl);
		gl = tmp;
	} else {
		hlist_add_head(&gl->gl_list, &gl_hash_table[hash].hb_list);
		write_unlock(gl_lock_addr(hash));
	}

	*glp = gl;

	return 0;
}

/**
 * gfs2_holder_init - initialize a struct gfs2_holder in the default way
 * @gl: the glock
 * @state: the state we're requesting
 * @flags: the modifier flags
 * @gh: the holder structure
 *
 */

void gfs2_holder_init(struct gfs2_glock *gl, unsigned int state, unsigned flags,
		      struct gfs2_holder *gh)
{
	INIT_LIST_HEAD(&gh->gh_list);
	gh->gh_gl = gl;
	gh->gh_ip = (unsigned long)__builtin_return_address(0);
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

void gfs2_holder_reinit(unsigned int state, unsigned flags, struct gfs2_holder *gh)
{
	gh->gh_state = state;
	gh->gh_flags = flags;
	gh->gh_iflags = 0;
	gh->gh_ip = (unsigned long)__builtin_return_address(0);
	if (gh->gh_owner_pid)
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
	gh->gh_gl = NULL;
	gh->gh_ip = 0;
}

/**
 * gfs2_glock_holder_wait
 * @word: unused
 *
 * This function and gfs2_glock_demote_wait both show up in the WCHAN
 * field. Thus I've separated these otherwise identical functions in
 * order to be more informative to the user.
 */

static int gfs2_glock_holder_wait(void *word)
{
        schedule();
        return 0;
}

static int gfs2_glock_demote_wait(void *word)
{
	schedule();
	return 0;
}

static void wait_on_holder(struct gfs2_holder *gh)
{
	might_sleep();
	wait_on_bit(&gh->gh_iflags, HIF_WAIT, gfs2_glock_holder_wait, TASK_UNINTERRUPTIBLE);
}

static void wait_on_demote(struct gfs2_glock *gl)
{
	might_sleep();
	wait_on_bit(&gl->gl_flags, GLF_DEMOTE, gfs2_glock_demote_wait, TASK_UNINTERRUPTIBLE);
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
			    unsigned long delay)
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
		gl->gl_ops->go_callback(gl);
	trace_gfs2_demote_rq(gl);
}

/**
 * gfs2_glock_wait - wait on a glock acquisition
 * @gh: the glock holder
 *
 * Returns: 0 on success
 */

int gfs2_glock_wait(struct gfs2_holder *gh)
{
	wait_on_holder(gh);
	return gh->gh_error;
}

void gfs2_print_dbg(struct seq_file *seq, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (seq) {
		struct gfs2_glock_iter *gi = seq->private;
		vsprintf(gi->string, fmt, args);
		seq_printf(seq, gi->string);
	} else {
		printk(KERN_ERR " ");
		vprintk(fmt, args);
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
__releases(&gl->gl_spin)
__acquires(&gl->gl_spin)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	struct list_head *insert_pt = NULL;
	struct gfs2_holder *gh2;
	int try_lock = 0;

	BUG_ON(gh->gh_owner_pid == NULL);
	if (test_and_set_bit(HIF_WAIT, &gh->gh_iflags))
		BUG();

	if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		if (test_bit(GLF_LOCK, &gl->gl_flags))
			try_lock = 1;
		if (test_bit(GLF_INVALIDATE_IN_PROGRESS, &gl->gl_flags))
			goto fail;
	}

	list_for_each_entry(gh2, &gl->gl_holders, gh_list) {
		if (unlikely(gh2->gh_owner_pid == gh->gh_owner_pid &&
		    (gh->gh_gl->gl_ops->go_type != LM_TYPE_FLOCK)))
			goto trap_recursive;
		if (try_lock &&
		    !(gh2->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) &&
		    !may_grant(gl, gh)) {
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
	if (likely(insert_pt == NULL)) {
		list_add_tail(&gh->gh_list, &gl->gl_holders);
		if (unlikely(gh->gh_flags & LM_FLAG_PRIORITY))
			goto do_cancel;
		return;
	}
	trace_gfs2_glock_queue(gh, 1);
	list_add_tail(&gh->gh_list, insert_pt);
do_cancel:
	gh = list_entry(gl->gl_holders.next, struct gfs2_holder, gh_list);
	if (!(gh->gh_flags & LM_FLAG_PRIORITY)) {
		spin_unlock(&gl->gl_spin);
		if (sdp->sd_lockstruct.ls_ops->lm_cancel)
			sdp->sd_lockstruct.ls_ops->lm_cancel(gl);
		spin_lock(&gl->gl_spin);
	}
	return;

trap_recursive:
	print_symbol(KERN_ERR "original: %s\n", gh2->gh_ip);
	printk(KERN_ERR "pid: %d\n", pid_nr(gh2->gh_owner_pid));
	printk(KERN_ERR "lock type: %d req lock state : %d\n",
	       gh2->gh_gl->gl_name.ln_type, gh2->gh_state);
	print_symbol(KERN_ERR "new: %s\n", gh->gh_ip);
	printk(KERN_ERR "pid: %d\n", pid_nr(gh->gh_owner_pid));
	printk(KERN_ERR "lock type: %d req lock state : %d\n",
	       gh->gh_gl->gl_name.ln_type, gh->gh_state);
	__dump_glock(NULL, gl);
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
	struct gfs2_sbd *sdp = gl->gl_sbd;
	int error = 0;

	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags)))
		return -EIO;

	spin_lock(&gl->gl_spin);
	add_to_queue(gh);
	if ((LM_FLAG_NOEXP & gh->gh_flags) &&
	    test_and_clear_bit(GLF_FROZEN, &gl->gl_flags))
		set_bit(GLF_REPLY_PENDING, &gl->gl_flags);
	run_queue(gl, 1);
	spin_unlock(&gl->gl_spin);

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

	spin_lock(&gl->gl_spin);
	if (gh->gh_flags & GL_NOCACHE)
		handle_callback(gl, LM_ST_UNLOCKED, 0);

	list_del_init(&gh->gh_list);
	if (find_first_holder(gl) == NULL) {
		if (glops->go_unlock) {
			GLOCK_BUG_ON(gl, test_and_set_bit(GLF_LOCK, &gl->gl_flags));
			spin_unlock(&gl->gl_spin);
			glops->go_unlock(gh);
			spin_lock(&gl->gl_spin);
			clear_bit(GLF_LOCK, &gl->gl_flags);
		}
		if (list_empty(&gl->gl_holders) &&
		    !test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
		    !test_bit(GLF_DEMOTE, &gl->gl_flags))
			fast_path = 1;
	}
	trace_gfs2_glock_queue(gh, 0);
	spin_unlock(&gl->gl_spin);
	if (likely(fast_path))
		return;

	gfs2_glock_hold(gl);
	if (test_bit(GLF_PENDING_DEMOTE, &gl->gl_flags) &&
	    !test_bit(GLF_DEMOTE, &gl->gl_flags))
		delay = gl->gl_ops->go_min_hold_time;
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, delay) == 0)
		gfs2_glock_put(gl);
}

void gfs2_glock_dq_wait(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	gfs2_glock_dq(gh);
	wait_on_demote(gl);
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
 * @flags: modifier flags for the aquisition
 * @gh: the struct gfs2_holder
 *
 * Returns: errno
 */

int gfs2_glock_nq_num(struct gfs2_sbd *sdp, u64 number,
		      const struct gfs2_glock_operations *glops,
		      unsigned int state, int flags, struct gfs2_holder *gh)
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
	unsigned int x;

	for (x = 0; x < num_gh; x++)
		gfs2_glock_dq(&ghs[x]);
}

/**
 * gfs2_glock_dq_uninit_m - release multiple glocks
 * @num_gh: the number of structures
 * @ghs: an array of struct gfs2_holder structures
 *
 */

void gfs2_glock_dq_uninit_m(unsigned int num_gh, struct gfs2_holder *ghs)
{
	unsigned int x;

	for (x = 0; x < num_gh; x++)
		gfs2_glock_dq_uninit(&ghs[x]);
}

void gfs2_glock_cb(struct gfs2_glock *gl, unsigned int state)
{
	unsigned long delay = 0;
	unsigned long holdtime;
	unsigned long now = jiffies;

	gfs2_glock_hold(gl);
	holdtime = gl->gl_tchange + gl->gl_ops->go_min_hold_time;
	if (time_before(now, holdtime))
		delay = holdtime - now;
	if (test_bit(GLF_REPLY_PENDING, &gl->gl_flags))
		delay = gl->gl_ops->go_min_hold_time;

	spin_lock(&gl->gl_spin);
	handle_callback(gl, state, delay);
	spin_unlock(&gl->gl_spin);
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, delay) == 0)
		gfs2_glock_put(gl);
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
 */

void gfs2_glock_complete(struct gfs2_glock *gl, int ret)
{
	struct lm_lockstruct *ls = &gl->gl_sbd->sd_lockstruct;

	gl->gl_reply = ret;

	if (unlikely(test_bit(DFL_BLOCK_LOCKS, &ls->ls_flags))) {
		spin_lock(&gl->gl_spin);
		if (gfs2_should_freeze(gl)) {
			set_bit(GLF_FROZEN, &gl->gl_flags);
			spin_unlock(&gl->gl_spin);
			return;
		}
		spin_unlock(&gl->gl_spin);
	}
	set_bit(GLF_REPLY_PENDING, &gl->gl_flags);
	gfs2_glock_hold(gl);
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
		gfs2_glock_put(gl);
}


static int gfs2_shrink_glock_memory(struct shrinker *shrink, int nr, gfp_t gfp_mask)
{
	struct gfs2_glock *gl;
	int may_demote;
	int nr_skipped = 0;
	LIST_HEAD(skipped);

	if (nr == 0)
		goto out;

	if (!(gfp_mask & __GFP_FS))
		return -1;

	spin_lock(&lru_lock);
	while(nr && !list_empty(&lru_list)) {
		gl = list_entry(lru_list.next, struct gfs2_glock, gl_lru);
		list_del_init(&gl->gl_lru);
		atomic_dec(&lru_count);

		/* Test for being demotable */
		if (!test_and_set_bit(GLF_LOCK, &gl->gl_flags)) {
			gfs2_glock_hold(gl);
			spin_unlock(&lru_lock);
			spin_lock(&gl->gl_spin);
			may_demote = demote_ok(gl);
			if (may_demote) {
				handle_callback(gl, LM_ST_UNLOCKED, 0);
				nr--;
			}
			clear_bit(GLF_LOCK, &gl->gl_flags);
			smp_mb__after_clear_bit();
			if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
				gfs2_glock_put_nolock(gl);
			spin_unlock(&gl->gl_spin);
			spin_lock(&lru_lock);
			continue;
		}
		nr_skipped++;
		list_add(&gl->gl_lru, &skipped);
	}
	list_splice(&skipped, &lru_list);
	atomic_add(nr_skipped, &lru_count);
	spin_unlock(&lru_lock);
out:
	return (atomic_read(&lru_count) / 100) * sysctl_vfs_cache_pressure;
}

static struct shrinker glock_shrinker = {
	.shrink = gfs2_shrink_glock_memory,
	.seeks = DEFAULT_SEEKS,
};

/**
 * examine_bucket - Call a function for glock in a hash bucket
 * @examiner: the function
 * @sdp: the filesystem
 * @bucket: the bucket
 *
 * Returns: 1 if the bucket has entries
 */

static int examine_bucket(glock_examiner examiner, struct gfs2_sbd *sdp,
			  unsigned int hash)
{
	struct gfs2_glock *gl, *prev = NULL;
	int has_entries = 0;
	struct hlist_head *head = &gl_hash_table[hash].hb_list;

	read_lock(gl_lock_addr(hash));
	/* Can't use hlist_for_each_entry - don't want prefetch here */
	if (hlist_empty(head))
		goto out;
	gl = list_entry(head->first, struct gfs2_glock, gl_list);
	while(1) {
		if (!sdp || gl->gl_sbd == sdp) {
			gfs2_glock_hold(gl);
			read_unlock(gl_lock_addr(hash));
			if (prev)
				gfs2_glock_put(prev);
			prev = gl;
			examiner(gl);
			has_entries = 1;
			read_lock(gl_lock_addr(hash));
		}
		if (gl->gl_list.next == NULL)
			break;
		gl = list_entry(gl->gl_list.next, struct gfs2_glock, gl_list);
	}
out:
	read_unlock(gl_lock_addr(hash));
	if (prev)
		gfs2_glock_put(prev);
	cond_resched();
	return has_entries;
}


/**
 * thaw_glock - thaw out a glock which has an unprocessed reply waiting
 * @gl: The glock to thaw
 *
 * N.B. When we freeze a glock, we leave a ref to the glock outstanding,
 * so this has to result in the ref count being dropped by one.
 */

static void thaw_glock(struct gfs2_glock *gl)
{
	if (!test_and_clear_bit(GLF_FROZEN, &gl->gl_flags))
		return;
	set_bit(GLF_REPLY_PENDING, &gl->gl_flags);
	gfs2_glock_hold(gl);
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
		gfs2_glock_put(gl);
}

/**
 * clear_glock - look at a glock and see if we can free it from glock cache
 * @gl: the glock to look at
 *
 */

static void clear_glock(struct gfs2_glock *gl)
{
	spin_lock(&lru_lock);
	if (!list_empty(&gl->gl_lru)) {
		list_del_init(&gl->gl_lru);
		atomic_dec(&lru_count);
	}
	spin_unlock(&lru_lock);

	spin_lock(&gl->gl_spin);
	if (find_first_holder(gl) == NULL && gl->gl_state != LM_ST_UNLOCKED)
		handle_callback(gl, LM_ST_UNLOCKED, 0);
	spin_unlock(&gl->gl_spin);
	gfs2_glock_hold(gl);
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
		gfs2_glock_put(gl);
}

/**
 * gfs2_glock_thaw - Thaw any frozen glocks
 * @sdp: The super block
 *
 */

void gfs2_glock_thaw(struct gfs2_sbd *sdp)
{
	unsigned x;

	for (x = 0; x < GFS2_GL_HASH_SIZE; x++)
		examine_bucket(thaw_glock, sdp, x);
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
	unsigned int x;

	for (x = 0; x < GFS2_GL_HASH_SIZE; x++)
		examine_bucket(clear_glock, sdp, x);
	flush_workqueue(glock_workqueue);
	wait_event(sdp->sd_glock_wait, atomic_read(&sdp->sd_glock_disposal) == 0);
	gfs2_dump_lockstate(sdp);
}

void gfs2_glock_finish_truncate(struct gfs2_inode *ip)
{
	struct gfs2_glock *gl = ip->i_gl;
	int ret;

	ret = gfs2_truncatei_resume(ip);
	gfs2_assert_withdraw(gl->gl_sbd, ret == 0);

	spin_lock(&gl->gl_spin);
	clear_bit(GLF_LOCK, &gl->gl_flags);
	run_queue(gl, 1);
	spin_unlock(&gl->gl_spin);
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

static const char *hflags2str(char *buf, unsigned flags, unsigned long iflags)
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
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int dump_holder(struct seq_file *seq, const struct gfs2_holder *gh)
{
	struct task_struct *gh_owner = NULL;
	char buffer[KSYM_SYMBOL_LEN];
	char flags_buf[32];

	sprint_symbol(buffer, gh->gh_ip);
	if (gh->gh_owner_pid)
		gh_owner = pid_task(gh->gh_owner_pid, PIDTYPE_PID);
	gfs2_print_dbg(seq, " H: s:%s f:%s e:%d p:%ld [%s] %s\n",
		  state2str(gh->gh_state),
		  hflags2str(flags_buf, gh->gh_flags, gh->gh_iflags),
		  gh->gh_error, 
		  gh->gh_owner_pid ? (long)pid_nr(gh->gh_owner_pid) : -1,
		  gh_owner ? gh_owner->comm : "(ended)", buffer);
	return 0;
}

static const char *gflags2str(char *buf, const unsigned long *gflags)
{
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
	*p = 0;
	return buf;
}

/**
 * __dump_glock - print information about a glock
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
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int __dump_glock(struct seq_file *seq, const struct gfs2_glock *gl)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	unsigned long long dtime;
	const struct gfs2_holder *gh;
	char gflags_buf[32];
	int error = 0;

	dtime = jiffies - gl->gl_demote_time;
	dtime *= 1000000/HZ; /* demote time in uSec */
	if (!test_bit(GLF_DEMOTE, &gl->gl_flags))
		dtime = 0;
	gfs2_print_dbg(seq, "G:  s:%s n:%u/%llx f:%s t:%s d:%s/%llu a:%d r:%d\n",
		  state2str(gl->gl_state),
		  gl->gl_name.ln_type,
		  (unsigned long long)gl->gl_name.ln_number,
		  gflags2str(gflags_buf, &gl->gl_flags),
		  state2str(gl->gl_target),
		  state2str(gl->gl_demote_state), dtime,
		  atomic_read(&gl->gl_ail_count),
		  atomic_read(&gl->gl_ref));

	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		error = dump_holder(seq, gh);
		if (error)
			goto out;
	}
	if (gl->gl_state != LM_ST_UNLOCKED && glops->go_dump)
		error = glops->go_dump(seq, gl);
out:
	return error;
}

static int dump_glock(struct seq_file *seq, struct gfs2_glock *gl)
{
	int ret;
	spin_lock(&gl->gl_spin);
	ret = __dump_glock(seq, gl);
	spin_unlock(&gl->gl_spin);
	return ret;
}

/**
 * gfs2_dump_lockstate - print out the current lockstate
 * @sdp: the filesystem
 * @ub: the buffer to copy the information into
 *
 * If @ub is NULL, dump the lockstate to the console.
 *
 */

static int gfs2_dump_lockstate(struct gfs2_sbd *sdp)
{
	struct gfs2_glock *gl;
	struct hlist_node *h;
	unsigned int x;
	int error = 0;

	for (x = 0; x < GFS2_GL_HASH_SIZE; x++) {

		read_lock(gl_lock_addr(x));

		hlist_for_each_entry(gl, h, &gl_hash_table[x].hb_list, gl_list) {
			if (gl->gl_sbd != sdp)
				continue;

			error = dump_glock(NULL, gl);
			if (error)
				break;
		}

		read_unlock(gl_lock_addr(x));

		if (error)
			break;
	}


	return error;
}


int __init gfs2_glock_init(void)
{
	unsigned i;
	for(i = 0; i < GFS2_GL_HASH_SIZE; i++) {
		INIT_HLIST_HEAD(&gl_hash_table[i].hb_list);
	}
#ifdef GL_HASH_LOCK_SZ
	for(i = 0; i < GL_HASH_LOCK_SZ; i++) {
		rwlock_init(&gl_hash_locks[i]);
	}
#endif

	glock_workqueue = create_workqueue("glock_workqueue");
	if (IS_ERR(glock_workqueue))
		return PTR_ERR(glock_workqueue);
	gfs2_delete_workqueue = create_workqueue("delete_workqueue");
	if (IS_ERR(gfs2_delete_workqueue)) {
		destroy_workqueue(glock_workqueue);
		return PTR_ERR(gfs2_delete_workqueue);
	}

	register_shrinker(&glock_shrinker);

	return 0;
}

void gfs2_glock_exit(void)
{
	unregister_shrinker(&glock_shrinker);
	destroy_workqueue(glock_workqueue);
	destroy_workqueue(gfs2_delete_workqueue);
}

static int gfs2_glock_iter_next(struct gfs2_glock_iter *gi)
{
	struct gfs2_glock *gl;

restart:
	read_lock(gl_lock_addr(gi->hash));
	gl = gi->gl;
	if (gl) {
		gi->gl = hlist_entry(gl->gl_list.next,
				     struct gfs2_glock, gl_list);
	} else {
		gi->gl = hlist_entry(gl_hash_table[gi->hash].hb_list.first,
				     struct gfs2_glock, gl_list);
	}
	if (gi->gl)
		gfs2_glock_hold(gi->gl);
	read_unlock(gl_lock_addr(gi->hash));
	if (gl)
		gfs2_glock_put(gl);
	while (gi->gl == NULL) {
		gi->hash++;
		if (gi->hash >= GFS2_GL_HASH_SIZE)
			return 1;
		read_lock(gl_lock_addr(gi->hash));
		gi->gl = hlist_entry(gl_hash_table[gi->hash].hb_list.first,
				     struct gfs2_glock, gl_list);
		if (gi->gl)
			gfs2_glock_hold(gi->gl);
		read_unlock(gl_lock_addr(gi->hash));
	}

	if (gi->sdp != gi->gl->gl_sbd)
		goto restart;

	return 0;
}

static void gfs2_glock_iter_free(struct gfs2_glock_iter *gi)
{
	if (gi->gl)
		gfs2_glock_put(gi->gl);
	gi->gl = NULL;
}

static void *gfs2_glock_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct gfs2_glock_iter *gi = seq->private;
	loff_t n = *pos;

	gi->hash = 0;

	do {
		if (gfs2_glock_iter_next(gi)) {
			gfs2_glock_iter_free(gi);
			return NULL;
		}
	} while (n--);

	return gi->gl;
}

static void *gfs2_glock_seq_next(struct seq_file *seq, void *iter_ptr,
				 loff_t *pos)
{
	struct gfs2_glock_iter *gi = seq->private;

	(*pos)++;

	if (gfs2_glock_iter_next(gi)) {
		gfs2_glock_iter_free(gi);
		return NULL;
	}

	return gi->gl;
}

static void gfs2_glock_seq_stop(struct seq_file *seq, void *iter_ptr)
{
	struct gfs2_glock_iter *gi = seq->private;
	gfs2_glock_iter_free(gi);
}

static int gfs2_glock_seq_show(struct seq_file *seq, void *iter_ptr)
{
	return dump_glock(seq, iter_ptr);
}

static const struct seq_operations gfs2_glock_seq_ops = {
	.start = gfs2_glock_seq_start,
	.next  = gfs2_glock_seq_next,
	.stop  = gfs2_glock_seq_stop,
	.show  = gfs2_glock_seq_show,
};

static int gfs2_debugfs_open(struct inode *inode, struct file *file)
{
	int ret = seq_open_private(file, &gfs2_glock_seq_ops,
				   sizeof(struct gfs2_glock_iter));
	if (ret == 0) {
		struct seq_file *seq = file->private_data;
		struct gfs2_glock_iter *gi = seq->private;
		gi->sdp = inode->i_private;
	}
	return ret;
}

static const struct file_operations gfs2_debug_fops = {
	.owner   = THIS_MODULE,
	.open    = gfs2_debugfs_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};

int gfs2_create_debugfs_file(struct gfs2_sbd *sdp)
{
	sdp->debugfs_dir = debugfs_create_dir(sdp->sd_table_name, gfs2_root);
	if (!sdp->debugfs_dir)
		return -ENOMEM;
	sdp->debugfs_dentry_glocks = debugfs_create_file("glocks",
							 S_IFREG | S_IRUGO,
							 sdp->debugfs_dir, sdp,
							 &gfs2_debug_fops);
	if (!sdp->debugfs_dentry_glocks)
		return -ENOMEM;

	return 0;
}

void gfs2_delete_debugfs_file(struct gfs2_sbd *sdp)
{
	if (sdp && sdp->debugfs_dir) {
		if (sdp->debugfs_dentry_glocks) {
			debugfs_remove(sdp->debugfs_dentry_glocks);
			sdp->debugfs_dentry_glocks = NULL;
		}
		debugfs_remove(sdp->debugfs_dir);
		sdp->debugfs_dir = NULL;
	}
}

int gfs2_register_debugfs(void)
{
	gfs2_root = debugfs_create_dir("gfs2", NULL);
	return gfs2_root ? 0 : -ENOMEM;
}

void gfs2_unregister_debugfs(void)
{
	debugfs_remove(gfs2_root);
	gfs2_root = NULL;
}
