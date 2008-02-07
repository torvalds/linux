/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/delay.h>
#include <linux/sort.h>
#include <linux/jhash.h>
#include <linux/kallsyms.h>
#include <linux/gfs2_ondisk.h>
#include <linux/list.h>
#include <linux/lm_interface.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <linux/rwsem.h>
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
#include "lm.h"
#include "lops.h"
#include "meta_io.h"
#include "quota.h"
#include "super.h"
#include "util.h"

struct gfs2_gl_hash_bucket {
        struct hlist_head hb_list;
};

struct glock_iter {
	int hash;                     /* hash bucket index         */
	struct gfs2_sbd *sdp;         /* incore superblock         */
	struct gfs2_glock *gl;        /* current glock struct      */
	struct seq_file *seq;         /* sequence file for debugfs */
	char string[512];             /* scratch space             */
};

typedef void (*glock_examiner) (struct gfs2_glock * gl);

static int gfs2_dump_lockstate(struct gfs2_sbd *sdp);
static int dump_glock(struct glock_iter *gi, struct gfs2_glock *gl);
static void gfs2_glock_xmote_th(struct gfs2_glock *gl, struct gfs2_holder *gh);
static void gfs2_glock_drop_th(struct gfs2_glock *gl);
static void run_queue(struct gfs2_glock *gl);

static DECLARE_RWSEM(gfs2_umount_flush_sem);
static struct dentry *gfs2_root;
static struct task_struct *scand_process;
static unsigned int scand_secs = 5;
static struct workqueue_struct *glock_workqueue;

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
 * relaxed_state_ok - is a requested lock compatible with the current lock mode?
 * @actual: the current state of the lock
 * @requested: the lock state that was requested by the caller
 * @flags: the modifier flags passed in by the caller
 *
 * Returns: 1 if the locks are compatible, 0 otherwise
 */

static inline int relaxed_state_ok(unsigned int actual, unsigned requested,
				   int flags)
{
	if (actual == requested)
		return 1;

	if (flags & GL_EXACT)
		return 0;

	if (actual == LM_ST_EXCLUSIVE && requested == LM_ST_SHARED)
		return 1;

	if (actual != LM_ST_UNLOCKED && (flags & LM_FLAG_ANY))
		return 1;

	return 0;
}

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
	struct inode *aspace = gl->gl_aspace;

	gfs2_lm_put_lock(sdp, gl->gl_lock);

	if (aspace)
		gfs2_aspace_put(aspace);

	kmem_cache_free(gfs2_glock_cachep, gl);
}

/**
 * gfs2_glock_hold() - increment reference count on glock
 * @gl: The glock to hold
 *
 */

void gfs2_glock_hold(struct gfs2_glock *gl)
{
	atomic_inc(&gl->gl_ref);
}

/**
 * gfs2_glock_put() - Decrement reference count on glock
 * @gl: The glock to put
 *
 */

int gfs2_glock_put(struct gfs2_glock *gl)
{
	int rv = 0;
	struct gfs2_sbd *sdp = gl->gl_sbd;

	write_lock(gl_lock_addr(gl->gl_hash));
	if (atomic_dec_and_test(&gl->gl_ref)) {
		hlist_del(&gl->gl_list);
		write_unlock(gl_lock_addr(gl->gl_hash));
		gfs2_assert(sdp, gl->gl_state == LM_ST_UNLOCKED);
		gfs2_assert(sdp, list_empty(&gl->gl_reclaim));
		gfs2_assert(sdp, list_empty(&gl->gl_holders));
		gfs2_assert(sdp, list_empty(&gl->gl_waiters1));
		gfs2_assert(sdp, list_empty(&gl->gl_waiters3));
		glock_free(gl);
		rv = 1;
		goto out;
	}
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
 * gfs2_glock_find() - Find glock by lock number
 * @sdp: The GFS2 superblock
 * @name: The lock name
 *
 * Returns: NULL, or the struct gfs2_glock with the requested number
 */

static struct gfs2_glock *gfs2_glock_find(const struct gfs2_sbd *sdp,
					  const struct lm_lockname *name)
{
	unsigned int hash = gl_hash(sdp, name);
	struct gfs2_glock *gl;

	read_lock(gl_lock_addr(hash));
	gl = search_bucket(hash, sdp, name);
	read_unlock(gl_lock_addr(hash));

	return gl;
}

static void glock_work_func(struct work_struct *work)
{
	struct gfs2_glock *gl = container_of(work, struct gfs2_glock, gl_work.work);

	spin_lock(&gl->gl_spin);
	if (test_and_clear_bit(GLF_PENDING_DEMOTE, &gl->gl_flags))
		set_bit(GLF_DEMOTE, &gl->gl_flags);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);
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
	struct lm_lockname name = { .ln_number = number, .ln_type = glops->go_type };
	struct gfs2_glock *gl, *tmp;
	unsigned int hash = gl_hash(sdp, &name);
	int error;

	read_lock(gl_lock_addr(hash));
	gl = search_bucket(hash, sdp, &name);
	read_unlock(gl_lock_addr(hash));

	if (gl || !create) {
		*glp = gl;
		return 0;
	}

	gl = kmem_cache_alloc(gfs2_glock_cachep, GFP_KERNEL);
	if (!gl)
		return -ENOMEM;

	gl->gl_flags = 0;
	gl->gl_name = name;
	atomic_set(&gl->gl_ref, 1);
	gl->gl_state = LM_ST_UNLOCKED;
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
	gl->gl_hash = hash;
	gl->gl_owner_pid = NULL;
	gl->gl_ip = 0;
	gl->gl_ops = glops;
	gl->gl_req_gh = NULL;
	gl->gl_req_bh = NULL;
	gl->gl_vn = 0;
	gl->gl_stamp = jiffies;
	gl->gl_tchange = jiffies;
	gl->gl_object = NULL;
	gl->gl_sbd = sdp;
	gl->gl_aspace = NULL;
	INIT_DELAYED_WORK(&gl->gl_work, glock_work_func);

	/* If this glock protects actual on-disk data or metadata blocks,
	   create a VFS inode to manage the pages/buffers holding them. */
	if (glops == &gfs2_inode_glops || glops == &gfs2_rgrp_glops) {
		gl->gl_aspace = gfs2_aspace_get(sdp);
		if (!gl->gl_aspace) {
			error = -ENOMEM;
			goto fail;
		}
	}

	error = gfs2_lm_get_lock(sdp, &name, &gl->gl_lock);
	if (error)
		goto fail_aspace;

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

fail_aspace:
	if (gl->gl_aspace)
		gfs2_aspace_put(gl->gl_aspace);
fail:
	kmem_cache_free(gfs2_glock_cachep, gl);
	return error;
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

static void gfs2_holder_wake(struct gfs2_holder *gh)
{
	clear_bit(HIF_WAIT, &gh->gh_iflags);
	smp_mb__after_clear_bit();
	wake_up_bit(&gh->gh_iflags, HIF_WAIT);
}

static int just_schedule(void *word)
{
        schedule();
        return 0;
}

static void wait_on_holder(struct gfs2_holder *gh)
{
	might_sleep();
	wait_on_bit(&gh->gh_iflags, HIF_WAIT, just_schedule, TASK_UNINTERRUPTIBLE);
}

static void gfs2_demote_wake(struct gfs2_glock *gl)
{
	gl->gl_demote_state = LM_ST_EXCLUSIVE;
        clear_bit(GLF_DEMOTE, &gl->gl_flags);
        smp_mb__after_clear_bit();
        wake_up_bit(&gl->gl_flags, GLF_DEMOTE);
}

static void wait_on_demote(struct gfs2_glock *gl)
{
	might_sleep();
	wait_on_bit(&gl->gl_flags, GLF_DEMOTE, just_schedule, TASK_UNINTERRUPTIBLE);
}

/**
 * rq_mutex - process a mutex request in the queue
 * @gh: the glock holder
 *
 * Returns: 1 if the queue is blocked
 */

static int rq_mutex(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;

	list_del_init(&gh->gh_list);
	/*  gh->gh_error never examined.  */
	set_bit(GLF_LOCK, &gl->gl_flags);
	clear_bit(HIF_WAIT, &gh->gh_iflags);
	smp_mb();
	wake_up_bit(&gh->gh_iflags, HIF_WAIT);

	return 1;
}

/**
 * rq_promote - process a promote request in the queue
 * @gh: the glock holder
 *
 * Acquire a new inter-node lock, or change a lock state to more restrictive.
 *
 * Returns: 1 if the queue is blocked
 */

static int rq_promote(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;

	if (!relaxed_state_ok(gl->gl_state, gh->gh_state, gh->gh_flags)) {
		if (list_empty(&gl->gl_holders)) {
			gl->gl_req_gh = gh;
			set_bit(GLF_LOCK, &gl->gl_flags);
			spin_unlock(&gl->gl_spin);
			gfs2_glock_xmote_th(gh->gh_gl, gh);
			spin_lock(&gl->gl_spin);
		}
		return 1;
	}

	if (list_empty(&gl->gl_holders)) {
		set_bit(HIF_FIRST, &gh->gh_iflags);
		set_bit(GLF_LOCK, &gl->gl_flags);
	} else {
		struct gfs2_holder *next_gh;
		if (gh->gh_state == LM_ST_EXCLUSIVE)
			return 1;
		next_gh = list_entry(gl->gl_holders.next, struct gfs2_holder,
				     gh_list);
		if (next_gh->gh_state == LM_ST_EXCLUSIVE)
			 return 1;
	}

	list_move_tail(&gh->gh_list, &gl->gl_holders);
	gh->gh_error = 0;
	set_bit(HIF_HOLDER, &gh->gh_iflags);

	gfs2_holder_wake(gh);

	return 0;
}

/**
 * rq_demote - process a demote request in the queue
 * @gh: the glock holder
 *
 * Returns: 1 if the queue is blocked
 */

static int rq_demote(struct gfs2_glock *gl)
{
	if (!list_empty(&gl->gl_holders))
		return 1;

	if (gl->gl_state == gl->gl_demote_state ||
	    gl->gl_state == LM_ST_UNLOCKED) {
		gfs2_demote_wake(gl);
		return 0;
	}

	set_bit(GLF_LOCK, &gl->gl_flags);
	set_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags);

	if (gl->gl_demote_state == LM_ST_UNLOCKED ||
	    gl->gl_state != LM_ST_EXCLUSIVE) {
		spin_unlock(&gl->gl_spin);
		gfs2_glock_drop_th(gl);
	} else {
		spin_unlock(&gl->gl_spin);
		gfs2_glock_xmote_th(gl, NULL);
	}

	spin_lock(&gl->gl_spin);
	clear_bit(GLF_DEMOTE_IN_PROGRESS, &gl->gl_flags);

	return 0;
}

/**
 * run_queue - process holder structures on a glock
 * @gl: the glock
 *
 */
static void run_queue(struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;
	int blocked = 1;

	for (;;) {
		if (test_bit(GLF_LOCK, &gl->gl_flags))
			break;

		if (!list_empty(&gl->gl_waiters1)) {
			gh = list_entry(gl->gl_waiters1.next,
					struct gfs2_holder, gh_list);
			blocked = rq_mutex(gh);
		} else if (test_bit(GLF_DEMOTE, &gl->gl_flags)) {
			blocked = rq_demote(gl);
			if (gl->gl_waiters2 && !blocked) {
				set_bit(GLF_DEMOTE, &gl->gl_flags);
				gl->gl_demote_state = LM_ST_UNLOCKED;
			}
			gl->gl_waiters2 = 0;
		} else if (!list_empty(&gl->gl_waiters3)) {
			gh = list_entry(gl->gl_waiters3.next,
					struct gfs2_holder, gh_list);
			blocked = rq_promote(gh);
		} else
			break;

		if (blocked)
			break;
	}
}

/**
 * gfs2_glmutex_lock - acquire a local lock on a glock
 * @gl: the glock
 *
 * Gives caller exclusive access to manipulate a glock structure.
 */

static void gfs2_glmutex_lock(struct gfs2_glock *gl)
{
	spin_lock(&gl->gl_spin);
	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags)) {
		struct gfs2_holder gh;

		gfs2_holder_init(gl, 0, 0, &gh);
		set_bit(HIF_WAIT, &gh.gh_iflags);
		list_add_tail(&gh.gh_list, &gl->gl_waiters1);
		spin_unlock(&gl->gl_spin);
		wait_on_holder(&gh);
		gfs2_holder_uninit(&gh);
	} else {
		gl->gl_owner_pid = get_pid(task_pid(current));
		gl->gl_ip = (unsigned long)__builtin_return_address(0);
		spin_unlock(&gl->gl_spin);
	}
}

/**
 * gfs2_glmutex_trylock - try to acquire a local lock on a glock
 * @gl: the glock
 *
 * Returns: 1 if the glock is acquired
 */

static int gfs2_glmutex_trylock(struct gfs2_glock *gl)
{
	int acquired = 1;

	spin_lock(&gl->gl_spin);
	if (test_and_set_bit(GLF_LOCK, &gl->gl_flags)) {
		acquired = 0;
	} else {
		gl->gl_owner_pid = get_pid(task_pid(current));
		gl->gl_ip = (unsigned long)__builtin_return_address(0);
	}
	spin_unlock(&gl->gl_spin);

	return acquired;
}

/**
 * gfs2_glmutex_unlock - release a local lock on a glock
 * @gl: the glock
 *
 */

static void gfs2_glmutex_unlock(struct gfs2_glock *gl)
{
	struct pid *pid;

	spin_lock(&gl->gl_spin);
	clear_bit(GLF_LOCK, &gl->gl_flags);
	pid = gl->gl_owner_pid;
	gl->gl_owner_pid = NULL;
	gl->gl_ip = 0;
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	put_pid(pid);
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
			    int remote, unsigned long delay)
{
	int bit = delay ? GLF_PENDING_DEMOTE : GLF_DEMOTE;

	spin_lock(&gl->gl_spin);
	set_bit(bit, &gl->gl_flags);
	if (gl->gl_demote_state == LM_ST_EXCLUSIVE) {
		gl->gl_demote_state = state;
		gl->gl_demote_time = jiffies;
		if (remote && gl->gl_ops->go_type == LM_TYPE_IOPEN &&
		    gl->gl_object) {
			gfs2_glock_schedule_for_reclaim(gl);
			spin_unlock(&gl->gl_spin);
			return;
		}
	} else if (gl->gl_demote_state != LM_ST_UNLOCKED &&
			gl->gl_demote_state != state) {
		if (test_bit(GLF_DEMOTE_IN_PROGRESS,  &gl->gl_flags)) 
			gl->gl_waiters2 = 1;
		else 
			gl->gl_demote_state = LM_ST_UNLOCKED;
	}
	spin_unlock(&gl->gl_spin);
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
			gfs2_glock_put(gl);
	}

	gl->gl_state = new_state;
	gl->gl_tchange = jiffies;
}

/**
 * xmote_bh - Called after the lock module is done acquiring a lock
 * @gl: The glock in question
 * @ret: the int returned from the lock module
 *
 */

static void xmote_bh(struct gfs2_glock *gl, unsigned int ret)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_holder *gh = gl->gl_req_gh;
	int prev_state = gl->gl_state;
	int op_done = 1;

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, list_empty(&gl->gl_holders));
	gfs2_assert_warn(sdp, !(ret & LM_OUT_ASYNC));

	state_change(gl, ret & LM_OUT_ST_MASK);

	if (prev_state != LM_ST_UNLOCKED && !(ret & LM_OUT_CACHEABLE)) {
		if (glops->go_inval)
			glops->go_inval(gl, DIO_METADATA);
	} else if (gl->gl_state == LM_ST_DEFERRED) {
		/* We might not want to do this here.
		   Look at moving to the inode glops. */
		if (glops->go_inval)
			glops->go_inval(gl, 0);
	}

	/*  Deal with each possible exit condition  */

	if (!gh) {
		gl->gl_stamp = jiffies;
		if (ret & LM_OUT_CANCELED) {
			op_done = 0;
		} else {
			spin_lock(&gl->gl_spin);
			if (gl->gl_state != gl->gl_demote_state) {
				gl->gl_req_bh = NULL;
				spin_unlock(&gl->gl_spin);
				gfs2_glock_drop_th(gl);
				gfs2_glock_put(gl);
				return;
			}
			gfs2_demote_wake(gl);
			spin_unlock(&gl->gl_spin);
		}
	} else {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		gh->gh_error = -EIO;
		if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) 
			goto out;
		gh->gh_error = GLR_CANCELED;
		if (ret & LM_OUT_CANCELED) 
			goto out;
		if (relaxed_state_ok(gl->gl_state, gh->gh_state, gh->gh_flags)) {
			list_add_tail(&gh->gh_list, &gl->gl_holders);
			gh->gh_error = 0;
			set_bit(HIF_HOLDER, &gh->gh_iflags);
			set_bit(HIF_FIRST, &gh->gh_iflags);
			op_done = 0;
			goto out;
		}
		gh->gh_error = GLR_TRYFAILED;
		if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB))
			goto out;
		gh->gh_error = -EINVAL;
		if (gfs2_assert_withdraw(sdp, 0) == -1)
			fs_err(sdp, "ret = 0x%.8X\n", ret);
out:
		spin_unlock(&gl->gl_spin);
	}

	if (glops->go_xmote_bh)
		glops->go_xmote_bh(gl);

	if (op_done) {
		spin_lock(&gl->gl_spin);
		gl->gl_req_gh = NULL;
		gl->gl_req_bh = NULL;
		clear_bit(GLF_LOCK, &gl->gl_flags);
		spin_unlock(&gl->gl_spin);
	}

	gfs2_glock_put(gl);

	if (gh)
		gfs2_holder_wake(gh);
}

/**
 * gfs2_glock_xmote_th - Call into the lock module to acquire or change a glock
 * @gl: The glock in question
 * @state: the requested state
 * @flags: modifier flags to the lock call
 *
 */

static void gfs2_glock_xmote_th(struct gfs2_glock *gl, struct gfs2_holder *gh)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	int flags = gh ? gh->gh_flags : 0;
	unsigned state = gh ? gh->gh_state : gl->gl_demote_state;
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	int lck_flags = flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB |
				 LM_FLAG_NOEXP | LM_FLAG_ANY |
				 LM_FLAG_PRIORITY);
	unsigned int lck_ret;

	if (glops->go_xmote_th)
		glops->go_xmote_th(gl);

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, list_empty(&gl->gl_holders));
	gfs2_assert_warn(sdp, state != LM_ST_UNLOCKED);
	gfs2_assert_warn(sdp, state != gl->gl_state);

	gfs2_glock_hold(gl);
	gl->gl_req_bh = xmote_bh;

	lck_ret = gfs2_lm_lock(sdp, gl->gl_lock, gl->gl_state, state, lck_flags);

	if (gfs2_assert_withdraw(sdp, !(lck_ret & LM_OUT_ERROR)))
		return;

	if (lck_ret & LM_OUT_ASYNC)
		gfs2_assert_warn(sdp, lck_ret == LM_OUT_ASYNC);
	else
		xmote_bh(gl, lck_ret);
}

/**
 * drop_bh - Called after a lock module unlock completes
 * @gl: the glock
 * @ret: the return status
 *
 * Doesn't wake up the process waiting on the struct gfs2_holder (if any)
 * Doesn't drop the reference on the glock the top half took out
 *
 */

static void drop_bh(struct gfs2_glock *gl, unsigned int ret)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	struct gfs2_holder *gh = gl->gl_req_gh;

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, list_empty(&gl->gl_holders));
	gfs2_assert_warn(sdp, !ret);

	state_change(gl, LM_ST_UNLOCKED);

	if (glops->go_inval)
		glops->go_inval(gl, DIO_METADATA);

	if (gh) {
		spin_lock(&gl->gl_spin);
		list_del_init(&gh->gh_list);
		gh->gh_error = 0;
		spin_unlock(&gl->gl_spin);
	}

	spin_lock(&gl->gl_spin);
	gfs2_demote_wake(gl);
	gl->gl_req_gh = NULL;
	gl->gl_req_bh = NULL;
	clear_bit(GLF_LOCK, &gl->gl_flags);
	spin_unlock(&gl->gl_spin);

	gfs2_glock_put(gl);

	if (gh)
		gfs2_holder_wake(gh);
}

/**
 * gfs2_glock_drop_th - call into the lock module to unlock a lock
 * @gl: the glock
 *
 */

static void gfs2_glock_drop_th(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	unsigned int ret;

	if (glops->go_xmote_th)
		glops->go_xmote_th(gl);

	gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));
	gfs2_assert_warn(sdp, list_empty(&gl->gl_holders));
	gfs2_assert_warn(sdp, gl->gl_state != LM_ST_UNLOCKED);

	gfs2_glock_hold(gl);
	gl->gl_req_bh = drop_bh;

	ret = gfs2_lm_unlock(sdp, gl->gl_lock, gl->gl_state);

	if (gfs2_assert_withdraw(sdp, !(ret & LM_OUT_ERROR)))
		return;

	if (!ret)
		drop_bh(gl, ret);
	else
		gfs2_assert_warn(sdp, ret == LM_OUT_ASYNC);
}

/**
 * do_cancels - cancel requests for locks stuck waiting on an expire flag
 * @gh: the LM_FLAG_PRIORITY holder waiting to acquire the lock
 *
 * Don't cancel GL_NOCANCEL requests.
 */

static void do_cancels(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;

	spin_lock(&gl->gl_spin);

	while (gl->gl_req_gh != gh &&
	       !test_bit(HIF_HOLDER, &gh->gh_iflags) &&
	       !list_empty(&gh->gh_list)) {
		if (gl->gl_req_bh && !(gl->gl_req_gh &&
				     (gl->gl_req_gh->gh_flags & GL_NOCANCEL))) {
			spin_unlock(&gl->gl_spin);
			gfs2_lm_cancel(gl->gl_sbd, gl->gl_lock);
			msleep(100);
			spin_lock(&gl->gl_spin);
		} else {
			spin_unlock(&gl->gl_spin);
			msleep(100);
			spin_lock(&gl->gl_spin);
		}
	}

	spin_unlock(&gl->gl_spin);
}

/**
 * glock_wait_internal - wait on a glock acquisition
 * @gh: the glock holder
 *
 * Returns: 0 on success
 */

static int glock_wait_internal(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_sbd *sdp = gl->gl_sbd;
	const struct gfs2_glock_operations *glops = gl->gl_ops;

	if (test_bit(HIF_ABORTED, &gh->gh_iflags))
		return -EIO;

	if (gh->gh_flags & (LM_FLAG_TRY | LM_FLAG_TRY_1CB)) {
		spin_lock(&gl->gl_spin);
		if (gl->gl_req_gh != gh &&
		    !test_bit(HIF_HOLDER, &gh->gh_iflags) &&
		    !list_empty(&gh->gh_list)) {
			list_del_init(&gh->gh_list);
			gh->gh_error = GLR_TRYFAILED;
			run_queue(gl);
			spin_unlock(&gl->gl_spin);
			return gh->gh_error;
		}
		spin_unlock(&gl->gl_spin);
	}

	if (gh->gh_flags & LM_FLAG_PRIORITY)
		do_cancels(gh);

	wait_on_holder(gh);
	if (gh->gh_error)
		return gh->gh_error;

	gfs2_assert_withdraw(sdp, test_bit(HIF_HOLDER, &gh->gh_iflags));
	gfs2_assert_withdraw(sdp, relaxed_state_ok(gl->gl_state, gh->gh_state,
						   gh->gh_flags));

	if (test_bit(HIF_FIRST, &gh->gh_iflags)) {
		gfs2_assert_warn(sdp, test_bit(GLF_LOCK, &gl->gl_flags));

		if (glops->go_lock) {
			gh->gh_error = glops->go_lock(gh);
			if (gh->gh_error) {
				spin_lock(&gl->gl_spin);
				list_del_init(&gh->gh_list);
				spin_unlock(&gl->gl_spin);
			}
		}

		spin_lock(&gl->gl_spin);
		gl->gl_req_gh = NULL;
		gl->gl_req_bh = NULL;
		clear_bit(GLF_LOCK, &gl->gl_flags);
		run_queue(gl);
		spin_unlock(&gl->gl_spin);
	}

	return gh->gh_error;
}

static inline struct gfs2_holder *
find_holder_by_owner(struct list_head *head, struct pid *pid)
{
	struct gfs2_holder *gh;

	list_for_each_entry(gh, head, gh_list) {
		if (gh->gh_owner_pid == pid)
			return gh;
	}

	return NULL;
}

static void print_dbg(struct glock_iter *gi, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	if (gi) {
		vsprintf(gi->string, fmt, args);
		seq_printf(gi->seq, gi->string);
	}
	else
		vprintk(fmt, args);
	va_end(args);
}

/**
 * add_to_queue - Add a holder to the wait queue (but look for recursion)
 * @gh: the holder structure to add
 *
 */

static void add_to_queue(struct gfs2_holder *gh)
{
	struct gfs2_glock *gl = gh->gh_gl;
	struct gfs2_holder *existing;

	BUG_ON(gh->gh_owner_pid == NULL);
	if (test_and_set_bit(HIF_WAIT, &gh->gh_iflags))
		BUG();

	if (!(gh->gh_flags & GL_FLOCK)) {
		existing = find_holder_by_owner(&gl->gl_holders, 
						gh->gh_owner_pid);
		if (existing) {
			print_symbol(KERN_WARNING "original: %s\n", 
				     existing->gh_ip);
			printk(KERN_INFO "pid : %d\n",
					pid_nr(existing->gh_owner_pid));
			printk(KERN_INFO "lock type : %d lock state : %d\n",
			       existing->gh_gl->gl_name.ln_type, 
			       existing->gh_gl->gl_state);
			print_symbol(KERN_WARNING "new: %s\n", gh->gh_ip);
			printk(KERN_INFO "pid : %d\n",
					pid_nr(gh->gh_owner_pid));
			printk(KERN_INFO "lock type : %d lock state : %d\n",
			       gl->gl_name.ln_type, gl->gl_state);
			BUG();
		}
		
		existing = find_holder_by_owner(&gl->gl_waiters3, 
						gh->gh_owner_pid);
		if (existing) {
			print_symbol(KERN_WARNING "original: %s\n", 
				     existing->gh_ip);
			print_symbol(KERN_WARNING "new: %s\n", gh->gh_ip);
			BUG();
		}
	}

	if (gh->gh_flags & LM_FLAG_PRIORITY)
		list_add(&gh->gh_list, &gl->gl_waiters3);
	else
		list_add_tail(&gh->gh_list, &gl->gl_waiters3);
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

restart:
	if (unlikely(test_bit(SDF_SHUTDOWN, &sdp->sd_flags))) {
		set_bit(HIF_ABORTED, &gh->gh_iflags);
		return -EIO;
	}

	spin_lock(&gl->gl_spin);
	add_to_queue(gh);
	run_queue(gl);
	spin_unlock(&gl->gl_spin);

	if (!(gh->gh_flags & GL_ASYNC)) {
		error = glock_wait_internal(gh);
		if (error == GLR_CANCELED) {
			msleep(100);
			goto restart;
		}
	}

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
	struct gfs2_glock *gl = gh->gh_gl;
	int ready = 0;

	spin_lock(&gl->gl_spin);

	if (test_bit(HIF_HOLDER, &gh->gh_iflags))
		ready = 1;
	else if (list_empty(&gh->gh_list)) {
		if (gh->gh_error == GLR_CANCELED) {
			spin_unlock(&gl->gl_spin);
			msleep(100);
			if (gfs2_glock_nq(gh))
				return 1;
			return 0;
		} else
			ready = 1;
	}

	spin_unlock(&gl->gl_spin);

	return ready;
}

/**
 * gfs2_glock_wait - wait for a lock acquisition that ended in a GLR_ASYNC
 * @gh: the holder structure
 *
 * Returns: 0, GLR_TRYFAILED, or errno on failure
 */

int gfs2_glock_wait(struct gfs2_holder *gh)
{
	int error;

	error = glock_wait_internal(gh);
	if (error == GLR_CANCELED) {
		msleep(100);
		gh->gh_flags &= ~GL_ASYNC;
		error = gfs2_glock_nq(gh);
	}

	return error;
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

	if (gh->gh_flags & GL_NOCACHE)
		handle_callback(gl, LM_ST_UNLOCKED, 0, 0);

	gfs2_glmutex_lock(gl);

	spin_lock(&gl->gl_spin);
	list_del_init(&gh->gh_list);

	if (list_empty(&gl->gl_holders)) {
		if (glops->go_unlock) {
			spin_unlock(&gl->gl_spin);
			glops->go_unlock(gh);
			spin_lock(&gl->gl_spin);
		}
		gl->gl_stamp = jiffies;
	}

	clear_bit(GLF_LOCK, &gl->gl_flags);
	spin_unlock(&gl->gl_spin);

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

/**
 * gfs2_lvb_hold - attach a LVB from a glock
 * @gl: The glock in question
 *
 */

int gfs2_lvb_hold(struct gfs2_glock *gl)
{
	int error;

	gfs2_glmutex_lock(gl);

	if (!atomic_read(&gl->gl_lvb_count)) {
		error = gfs2_lm_hold_lvb(gl->gl_sbd, gl->gl_lock, &gl->gl_lvb);
		if (error) {
			gfs2_glmutex_unlock(gl);
			return error;
		}
		gfs2_glock_hold(gl);
	}
	atomic_inc(&gl->gl_lvb_count);

	gfs2_glmutex_unlock(gl);

	return 0;
}

/**
 * gfs2_lvb_unhold - detach a LVB from a glock
 * @gl: The glock in question
 *
 */

void gfs2_lvb_unhold(struct gfs2_glock *gl)
{
	gfs2_glock_hold(gl);
	gfs2_glmutex_lock(gl);

	gfs2_assert(gl->gl_sbd, atomic_read(&gl->gl_lvb_count) > 0);
	if (atomic_dec_and_test(&gl->gl_lvb_count)) {
		gfs2_lm_unhold_lvb(gl->gl_sbd, gl->gl_lock, gl->gl_lvb);
		gl->gl_lvb = NULL;
		gfs2_glock_put(gl);
	}

	gfs2_glmutex_unlock(gl);
	gfs2_glock_put(gl);
}

static void blocking_cb(struct gfs2_sbd *sdp, struct lm_lockname *name,
			unsigned int state)
{
	struct gfs2_glock *gl;
	unsigned long delay = 0;
	unsigned long holdtime;
	unsigned long now = jiffies;

	gl = gfs2_glock_find(sdp, name);
	if (!gl)
		return;

	holdtime = gl->gl_tchange + gl->gl_ops->go_min_hold_time;
	if (time_before(now, holdtime))
		delay = holdtime - now;

	handle_callback(gl, state, 1, delay);
	if (queue_delayed_work(glock_workqueue, &gl->gl_work, delay) == 0)
		gfs2_glock_put(gl);
}

/**
 * gfs2_glock_cb - Callback used by locking module
 * @sdp: Pointer to the superblock
 * @type: Type of callback
 * @data: Type dependent data pointer
 *
 * Called by the locking module when it wants to tell us something.
 * Either we need to drop a lock, one of our ASYNC requests completed, or
 * a journal from another client needs to be recovered.
 */

void gfs2_glock_cb(void *cb_data, unsigned int type, void *data)
{
	struct gfs2_sbd *sdp = cb_data;

	switch (type) {
	case LM_CB_NEED_E:
		blocking_cb(sdp, data, LM_ST_UNLOCKED);
		return;

	case LM_CB_NEED_D:
		blocking_cb(sdp, data, LM_ST_DEFERRED);
		return;

	case LM_CB_NEED_S:
		blocking_cb(sdp, data, LM_ST_SHARED);
		return;

	case LM_CB_ASYNC: {
		struct lm_async_cb *async = data;
		struct gfs2_glock *gl;

		down_read(&gfs2_umount_flush_sem);
		gl = gfs2_glock_find(sdp, &async->lc_name);
		if (gfs2_assert_warn(sdp, gl))
			return;
		if (!gfs2_assert_warn(sdp, gl->gl_req_bh))
			gl->gl_req_bh(gl, async->lc_ret);
		if (queue_delayed_work(glock_workqueue, &gl->gl_work, 0) == 0)
			gfs2_glock_put(gl);
		up_read(&gfs2_umount_flush_sem);
		return;
	}

	case LM_CB_NEED_RECOVERY:
		gfs2_jdesc_make_dirty(sdp, *(unsigned int *)data);
		if (sdp->sd_recoverd_process)
			wake_up_process(sdp->sd_recoverd_process);
		return;

	case LM_CB_DROPLOCKS:
		gfs2_gl_hash_clear(sdp, NO_WAIT);
		gfs2_quota_scan(sdp);
		return;

	default:
		gfs2_assert_warn(sdp, 0);
		return;
	}
}

/**
 * demote_ok - Check to see if it's ok to unlock a glock
 * @gl: the glock
 *
 * Returns: 1 if it's ok
 */

static int demote_ok(struct gfs2_glock *gl)
{
	const struct gfs2_glock_operations *glops = gl->gl_ops;
	int demote = 1;

	if (test_bit(GLF_STICKY, &gl->gl_flags))
		demote = 0;
	else if (glops->go_demote_ok)
		demote = glops->go_demote_ok(gl);

	return demote;
}

/**
 * gfs2_glock_schedule_for_reclaim - Add a glock to the reclaim list
 * @gl: the glock
 *
 */

void gfs2_glock_schedule_for_reclaim(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;

	spin_lock(&sdp->sd_reclaim_lock);
	if (list_empty(&gl->gl_reclaim)) {
		gfs2_glock_hold(gl);
		list_add(&gl->gl_reclaim, &sdp->sd_reclaim_list);
		atomic_inc(&sdp->sd_reclaim_count);
	}
	spin_unlock(&sdp->sd_reclaim_lock);

	wake_up(&sdp->sd_reclaim_wq);
}

/**
 * gfs2_reclaim_glock - process the next glock on the filesystem's reclaim list
 * @sdp: the filesystem
 *
 * Called from gfs2_glockd() glock reclaim daemon, or when promoting a
 * different glock and we notice that there are a lot of glocks in the
 * reclaim list.
 *
 */

void gfs2_reclaim_glock(struct gfs2_sbd *sdp)
{
	struct gfs2_glock *gl;

	spin_lock(&sdp->sd_reclaim_lock);
	if (list_empty(&sdp->sd_reclaim_list)) {
		spin_unlock(&sdp->sd_reclaim_lock);
		return;
	}
	gl = list_entry(sdp->sd_reclaim_list.next,
			struct gfs2_glock, gl_reclaim);
	list_del_init(&gl->gl_reclaim);
	spin_unlock(&sdp->sd_reclaim_lock);

	atomic_dec(&sdp->sd_reclaim_count);
	atomic_inc(&sdp->sd_reclaimed);

	if (gfs2_glmutex_trylock(gl)) {
		if (list_empty(&gl->gl_holders) &&
		    gl->gl_state != LM_ST_UNLOCKED && demote_ok(gl))
			handle_callback(gl, LM_ST_UNLOCKED, 0, 0);
		gfs2_glmutex_unlock(gl);
	}

	gfs2_glock_put(gl);
}

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
 * scan_glock - look at a glock and see if we can reclaim it
 * @gl: the glock to look at
 *
 */

static void scan_glock(struct gfs2_glock *gl)
{
	if (gl->gl_ops == &gfs2_inode_glops && gl->gl_object)
		return;

	if (gfs2_glmutex_trylock(gl)) {
		if (list_empty(&gl->gl_holders) &&
		    gl->gl_state != LM_ST_UNLOCKED && demote_ok(gl))
			goto out_schedule;
		gfs2_glmutex_unlock(gl);
	}
	return;

out_schedule:
	gfs2_glmutex_unlock(gl);
	gfs2_glock_schedule_for_reclaim(gl);
}

/**
 * clear_glock - look at a glock and see if we can free it from glock cache
 * @gl: the glock to look at
 *
 */

static void clear_glock(struct gfs2_glock *gl)
{
	struct gfs2_sbd *sdp = gl->gl_sbd;
	int released;

	spin_lock(&sdp->sd_reclaim_lock);
	if (!list_empty(&gl->gl_reclaim)) {
		list_del_init(&gl->gl_reclaim);
		atomic_dec(&sdp->sd_reclaim_count);
		spin_unlock(&sdp->sd_reclaim_lock);
		released = gfs2_glock_put(gl);
		gfs2_assert(sdp, !released);
	} else {
		spin_unlock(&sdp->sd_reclaim_lock);
	}

	if (gfs2_glmutex_trylock(gl)) {
		if (list_empty(&gl->gl_holders) &&
		    gl->gl_state != LM_ST_UNLOCKED)
			handle_callback(gl, LM_ST_UNLOCKED, 0, 0);
		gfs2_glmutex_unlock(gl);
	}
}

/**
 * gfs2_gl_hash_clear - Empty out the glock hash table
 * @sdp: the filesystem
 * @wait: wait until it's all gone
 *
 * Called when unmounting the filesystem, or when inter-node lock manager
 * requests DROPLOCKS because it is running out of capacity.
 */

void gfs2_gl_hash_clear(struct gfs2_sbd *sdp, int wait)
{
	unsigned long t;
	unsigned int x;
	int cont;

	t = jiffies;

	for (;;) {
		cont = 0;
		for (x = 0; x < GFS2_GL_HASH_SIZE; x++) {
			if (examine_bucket(clear_glock, sdp, x))
				cont = 1;
		}

		if (!wait || !cont)
			break;

		if (time_after_eq(jiffies,
				  t + gfs2_tune_get(sdp, gt_stall_secs) * HZ)) {
			fs_warn(sdp, "Unmount seems to be stalled. "
				     "Dumping lock state...\n");
			gfs2_dump_lockstate(sdp);
			t = jiffies;
		}

		down_write(&gfs2_umount_flush_sem);
		invalidate_inodes(sdp->sd_vfs);
		up_write(&gfs2_umount_flush_sem);
		msleep(10);
	}
}

/*
 *  Diagnostic routines to help debug distributed deadlock
 */

static void gfs2_print_symbol(struct glock_iter *gi, const char *fmt,
                              unsigned long address)
{
	char buffer[KSYM_SYMBOL_LEN];

	sprint_symbol(buffer, address);
	print_dbg(gi, fmt, buffer);
}

/**
 * dump_holder - print information about a glock holder
 * @str: a string naming the type of holder
 * @gh: the glock holder
 *
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int dump_holder(struct glock_iter *gi, char *str,
		       struct gfs2_holder *gh)
{
	unsigned int x;
	struct task_struct *gh_owner;

	print_dbg(gi, "  %s\n", str);
	if (gh->gh_owner_pid) {
		print_dbg(gi, "    owner = %ld ",
				(long)pid_nr(gh->gh_owner_pid));
		gh_owner = pid_task(gh->gh_owner_pid, PIDTYPE_PID);
		if (gh_owner)
			print_dbg(gi, "(%s)\n", gh_owner->comm);
		else
			print_dbg(gi, "(ended)\n");
	} else
		print_dbg(gi, "    owner = -1\n");
	print_dbg(gi, "    gh_state = %u\n", gh->gh_state);
	print_dbg(gi, "    gh_flags =");
	for (x = 0; x < 32; x++)
		if (gh->gh_flags & (1 << x))
			print_dbg(gi, " %u", x);
	print_dbg(gi, " \n");
	print_dbg(gi, "    error = %d\n", gh->gh_error);
	print_dbg(gi, "    gh_iflags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &gh->gh_iflags))
			print_dbg(gi, " %u", x);
	print_dbg(gi, " \n");
        gfs2_print_symbol(gi, "    initialized at: %s\n", gh->gh_ip);

	return 0;
}

/**
 * dump_inode - print information about an inode
 * @ip: the inode
 *
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int dump_inode(struct glock_iter *gi, struct gfs2_inode *ip)
{
	unsigned int x;

	print_dbg(gi, "  Inode:\n");
	print_dbg(gi, "    num = %llu/%llu\n",
		  (unsigned long long)ip->i_no_formal_ino,
		  (unsigned long long)ip->i_no_addr);
	print_dbg(gi, "    type = %u\n", IF2DT(ip->i_inode.i_mode));
	print_dbg(gi, "    i_flags =");
	for (x = 0; x < 32; x++)
		if (test_bit(x, &ip->i_flags))
			print_dbg(gi, " %u", x);
	print_dbg(gi, " \n");
	return 0;
}

/**
 * dump_glock - print information about a glock
 * @gl: the glock
 * @count: where we are in the buffer
 *
 * Returns: 0 on success, -ENOBUFS when we run out of space
 */

static int dump_glock(struct glock_iter *gi, struct gfs2_glock *gl)
{
	struct gfs2_holder *gh;
	unsigned int x;
	int error = -ENOBUFS;
	struct task_struct *gl_owner;

	spin_lock(&gl->gl_spin);

	print_dbg(gi, "Glock 0x%p (%u, 0x%llx)\n", gl, gl->gl_name.ln_type,
		   (unsigned long long)gl->gl_name.ln_number);
	print_dbg(gi, "  gl_flags =");
	for (x = 0; x < 32; x++) {
		if (test_bit(x, &gl->gl_flags))
			print_dbg(gi, " %u", x);
	}
	if (!test_bit(GLF_LOCK, &gl->gl_flags))
		print_dbg(gi, " (unlocked)");
	print_dbg(gi, " \n");
	print_dbg(gi, "  gl_ref = %d\n", atomic_read(&gl->gl_ref));
	print_dbg(gi, "  gl_state = %u\n", gl->gl_state);
	if (gl->gl_owner_pid) {
		gl_owner = pid_task(gl->gl_owner_pid, PIDTYPE_PID);
		if (gl_owner)
			print_dbg(gi, "  gl_owner = pid %d (%s)\n",
				  pid_nr(gl->gl_owner_pid), gl_owner->comm);
		else
			print_dbg(gi, "  gl_owner = %d (ended)\n",
				  pid_nr(gl->gl_owner_pid));
	} else
		print_dbg(gi, "  gl_owner = -1\n");
	print_dbg(gi, "  gl_ip = %lu\n", gl->gl_ip);
	print_dbg(gi, "  req_gh = %s\n", (gl->gl_req_gh) ? "yes" : "no");
	print_dbg(gi, "  req_bh = %s\n", (gl->gl_req_bh) ? "yes" : "no");
	print_dbg(gi, "  lvb_count = %d\n", atomic_read(&gl->gl_lvb_count));
	print_dbg(gi, "  object = %s\n", (gl->gl_object) ? "yes" : "no");
	print_dbg(gi, "  reclaim = %s\n",
		   (list_empty(&gl->gl_reclaim)) ? "no" : "yes");
	if (gl->gl_aspace)
		print_dbg(gi, "  aspace = 0x%p nrpages = %lu\n", gl->gl_aspace,
			   gl->gl_aspace->i_mapping->nrpages);
	else
		print_dbg(gi, "  aspace = no\n");
	print_dbg(gi, "  ail = %d\n", atomic_read(&gl->gl_ail_count));
	if (gl->gl_req_gh) {
		error = dump_holder(gi, "Request", gl->gl_req_gh);
		if (error)
			goto out;
	}
	list_for_each_entry(gh, &gl->gl_holders, gh_list) {
		error = dump_holder(gi, "Holder", gh);
		if (error)
			goto out;
	}
	list_for_each_entry(gh, &gl->gl_waiters1, gh_list) {
		error = dump_holder(gi, "Waiter1", gh);
		if (error)
			goto out;
	}
	list_for_each_entry(gh, &gl->gl_waiters3, gh_list) {
		error = dump_holder(gi, "Waiter3", gh);
		if (error)
			goto out;
	}
	if (test_bit(GLF_DEMOTE, &gl->gl_flags)) {
		print_dbg(gi, "  Demotion req to state %u (%llu uS ago)\n",
			  gl->gl_demote_state, (unsigned long long)
			  (jiffies - gl->gl_demote_time)*(1000000/HZ));
	}
	if (gl->gl_ops == &gfs2_inode_glops && gl->gl_object) {
		if (!test_bit(GLF_LOCK, &gl->gl_flags) &&
			list_empty(&gl->gl_holders)) {
			error = dump_inode(gi, gl->gl_object);
			if (error)
				goto out;
		} else {
			error = -ENOBUFS;
			print_dbg(gi, "  Inode: busy\n");
		}
	}

	error = 0;

out:
	spin_unlock(&gl->gl_spin);
	return error;
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

/**
 * gfs2_scand - Look for cached glocks and inodes to toss from memory
 * @sdp: Pointer to GFS2 superblock
 *
 * One of these daemons runs, finding candidates to add to sd_reclaim_list.
 * See gfs2_glockd()
 */

static int gfs2_scand(void *data)
{
	unsigned x;
	unsigned delay;

	while (!kthread_should_stop()) {
		for (x = 0; x < GFS2_GL_HASH_SIZE; x++)
			examine_bucket(scan_glock, NULL, x);
		if (freezing(current))
			refrigerator();
		delay = scand_secs;
		if (delay < 1)
			delay = 1;
		schedule_timeout_interruptible(delay * HZ);
	}

	return 0;
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

	scand_process = kthread_run(gfs2_scand, NULL, "gfs2_scand");
	if (IS_ERR(scand_process))
		return PTR_ERR(scand_process);

	glock_workqueue = create_workqueue("glock_workqueue");
	if (IS_ERR(glock_workqueue)) {
		kthread_stop(scand_process);
		return PTR_ERR(glock_workqueue);
	}

	return 0;
}

void gfs2_glock_exit(void)
{
	destroy_workqueue(glock_workqueue);
	kthread_stop(scand_process);
}

module_param(scand_secs, uint, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(scand_secs, "The number of seconds between scand runs");

static int gfs2_glock_iter_next(struct glock_iter *gi)
{
	struct gfs2_glock *gl;

restart:
	read_lock(gl_lock_addr(gi->hash));
	gl = gi->gl;
	if (gl) {
		gi->gl = hlist_entry(gl->gl_list.next,
				     struct gfs2_glock, gl_list);
		if (gi->gl)
			gfs2_glock_hold(gi->gl);
	}
	read_unlock(gl_lock_addr(gi->hash));
	if (gl)
		gfs2_glock_put(gl);
	if (gl && gi->gl == NULL)
		gi->hash++;
	while(gi->gl == NULL) {
		if (gi->hash >= GFS2_GL_HASH_SIZE)
			return 1;
		read_lock(gl_lock_addr(gi->hash));
		gi->gl = hlist_entry(gl_hash_table[gi->hash].hb_list.first,
				     struct gfs2_glock, gl_list);
		if (gi->gl)
			gfs2_glock_hold(gi->gl);
		read_unlock(gl_lock_addr(gi->hash));
		gi->hash++;
	}

	if (gi->sdp != gi->gl->gl_sbd)
		goto restart;

	return 0;
}

static void gfs2_glock_iter_free(struct glock_iter *gi)
{
	if (gi->gl)
		gfs2_glock_put(gi->gl);
	kfree(gi);
}

static struct glock_iter *gfs2_glock_iter_init(struct gfs2_sbd *sdp)
{
	struct glock_iter *gi;

	gi = kmalloc(sizeof (*gi), GFP_KERNEL);
	if (!gi)
		return NULL;

	gi->sdp = sdp;
	gi->hash = 0;
	gi->seq = NULL;
	gi->gl = NULL;
	memset(gi->string, 0, sizeof(gi->string));

	if (gfs2_glock_iter_next(gi)) {
		gfs2_glock_iter_free(gi);
		return NULL;
	}

	return gi;
}

static void *gfs2_glock_seq_start(struct seq_file *file, loff_t *pos)
{
	struct glock_iter *gi;
	loff_t n = *pos;

	gi = gfs2_glock_iter_init(file->private);
	if (!gi)
		return NULL;

	while(n--) {
		if (gfs2_glock_iter_next(gi)) {
			gfs2_glock_iter_free(gi);
			return NULL;
		}
	}

	return gi;
}

static void *gfs2_glock_seq_next(struct seq_file *file, void *iter_ptr,
				 loff_t *pos)
{
	struct glock_iter *gi = iter_ptr;

	(*pos)++;

	if (gfs2_glock_iter_next(gi)) {
		gfs2_glock_iter_free(gi);
		return NULL;
	}

	return gi;
}

static void gfs2_glock_seq_stop(struct seq_file *file, void *iter_ptr)
{
	struct glock_iter *gi = iter_ptr;
	if (gi)
		gfs2_glock_iter_free(gi);
}

static int gfs2_glock_seq_show(struct seq_file *file, void *iter_ptr)
{
	struct glock_iter *gi = iter_ptr;

	gi->seq = file;
	dump_glock(gi, gi->gl);

	return 0;
}

static const struct seq_operations gfs2_glock_seq_ops = {
	.start = gfs2_glock_seq_start,
	.next  = gfs2_glock_seq_next,
	.stop  = gfs2_glock_seq_stop,
	.show  = gfs2_glock_seq_show,
};

static int gfs2_debugfs_open(struct inode *inode, struct file *file)
{
	struct seq_file *seq;
	int ret;

	ret = seq_open(file, &gfs2_glock_seq_ops);
	if (ret)
		return ret;

	seq = file->private_data;
	seq->private = inode->i_private;

	return 0;
}

static const struct file_operations gfs2_debug_fops = {
	.owner   = THIS_MODULE,
	.open    = gfs2_debugfs_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
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
