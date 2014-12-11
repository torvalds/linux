/*
 * Percpu refcounts:
 * (C) 2012 Google, Inc.
 * Author: Kent Overstreet <koverstreet@google.com>
 *
 * This implements a refcount with similar semantics to atomic_t - atomic_inc(),
 * atomic_dec_and_test() - but percpu.
 *
 * There's one important difference between percpu refs and normal atomic_t
 * refcounts; you have to keep track of your initial refcount, and then when you
 * start shutting down you call percpu_ref_kill() _before_ dropping the initial
 * refcount.
 *
 * The refcount will have a range of 0 to ((1U << 31) - 1), i.e. one bit less
 * than an atomic_t - this is because of the way shutdown works, see
 * percpu_ref_kill()/PERCPU_COUNT_BIAS.
 *
 * Before you call percpu_ref_kill(), percpu_ref_put() does not check for the
 * refcount hitting 0 - it can't, if it was in percpu mode. percpu_ref_kill()
 * puts the ref back in single atomic_t mode, collecting the per cpu refs and
 * issuing the appropriate barriers, and then marks the ref as shutting down so
 * that percpu_ref_put() will check for the ref hitting 0.  After it returns,
 * it's safe to drop the initial ref.
 *
 * USAGE:
 *
 * See fs/aio.c for some example usage; it's used there for struct kioctx, which
 * is created when userspaces calls io_setup(), and destroyed when userspace
 * calls io_destroy() or the process exits.
 *
 * In the aio code, kill_ioctx() is called when we wish to destroy a kioctx; it
 * calls percpu_ref_kill(), then hlist_del_rcu() and synchronize_rcu() to remove
 * the kioctx from the proccess's list of kioctxs - after that, there can't be
 * any new users of the kioctx (from lookup_ioctx()) and it's then safe to drop
 * the initial ref with percpu_ref_put().
 *
 * Code that does a two stage shutdown like this often needs some kind of
 * explicit synchronization to ensure the initial refcount can only be dropped
 * once - percpu_ref_kill() does this for you, it returns true once and false if
 * someone else already called it. The aio code uses it this way, but it's not
 * necessary if the code has some other mechanism to synchronize teardown.
 * around.
 */

#ifndef _LINUX_PERCPU_REFCOUNT_H
#define _LINUX_PERCPU_REFCOUNT_H

#include <linux/atomic.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/rcupdate.h>
#include <linux/gfp.h>

struct percpu_ref;
typedef void (percpu_ref_func_t)(struct percpu_ref *);

/* flags set in the lower bits of percpu_ref->percpu_count_ptr */
enum {
	__PERCPU_REF_ATOMIC	= 1LU << 0,	/* operating in atomic mode */
	__PERCPU_REF_DEAD	= 1LU << 1,	/* (being) killed */
	__PERCPU_REF_ATOMIC_DEAD = __PERCPU_REF_ATOMIC | __PERCPU_REF_DEAD,

	__PERCPU_REF_FLAG_BITS	= 2,
};

/* @flags for percpu_ref_init() */
enum {
	/*
	 * Start w/ ref == 1 in atomic mode.  Can be switched to percpu
	 * operation using percpu_ref_switch_to_percpu().  If initialized
	 * with this flag, the ref will stay in atomic mode until
	 * percpu_ref_switch_to_percpu() is invoked on it.
	 */
	PERCPU_REF_INIT_ATOMIC	= 1 << 0,

	/*
	 * Start dead w/ ref == 0 in atomic mode.  Must be revived with
	 * percpu_ref_reinit() before used.  Implies INIT_ATOMIC.
	 */
	PERCPU_REF_INIT_DEAD	= 1 << 1,
};

struct percpu_ref {
	atomic_long_t		count;
	/*
	 * The low bit of the pointer indicates whether the ref is in percpu
	 * mode; if set, then get/put will manipulate the atomic_t.
	 */
	unsigned long		percpu_count_ptr;
	percpu_ref_func_t	*release;
	percpu_ref_func_t	*confirm_switch;
	bool			force_atomic:1;
	struct rcu_head		rcu;
};

int __must_check percpu_ref_init(struct percpu_ref *ref,
				 percpu_ref_func_t *release, unsigned int flags,
				 gfp_t gfp);
void percpu_ref_exit(struct percpu_ref *ref);
void percpu_ref_switch_to_atomic(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_switch);
void percpu_ref_switch_to_percpu(struct percpu_ref *ref);
void percpu_ref_kill_and_confirm(struct percpu_ref *ref,
				 percpu_ref_func_t *confirm_kill);
void percpu_ref_reinit(struct percpu_ref *ref);

/**
 * percpu_ref_kill - drop the initial ref
 * @ref: percpu_ref to kill
 *
 * Must be used to drop the initial ref on a percpu refcount; must be called
 * precisely once before shutdown.
 *
 * Puts @ref in non percpu mode, then does a call_rcu() before gathering up the
 * percpu counters and dropping the initial ref.
 */
static inline void percpu_ref_kill(struct percpu_ref *ref)
{
	return percpu_ref_kill_and_confirm(ref, NULL);
}

/*
 * Internal helper.  Don't use outside percpu-refcount proper.  The
 * function doesn't return the pointer and let the caller test it for NULL
 * because doing so forces the compiler to generate two conditional
 * branches as it can't assume that @ref->percpu_count is not NULL.
 */
static inline bool __ref_is_percpu(struct percpu_ref *ref,
					  unsigned long __percpu **percpu_countp)
{
	unsigned long percpu_ptr = ACCESS_ONCE(ref->percpu_count_ptr);

	/* paired with smp_store_release() in percpu_ref_reinit() */
	smp_read_barrier_depends();

	/*
	 * Theoretically, the following could test just ATOMIC; however,
	 * then we'd have to mask off DEAD separately as DEAD may be
	 * visible without ATOMIC if we race with percpu_ref_kill().  DEAD
	 * implies ATOMIC anyway.  Test them together.
	 */
	if (unlikely(percpu_ptr & __PERCPU_REF_ATOMIC_DEAD))
		return false;

	*percpu_countp = (unsigned long __percpu *)percpu_ptr;
	return true;
}

/**
 * percpu_ref_get_many - increment a percpu refcount
 * @ref: percpu_ref to get
 * @nr: number of references to get
 *
 * Analogous to atomic_long_add().
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline void percpu_ref_get_many(struct percpu_ref *ref, unsigned long nr)
{
	unsigned long __percpu *percpu_count;

	rcu_read_lock_sched();

	if (__ref_is_percpu(ref, &percpu_count))
		this_cpu_add(*percpu_count, nr);
	else
		atomic_long_add(nr, &ref->count);

	rcu_read_unlock_sched();
}

/**
 * percpu_ref_get - increment a percpu refcount
 * @ref: percpu_ref to get
 *
 * Analagous to atomic_long_inc().
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline void percpu_ref_get(struct percpu_ref *ref)
{
	percpu_ref_get_many(ref, 1);
}

/**
 * percpu_ref_tryget - try to increment a percpu refcount
 * @ref: percpu_ref to try-get
 *
 * Increment a percpu refcount unless its count already reached zero.
 * Returns %true on success; %false on failure.
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline bool percpu_ref_tryget(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count;
	int ret;

	rcu_read_lock_sched();

	if (__ref_is_percpu(ref, &percpu_count)) {
		this_cpu_inc(*percpu_count);
		ret = true;
	} else {
		ret = atomic_long_inc_not_zero(&ref->count);
	}

	rcu_read_unlock_sched();

	return ret;
}

/**
 * percpu_ref_tryget_live - try to increment a live percpu refcount
 * @ref: percpu_ref to try-get
 *
 * Increment a percpu refcount unless it has already been killed.  Returns
 * %true on success; %false on failure.
 *
 * Completion of percpu_ref_kill() in itself doesn't guarantee that this
 * function will fail.  For such guarantee, percpu_ref_kill_and_confirm()
 * should be used.  After the confirm_kill callback is invoked, it's
 * guaranteed that no new reference will be given out by
 * percpu_ref_tryget_live().
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline bool percpu_ref_tryget_live(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count;
	int ret = false;

	rcu_read_lock_sched();

	if (__ref_is_percpu(ref, &percpu_count)) {
		this_cpu_inc(*percpu_count);
		ret = true;
	} else if (!(ACCESS_ONCE(ref->percpu_count_ptr) & __PERCPU_REF_DEAD)) {
		ret = atomic_long_inc_not_zero(&ref->count);
	}

	rcu_read_unlock_sched();

	return ret;
}

/**
 * percpu_ref_put_many - decrement a percpu refcount
 * @ref: percpu_ref to put
 * @nr: number of references to put
 *
 * Decrement the refcount, and if 0, call the release function (which was passed
 * to percpu_ref_init())
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline void percpu_ref_put_many(struct percpu_ref *ref, unsigned long nr)
{
	unsigned long __percpu *percpu_count;

	rcu_read_lock_sched();

	if (__ref_is_percpu(ref, &percpu_count))
		this_cpu_sub(*percpu_count, nr);
	else if (unlikely(atomic_long_sub_and_test(nr, &ref->count)))
		ref->release(ref);

	rcu_read_unlock_sched();
}

/**
 * percpu_ref_put - decrement a percpu refcount
 * @ref: percpu_ref to put
 *
 * Decrement the refcount, and if 0, call the release function (which was passed
 * to percpu_ref_init())
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline void percpu_ref_put(struct percpu_ref *ref)
{
	percpu_ref_put_many(ref, 1);
}

/**
 * percpu_ref_is_zero - test whether a percpu refcount reached zero
 * @ref: percpu_ref to test
 *
 * Returns %true if @ref reached zero.
 *
 * This function is safe to call as long as @ref is between init and exit.
 */
static inline bool percpu_ref_is_zero(struct percpu_ref *ref)
{
	unsigned long __percpu *percpu_count;

	if (__ref_is_percpu(ref, &percpu_count))
		return false;
	return !atomic_long_read(&ref->count);
}

#endif
