// SPDX-License-Identifier: GPL-2.0-only
/*
 * kernel/locking/mutex.c
 *
 * Mutexes: blocking mutual exclusion locks
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * Many thanks to Arjan van de Ven, Thomas Gleixner, Steven Rostedt and
 * David Howells for suggestions and improvements.
 *
 *  - Adaptive spinning for mutexes by Peter Zijlstra. (Ported to mainline
 *    from the -rt tree, where it was originally implemented for rtmutexes
 *    by Steven Rostedt, based on work by Gregory Haskins, Peter Morreale
 *    and Sven Dietrich.
 *
 * Also see Documentation/locking/mutex-design.rst.
 */
#include <linux/mutex.h>
#include <linux/ww_mutex.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/debug.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/debug_locks.h>
#include <linux/osq_lock.h>

#define CREATE_TRACE_POINTS
#include <trace/events/lock.h>

#undef CREATE_TRACE_POINTS
#include <trace/hooks/dtask.h>

#ifndef CONFIG_PREEMPT_RT
#include "mutex.h"

#ifdef CONFIG_DEBUG_MUTEXES
# define MUTEX_WARN_ON(cond) DEBUG_LOCKS_WARN_ON(cond)
#else
# define MUTEX_WARN_ON(cond)
#endif

void
__mutex_init(struct mutex *lock, const char *name, struct lock_class_key *key)
{
	atomic_long_set(&lock->owner, 0);
	raw_spin_lock_init(&lock->wait_lock);
	INIT_LIST_HEAD(&lock->wait_list);
#ifdef CONFIG_MUTEX_SPIN_ON_OWNER
	osq_lock_init(&lock->osq);
#endif

	debug_mutex_init(lock, name, key);
}
EXPORT_SYMBOL(__mutex_init);

/*
 * @owner: contains: 'struct task_struct *' to the current lock owner,
 * NULL means not owned. Since task_struct pointers are aligned at
 * at least L1_CACHE_BYTES, we have low bits to store extra state.
 *
 * Bit0 indicates a non-empty waiter list; unlock must issue a wakeup.
 * Bit1 indicates unlock needs to hand the lock to the top-waiter
 * Bit2 indicates handoff has been done and we're waiting for pickup.
 */
#define MUTEX_FLAG_WAITERS	0x01
#define MUTEX_FLAG_HANDOFF	0x02
#define MUTEX_FLAG_PICKUP	0x04

#define MUTEX_FLAGS		0x07

/*
 * Internal helper function; C doesn't allow us to hide it :/
 *
 * DO NOT USE (outside of mutex code).
 */
static inline struct task_struct *__mutex_owner(struct mutex *lock)
{
	return (struct task_struct *)(atomic_long_read(&lock->owner) & ~MUTEX_FLAGS);
}

static inline struct task_struct *__owner_task(unsigned long owner)
{
	return (struct task_struct *)(owner & ~MUTEX_FLAGS);
}

bool mutex_is_locked(struct mutex *lock)
{
	return __mutex_owner(lock) != NULL;
}
EXPORT_SYMBOL(mutex_is_locked);

static inline unsigned long __owner_flags(unsigned long owner)
{
	return owner & MUTEX_FLAGS;
}

/*
 * Returns: __mutex_owner(lock) on failure or NULL on success.
 */
static inline struct task_struct *__mutex_trylock_common(struct mutex *lock, bool handoff)
{
	unsigned long owner, curr = (unsigned long)current;

	owner = atomic_long_read(&lock->owner);
	for (;;) { /* must loop, can race against a flag */
		unsigned long flags = __owner_flags(owner);
		unsigned long task = owner & ~MUTEX_FLAGS;

		if (task) {
			if (flags & MUTEX_FLAG_PICKUP) {
				if (task != curr)
					break;
				flags &= ~MUTEX_FLAG_PICKUP;
			} else if (handoff) {
				if (flags & MUTEX_FLAG_HANDOFF)
					break;
				flags |= MUTEX_FLAG_HANDOFF;
			} else {
				break;
			}
		} else {
			MUTEX_WARN_ON(flags & (MUTEX_FLAG_HANDOFF | MUTEX_FLAG_PICKUP));
			task = curr;
		}

		if (atomic_long_try_cmpxchg_acquire(&lock->owner, &owner, task | flags)) {
			if (task == curr)
				return NULL;
			break;
		}
	}

	return __owner_task(owner);
}

/*
 * Trylock or set HANDOFF
 */
static inline bool __mutex_trylock_or_handoff(struct mutex *lock, bool handoff)
{
	return !__mutex_trylock_common(lock, handoff);
}

/*
 * Actual trylock that will work on any unlocked state.
 */
static inline bool __mutex_trylock(struct mutex *lock)
{
	return !__mutex_trylock_common(lock, false);
}

#ifndef CONFIG_DEBUG_LOCK_ALLOC
/*
 * Lockdep annotations are contained to the slow paths for simplicity.
 * There is nothing that would stop spreading the lockdep annotations outwards
 * except more code.
 */

/*
 * Optimistic trylock that only works in the uncontended case. Make sure to
 * follow with a __mutex_trylock() before failing.
 */
static __always_inline bool __mutex_trylock_fast(struct mutex *lock)
{
	unsigned long curr = (unsigned long)current;
	unsigned long zero = 0UL;

	if (atomic_long_try_cmpxchg_acquire(&lock->owner, &zero, curr))
		return true;

	return false;
}

static __always_inline bool __mutex_unlock_fast(struct mutex *lock)
{
	unsigned long curr = (unsigned long)current;

	return atomic_long_try_cmpxchg_release(&lock->owner, &curr, 0UL);
}
#endif

static inline void __mutex_set_flag(struct mutex *lock, unsigned long flag)
{
	atomic_long_or(flag, &lock->owner);
}

static inline void __mutex_clear_flag(struct mutex *lock, unsigned long flag)
{
	atomic_long_andnot(flag, &lock->owner);
}

static inline bool __mutex_waiter_is_first(struct mutex *lock, struct mutex_waiter *waiter)
{
	return list_first_entry(&lock->wait_list, struct mutex_waiter, list) == waiter;
}

/*
 * Add @waiter to a given location in the lock wait_list and set the
 * FLAG_WAITERS flag if it's the first waiter.
 */
static void
__mutex_add_waiter(struct mutex *lock, struct mutex_waiter *waiter,
		   struct list_head *list)
{
	debug_mutex_add_waiter(lock, waiter, current);

	list_add_tail(&waiter->list, list);
	if (__mutex_waiter_is_first(lock, waiter))
		__mutex_set_flag(lock, MUTEX_FLAG_WAITERS);
}

static void
__mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter)
{
	list_del(&waiter->list);
	if (likely(list_empty(&lock->wait_list)))
		__mutex_clear_flag(lock, MUTEX_FLAGS);

	debug_mutex_remove_waiter(lock, waiter, current);
}

/*
 * Give up ownership to a specific task, when @task = NULL, this is equivalent
 * to a regular unlock. Sets PICKUP on a handoff, clears HANDOFF, preserves
 * WAITERS. Provides RELEASE semantics like a regular unlock, the
 * __mutex_trylock() provides a matching ACQUIRE semantics for the handoff.
 */
static void __mutex_handoff(struct mutex *lock, struct task_struct *task)
{
	unsigned long owner = atomic_long_read(&lock->owner);

	for (;;) {
		unsigned long new;

		MUTEX_WARN_ON(__owner_task(owner) != current);
		MUTEX_WARN_ON(owner & MUTEX_FLAG_PICKUP);

		new = (owner & MUTEX_FLAG_WAITERS);
		new |= (unsigned long)task;
		if (task)
			new |= MUTEX_FLAG_PICKUP;

		if (atomic_long_try_cmpxchg_release(&lock->owner, &owner, new))
			break;
	}
}

#ifndef CONFIG_DEBUG_LOCK_ALLOC
/*
 * We split the mutex lock/unlock logic into separate fastpath and
 * slowpath functions, to reduce the register pressure on the fastpath.
 * We also put the fastpath first in the kernel image, to make sure the
 * branch is predicted by the CPU as default-untaken.
 */
static void __sched __mutex_lock_slowpath(struct mutex *lock);

/**
 * mutex_lock - acquire the mutex
 * @lock: the mutex to be acquired
 *
 * Lock the mutex exclusively for this task. If the mutex is not
 * available right now, it will sleep until it can get it.
 *
 * The mutex must later on be released by the same task that
 * acquired it. Recursive locking is not allowed. The task
 * may not exit without first unlocking the mutex. Also, kernel
 * memory where the mutex resides must not be freed with
 * the mutex still locked. The mutex must first be initialized
 * (or statically defined) before it can be locked. memset()-ing
 * the mutex to 0 is not allowed.
 *
 * (The CONFIG_DEBUG_MUTEXES .config option turns on debugging
 * checks that will enforce the restrictions and will also do
 * deadlock debugging)
 *
 * This function is similar to (but not equivalent to) down().
 */
void __sched mutex_lock(struct mutex *lock)
{
	might_sleep();

	if (!__mutex_trylock_fast(lock))
		__mutex_lock_slowpath(lock);
}
EXPORT_SYMBOL(mutex_lock);
#endif

#include "ww_mutex.h"

#ifdef CONFIG_MUTEX_SPIN_ON_OWNER

/*
 * Trylock variant that returns the owning task on failure.
 */
static inline struct task_struct *__mutex_trylock_or_owner(struct mutex *lock)
{
	return __mutex_trylock_common(lock, false);
}

static inline
bool ww_mutex_spin_on_owner(struct mutex *lock, struct ww_acquire_ctx *ww_ctx,
			    struct mutex_waiter *waiter)
{
	struct ww_mutex *ww;

	ww = container_of(lock, struct ww_mutex, base);

	/*
	 * If ww->ctx is set the contents are undefined, only
	 * by acquiring wait_lock there is a guarantee that
	 * they are not invalid when reading.
	 *
	 * As such, when deadlock detection needs to be
	 * performed the optimistic spinning cannot be done.
	 *
	 * Check this in every inner iteration because we may
	 * be racing against another thread's ww_mutex_lock.
	 */
	if (ww_ctx->acquired > 0 && READ_ONCE(ww->ctx))
		return false;

	/*
	 * If we aren't on the wait list yet, cancel the spin
	 * if there are waiters. We want  to avoid stealing the
	 * lock from a waiter with an earlier stamp, since the
	 * other thread may already own a lock that we also
	 * need.
	 */
	if (!waiter && (atomic_long_read(&lock->owner) & MUTEX_FLAG_WAITERS))
		return false;

	/*
	 * Similarly, stop spinning if we are no longer the
	 * first waiter.
	 */
	if (waiter && !__mutex_waiter_is_first(lock, waiter))
		return false;

	return true;
}

/*
 * Look out! "owner" is an entirely speculative pointer access and not
 * reliable.
 *
 * "noinline" so that this function shows up on perf profiles.
 */
static noinline
bool mutex_spin_on_owner(struct mutex *lock, struct task_struct *owner,
			 struct ww_acquire_ctx *ww_ctx, struct mutex_waiter *waiter)
{
	bool ret = true;

	lockdep_assert_preemption_disabled();

	while (__mutex_owner(lock) == owner) {
		/*
		 * Ensure we emit the owner->on_cpu, dereference _after_
		 * checking lock->owner still matches owner. And we already
		 * disabled preemption which is equal to the RCU read-side
		 * crital section in optimistic spinning code. Thus the
		 * task_strcut structure won't go away during the spinning
		 * period
		 */
		barrier();

		/*
		 * Use vcpu_is_preempted to detect lock holder preemption issue.
		 */
		if (!owner_on_cpu(owner) || need_resched()) {
			ret = false;
			break;
		}

		if (ww_ctx && !ww_mutex_spin_on_owner(lock, ww_ctx, waiter)) {
			ret = false;
			break;
		}

		cpu_relax();
	}

	return ret;
}

/*
 * Initial check for entering the mutex spinning loop
 */
static inline int mutex_can_spin_on_owner(struct mutex *lock)
{
	struct task_struct *owner;
	int retval = 1;

	lockdep_assert_preemption_disabled();

	if (need_resched())
		return 0;

	/*
	 * We already disabled preemption which is equal to the RCU read-side
	 * crital section in optimistic spinning code. Thus the task_strcut
	 * structure won't go away during the spinning period.
	 */
	owner = __mutex_owner(lock);
	if (owner)
		retval = owner_on_cpu(owner);

	/*
	 * If lock->owner is not set, the mutex has been released. Return true
	 * such that we'll trylock in the spin path, which is a faster option
	 * than the blocking slow path.
	 */
	return retval;
}

/*
 * Optimistic spinning.
 *
 * We try to spin for acquisition when we find that the lock owner
 * is currently running on a (different) CPU and while we don't
 * need to reschedule. The rationale is that if the lock owner is
 * running, it is likely to release the lock soon.
 *
 * The mutex spinners are queued up using MCS lock so that only one
 * spinner can compete for the mutex. However, if mutex spinning isn't
 * going to happen, there is no point in going through the lock/unlock
 * overhead.
 *
 * Returns true when the lock was taken, otherwise false, indicating
 * that we need to jump to the slowpath and sleep.
 *
 * The waiter flag is set to true if the spinner is a waiter in the wait
 * queue. The waiter-spinner will spin on the lock directly and concurrently
 * with the spinner at the head of the OSQ, if present, until the owner is
 * changed to itself.
 */
static __always_inline bool
mutex_optimistic_spin(struct mutex *lock, struct ww_acquire_ctx *ww_ctx,
		      struct mutex_waiter *waiter)
{
	if (!waiter) {
		/*
		 * The purpose of the mutex_can_spin_on_owner() function is
		 * to eliminate the overhead of osq_lock() and osq_unlock()
		 * in case spinning isn't possible. As a waiter-spinner
		 * is not going to take OSQ lock anyway, there is no need
		 * to call mutex_can_spin_on_owner().
		 */
		if (!mutex_can_spin_on_owner(lock))
			goto fail;

		/*
		 * In order to avoid a stampede of mutex spinners trying to
		 * acquire the mutex all at once, the spinners need to take a
		 * MCS (queued) lock first before spinning on the owner field.
		 */
		if (!osq_lock(&lock->osq))
			goto fail;
	}

	for (;;) {
		struct task_struct *owner;

		/* Try to acquire the mutex... */
		owner = __mutex_trylock_or_owner(lock);
		if (!owner)
			break;

		/*
		 * There's an owner, wait for it to either
		 * release the lock or go to sleep.
		 */
		if (!mutex_spin_on_owner(lock, owner, ww_ctx, waiter))
			goto fail_unlock;

		/*
		 * The cpu_relax() call is a compiler barrier which forces
		 * everything in this loop to be re-loaded. We don't need
		 * memory barriers as we'll eventually observe the right
		 * values at the cost of a few extra spins.
		 */
		cpu_relax();
	}

	if (!waiter)
		osq_unlock(&lock->osq);

	return true;


fail_unlock:
	if (!waiter)
		osq_unlock(&lock->osq);

fail:
	/*
	 * If we fell out of the spin path because of need_resched(),
	 * reschedule now, before we try-lock the mutex. This avoids getting
	 * scheduled out right after we obtained the mutex.
	 */
	if (need_resched()) {
		/*
		 * We _should_ have TASK_RUNNING here, but just in case
		 * we do not, make it so, otherwise we might get stuck.
		 */
		__set_current_state(TASK_RUNNING);
		schedule_preempt_disabled();
	}

	return false;
}
#else
static __always_inline bool
mutex_optimistic_spin(struct mutex *lock, struct ww_acquire_ctx *ww_ctx,
		      struct mutex_waiter *waiter)
{
	return false;
}
#endif

static noinline void __sched __mutex_unlock_slowpath(struct mutex *lock, unsigned long ip);

/**
 * mutex_unlock - release the mutex
 * @lock: the mutex to be released
 *
 * Unlock a mutex that has been locked by this task previously.
 *
 * This function must not be used in interrupt context. Unlocking
 * of a not locked mutex is not allowed.
 *
 * This function is similar to (but not equivalent to) up().
 */
void __sched mutex_unlock(struct mutex *lock)
{
#ifndef CONFIG_DEBUG_LOCK_ALLOC
	if (__mutex_unlock_fast(lock))
		return;
#endif
	__mutex_unlock_slowpath(lock, _RET_IP_);
}
EXPORT_SYMBOL(mutex_unlock);

/**
 * ww_mutex_unlock - release the w/w mutex
 * @lock: the mutex to be released
 *
 * Unlock a mutex that has been locked by this task previously with any of the
 * ww_mutex_lock* functions (with or without an acquire context). It is
 * forbidden to release the locks after releasing the acquire context.
 *
 * This function must not be used in interrupt context. Unlocking
 * of a unlocked mutex is not allowed.
 */
void __sched ww_mutex_unlock(struct ww_mutex *lock)
{
	__ww_mutex_unlock(lock);
	mutex_unlock(&lock->base);
}
EXPORT_SYMBOL(ww_mutex_unlock);

/*
 * Lock a mutex (possibly interruptible), slowpath:
 */
static __always_inline int __sched
__mutex_lock_common(struct mutex *lock, unsigned int state, unsigned int subclass,
		    struct lockdep_map *nest_lock, unsigned long ip,
		    struct ww_acquire_ctx *ww_ctx, const bool use_ww_ctx)
{
	struct mutex_waiter waiter;
	struct ww_mutex *ww;
	int ret;

	if (!use_ww_ctx)
		ww_ctx = NULL;

	might_sleep();

	MUTEX_WARN_ON(lock->magic != lock);

	ww = container_of(lock, struct ww_mutex, base);
	if (ww_ctx) {
		if (unlikely(ww_ctx == READ_ONCE(ww->ctx)))
			return -EALREADY;

		/*
		 * Reset the wounded flag after a kill. No other process can
		 * race and wound us here since they can't have a valid owner
		 * pointer if we don't have any locks held.
		 */
		if (ww_ctx->acquired == 0)
			ww_ctx->wounded = 0;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
		nest_lock = &ww_ctx->dep_map;
#endif
	}

	preempt_disable();
	mutex_acquire_nest(&lock->dep_map, subclass, 0, nest_lock, ip);

	trace_contention_begin(lock, LCB_F_MUTEX | LCB_F_SPIN);
	if (__mutex_trylock(lock) ||
	    mutex_optimistic_spin(lock, ww_ctx, NULL)) {
		/* got the lock, yay! */
		lock_acquired(&lock->dep_map, ip);
		if (ww_ctx)
			ww_mutex_set_context_fastpath(ww, ww_ctx);
		trace_contention_end(lock, 0);
		preempt_enable();
		return 0;
	}

	raw_spin_lock(&lock->wait_lock);
	/*
	 * After waiting to acquire the wait_lock, try again.
	 */
	if (__mutex_trylock(lock)) {
		if (ww_ctx)
			__ww_mutex_check_waiters(lock, ww_ctx);

		goto skip_wait;
	}

	debug_mutex_lock_common(lock, &waiter);
	waiter.task = current;
	if (use_ww_ctx)
		waiter.ww_ctx = ww_ctx;

	lock_contended(&lock->dep_map, ip);

	if (!use_ww_ctx) {
		/* add waiting tasks to the end of the waitqueue (FIFO): */
		__mutex_add_waiter(lock, &waiter, &lock->wait_list);
	} else {
		/*
		 * Add in stamp order, waking up waiters that must kill
		 * themselves.
		 */
		ret = __ww_mutex_add_waiter(&waiter, lock, ww_ctx);
		if (ret)
			goto err_early_kill;
	}

	trace_android_vh_mutex_wait_start(lock);
	set_current_state(state);
	trace_contention_begin(lock, LCB_F_MUTEX);
	for (;;) {
		bool first;

		/*
		 * Once we hold wait_lock, we're serialized against
		 * mutex_unlock() handing the lock off to us, do a trylock
		 * before testing the error conditions to make sure we pick up
		 * the handoff.
		 */
		if (__mutex_trylock(lock))
			goto acquired;

		/*
		 * Check for signals and kill conditions while holding
		 * wait_lock. This ensures the lock cancellation is ordered
		 * against mutex_unlock() and wake-ups do not go missing.
		 */
		if (signal_pending_state(state, current)) {
			ret = -EINTR;
			goto err;
		}

		if (ww_ctx) {
			ret = __ww_mutex_check_kill(lock, &waiter, ww_ctx);
			if (ret)
				goto err;
		}

		raw_spin_unlock(&lock->wait_lock);
		schedule_preempt_disabled();

		first = __mutex_waiter_is_first(lock, &waiter);

		set_current_state(state);
		/*
		 * Here we order against unlock; we must either see it change
		 * state back to RUNNING and fall through the next schedule(),
		 * or we must see its unlock and acquire.
		 */
		if (__mutex_trylock_or_handoff(lock, first))
			break;

		if (first) {
			trace_contention_begin(lock, LCB_F_MUTEX | LCB_F_SPIN);
			if (mutex_optimistic_spin(lock, ww_ctx, &waiter))
				break;
			trace_contention_begin(lock, LCB_F_MUTEX);
		}

		raw_spin_lock(&lock->wait_lock);
	}
	raw_spin_lock(&lock->wait_lock);
acquired:
	__set_current_state(TASK_RUNNING);
	trace_android_vh_mutex_wait_finish(lock);

	if (ww_ctx) {
		/*
		 * Wound-Wait; we stole the lock (!first_waiter), check the
		 * waiters as anyone might want to wound us.
		 */
		if (!ww_ctx->is_wait_die &&
		    !__mutex_waiter_is_first(lock, &waiter))
			__ww_mutex_check_waiters(lock, ww_ctx);
	}

	__mutex_remove_waiter(lock, &waiter);

	debug_mutex_free_waiter(&waiter);

skip_wait:
	/* got the lock - cleanup and rejoice! */
	lock_acquired(&lock->dep_map, ip);
	trace_contention_end(lock, 0);

	if (ww_ctx)
		ww_mutex_lock_acquired(ww, ww_ctx);

	raw_spin_unlock(&lock->wait_lock);
	preempt_enable();
	return 0;

err:
	__set_current_state(TASK_RUNNING);
	trace_android_vh_mutex_wait_finish(lock);
	__mutex_remove_waiter(lock, &waiter);
err_early_kill:
	trace_contention_end(lock, ret);
	raw_spin_unlock(&lock->wait_lock);
	debug_mutex_free_waiter(&waiter);
	mutex_release(&lock->dep_map, ip);
	preempt_enable();
	return ret;
}

static int __sched
__mutex_lock(struct mutex *lock, unsigned int state, unsigned int subclass,
	     struct lockdep_map *nest_lock, unsigned long ip)
{
	return __mutex_lock_common(lock, state, subclass, nest_lock, ip, NULL, false);
}

static int __sched
__ww_mutex_lock(struct mutex *lock, unsigned int state, unsigned int subclass,
		unsigned long ip, struct ww_acquire_ctx *ww_ctx)
{
	return __mutex_lock_common(lock, state, subclass, NULL, ip, ww_ctx, true);
}

/**
 * ww_mutex_trylock - tries to acquire the w/w mutex with optional acquire context
 * @ww: mutex to lock
 * @ww_ctx: optional w/w acquire context
 *
 * Trylocks a mutex with the optional acquire context; no deadlock detection is
 * possible. Returns 1 if the mutex has been acquired successfully, 0 otherwise.
 *
 * Unlike ww_mutex_lock, no deadlock handling is performed. However, if a @ctx is
 * specified, -EALREADY handling may happen in calls to ww_mutex_trylock.
 *
 * A mutex acquired with this function must be released with ww_mutex_unlock.
 */
int ww_mutex_trylock(struct ww_mutex *ww, struct ww_acquire_ctx *ww_ctx)
{
	if (!ww_ctx)
		return mutex_trylock(&ww->base);

	MUTEX_WARN_ON(ww->base.magic != &ww->base);

	/*
	 * Reset the wounded flag after a kill. No other process can
	 * race and wound us here, since they can't have a valid owner
	 * pointer if we don't have any locks held.
	 */
	if (ww_ctx->acquired == 0)
		ww_ctx->wounded = 0;

	if (__mutex_trylock(&ww->base)) {
		ww_mutex_set_context_fastpath(ww, ww_ctx);
		mutex_acquire_nest(&ww->base.dep_map, 0, 1, &ww_ctx->dep_map, _RET_IP_);
		return 1;
	}

	return 0;
}
EXPORT_SYMBOL(ww_mutex_trylock);

#ifdef CONFIG_DEBUG_LOCK_ALLOC
void __sched
mutex_lock_nested(struct mutex *lock, unsigned int subclass)
{
	__mutex_lock(lock, TASK_UNINTERRUPTIBLE, subclass, NULL, _RET_IP_);
}

EXPORT_SYMBOL_GPL(mutex_lock_nested);

void __sched
_mutex_lock_nest_lock(struct mutex *lock, struct lockdep_map *nest)
{
	__mutex_lock(lock, TASK_UNINTERRUPTIBLE, 0, nest, _RET_IP_);
}
EXPORT_SYMBOL_GPL(_mutex_lock_nest_lock);

int __sched
mutex_lock_killable_nested(struct mutex *lock, unsigned int subclass)
{
	return __mutex_lock(lock, TASK_KILLABLE, subclass, NULL, _RET_IP_);
}
EXPORT_SYMBOL_GPL(mutex_lock_killable_nested);

int __sched
mutex_lock_interruptible_nested(struct mutex *lock, unsigned int subclass)
{
	return __mutex_lock(lock, TASK_INTERRUPTIBLE, subclass, NULL, _RET_IP_);
}
EXPORT_SYMBOL_GPL(mutex_lock_interruptible_nested);

void __sched
mutex_lock_io_nested(struct mutex *lock, unsigned int subclass)
{
	int token;

	might_sleep();

	token = io_schedule_prepare();
	__mutex_lock_common(lock, TASK_UNINTERRUPTIBLE,
			    subclass, NULL, _RET_IP_, NULL, 0);
	io_schedule_finish(token);
}
EXPORT_SYMBOL_GPL(mutex_lock_io_nested);

static inline int
ww_mutex_deadlock_injection(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
#ifdef CONFIG_DEBUG_WW_MUTEX_SLOWPATH
	unsigned tmp;

	if (ctx->deadlock_inject_countdown-- == 0) {
		tmp = ctx->deadlock_inject_interval;
		if (tmp > UINT_MAX/4)
			tmp = UINT_MAX;
		else
			tmp = tmp*2 + tmp + tmp/2;

		ctx->deadlock_inject_interval = tmp;
		ctx->deadlock_inject_countdown = tmp;
		ctx->contending_lock = lock;

		ww_mutex_unlock(lock);

		return -EDEADLK;
	}
#endif

	return 0;
}

int __sched
ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	int ret;

	might_sleep();
	ret =  __ww_mutex_lock(&lock->base, TASK_UNINTERRUPTIBLE,
			       0, _RET_IP_, ctx);
	if (!ret && ctx && ctx->acquired > 1)
		return ww_mutex_deadlock_injection(lock, ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(ww_mutex_lock);

int __sched
ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	int ret;

	might_sleep();
	ret = __ww_mutex_lock(&lock->base, TASK_INTERRUPTIBLE,
			      0, _RET_IP_, ctx);

	if (!ret && ctx && ctx->acquired > 1)
		return ww_mutex_deadlock_injection(lock, ctx);

	return ret;
}
EXPORT_SYMBOL_GPL(ww_mutex_lock_interruptible);

#endif

/*
 * Release the lock, slowpath:
 */
static noinline void __sched __mutex_unlock_slowpath(struct mutex *lock, unsigned long ip)
{
	struct task_struct *next = NULL;
	DEFINE_WAKE_Q(wake_q);
	unsigned long owner;

	mutex_release(&lock->dep_map, ip);

	/*
	 * Release the lock before (potentially) taking the spinlock such that
	 * other contenders can get on with things ASAP.
	 *
	 * Except when HANDOFF, in that case we must not clear the owner field,
	 * but instead set it to the top waiter.
	 */
	owner = atomic_long_read(&lock->owner);
	for (;;) {
		MUTEX_WARN_ON(__owner_task(owner) != current);
		MUTEX_WARN_ON(owner & MUTEX_FLAG_PICKUP);

		if (owner & MUTEX_FLAG_HANDOFF)
			break;

		if (atomic_long_try_cmpxchg_release(&lock->owner, &owner, __owner_flags(owner))) {
			if (owner & MUTEX_FLAG_WAITERS)
				break;

			return;
		}
	}

	raw_spin_lock(&lock->wait_lock);
	debug_mutex_unlock(lock);
	if (!list_empty(&lock->wait_list)) {
		/* get the first entry from the wait-list: */
		struct mutex_waiter *waiter =
			list_first_entry(&lock->wait_list,
					 struct mutex_waiter, list);

		next = waiter->task;

		debug_mutex_wake_waiter(lock, waiter);
		wake_q_add(&wake_q, next);
	}

	if (owner & MUTEX_FLAG_HANDOFF)
		__mutex_handoff(lock, next);

	raw_spin_unlock(&lock->wait_lock);

	wake_up_q(&wake_q);
}

#ifndef CONFIG_DEBUG_LOCK_ALLOC
/*
 * Here come the less common (and hence less performance-critical) APIs:
 * mutex_lock_interruptible() and mutex_trylock().
 */
static noinline int __sched
__mutex_lock_killable_slowpath(struct mutex *lock);

static noinline int __sched
__mutex_lock_interruptible_slowpath(struct mutex *lock);

/**
 * mutex_lock_interruptible() - Acquire the mutex, interruptible by signals.
 * @lock: The mutex to be acquired.
 *
 * Lock the mutex like mutex_lock().  If a signal is delivered while the
 * process is sleeping, this function will return without acquiring the
 * mutex.
 *
 * Context: Process context.
 * Return: 0 if the lock was successfully acquired or %-EINTR if a
 * signal arrived.
 */
int __sched mutex_lock_interruptible(struct mutex *lock)
{
	might_sleep();

	if (__mutex_trylock_fast(lock))
		return 0;

	return __mutex_lock_interruptible_slowpath(lock);
}

EXPORT_SYMBOL(mutex_lock_interruptible);

/**
 * mutex_lock_killable() - Acquire the mutex, interruptible by fatal signals.
 * @lock: The mutex to be acquired.
 *
 * Lock the mutex like mutex_lock().  If a signal which will be fatal to
 * the current process is delivered while the process is sleeping, this
 * function will return without acquiring the mutex.
 *
 * Context: Process context.
 * Return: 0 if the lock was successfully acquired or %-EINTR if a
 * fatal signal arrived.
 */
int __sched mutex_lock_killable(struct mutex *lock)
{
	might_sleep();

	if (__mutex_trylock_fast(lock))
		return 0;

	return __mutex_lock_killable_slowpath(lock);
}
EXPORT_SYMBOL(mutex_lock_killable);

/**
 * mutex_lock_io() - Acquire the mutex and mark the process as waiting for I/O
 * @lock: The mutex to be acquired.
 *
 * Lock the mutex like mutex_lock().  While the task is waiting for this
 * mutex, it will be accounted as being in the IO wait state by the
 * scheduler.
 *
 * Context: Process context.
 */
void __sched mutex_lock_io(struct mutex *lock)
{
	int token;

	token = io_schedule_prepare();
	mutex_lock(lock);
	io_schedule_finish(token);
}
EXPORT_SYMBOL_GPL(mutex_lock_io);

static noinline void __sched
__mutex_lock_slowpath(struct mutex *lock)
{
	__mutex_lock(lock, TASK_UNINTERRUPTIBLE, 0, NULL, _RET_IP_);
}

static noinline int __sched
__mutex_lock_killable_slowpath(struct mutex *lock)
{
	return __mutex_lock(lock, TASK_KILLABLE, 0, NULL, _RET_IP_);
}

static noinline int __sched
__mutex_lock_interruptible_slowpath(struct mutex *lock)
{
	return __mutex_lock(lock, TASK_INTERRUPTIBLE, 0, NULL, _RET_IP_);
}

static noinline int __sched
__ww_mutex_lock_slowpath(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	return __ww_mutex_lock(&lock->base, TASK_UNINTERRUPTIBLE, 0,
			       _RET_IP_, ctx);
}

static noinline int __sched
__ww_mutex_lock_interruptible_slowpath(struct ww_mutex *lock,
					    struct ww_acquire_ctx *ctx)
{
	return __ww_mutex_lock(&lock->base, TASK_INTERRUPTIBLE, 0,
			       _RET_IP_, ctx);
}

#endif

/**
 * mutex_trylock - try to acquire the mutex, without waiting
 * @lock: the mutex to be acquired
 *
 * Try to acquire the mutex atomically. Returns 1 if the mutex
 * has been acquired successfully, and 0 on contention.
 *
 * NOTE: this function follows the spin_trylock() convention, so
 * it is negated from the down_trylock() return values! Be careful
 * about this when converting semaphore users to mutexes.
 *
 * This function must not be used in interrupt context. The
 * mutex must be released by the same task that acquired it.
 */
int __sched mutex_trylock(struct mutex *lock)
{
	bool locked;

	MUTEX_WARN_ON(lock->magic != lock);

	locked = __mutex_trylock(lock);
	if (locked)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return locked;
}
EXPORT_SYMBOL(mutex_trylock);

#ifndef CONFIG_DEBUG_LOCK_ALLOC
int __sched
ww_mutex_lock(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	might_sleep();

	if (__mutex_trylock_fast(&lock->base)) {
		if (ctx)
			ww_mutex_set_context_fastpath(lock, ctx);
		return 0;
	}

	return __ww_mutex_lock_slowpath(lock, ctx);
}
EXPORT_SYMBOL(ww_mutex_lock);

int __sched
ww_mutex_lock_interruptible(struct ww_mutex *lock, struct ww_acquire_ctx *ctx)
{
	might_sleep();

	if (__mutex_trylock_fast(&lock->base)) {
		if (ctx)
			ww_mutex_set_context_fastpath(lock, ctx);
		return 0;
	}

	return __ww_mutex_lock_interruptible_slowpath(lock, ctx);
}
EXPORT_SYMBOL(ww_mutex_lock_interruptible);

#endif /* !CONFIG_DEBUG_LOCK_ALLOC */
#endif /* !CONFIG_PREEMPT_RT */

/**
 * atomic_dec_and_mutex_lock - return holding mutex if we dec to 0
 * @cnt: the atomic which we are to dec
 * @lock: the mutex to return holding if we dec to 0
 *
 * return true and hold lock if we dec to 0, return false otherwise
 */
int atomic_dec_and_mutex_lock(atomic_t *cnt, struct mutex *lock)
{
	/* dec if we can't possibly hit 0 */
	if (atomic_add_unless(cnt, -1, 1))
		return 0;
	/* we might hit 0, so take the lock */
	mutex_lock(lock);
	if (!atomic_dec_and_test(cnt)) {
		/* when we actually did the dec, we didn't hit 0 */
		mutex_unlock(lock);
		return 0;
	}
	/* we hit 0, and we hold the lock */
	return 1;
}
EXPORT_SYMBOL(atomic_dec_and_mutex_lock);
