/*
 * RT-Mutexes: simple blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner.
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2005-2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *  Copyright (C) 2005 Kihon Technologies Inc., Steven Rostedt
 *  Copyright (C) 2006 Esben Nielsen
 *
 *  See Documentation/rt-mutex-design.txt for details.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/timer.h>

#include "rtmutex_common.h"

/*
 * lock->owner state tracking:
 *
 * lock->owner holds the task_struct pointer of the owner. Bit 0 and 1
 * are used to keep track of the "owner is pending" and "lock has
 * waiters" state.
 *
 * owner	bit1	bit0
 * NULL		0	0	lock is free (fast acquire possible)
 * NULL		0	1	invalid state
 * NULL		1	0	Transitional State*
 * NULL		1	1	invalid state
 * taskpointer	0	0	lock is held (fast release possible)
 * taskpointer	0	1	task is pending owner
 * taskpointer	1	0	lock is held and has waiters
 * taskpointer	1	1	task is pending owner and lock has more waiters
 *
 * Pending ownership is assigned to the top (highest priority)
 * waiter of the lock, when the lock is released. The thread is woken
 * up and can now take the lock. Until the lock is taken (bit 0
 * cleared) a competing higher priority thread can steal the lock
 * which puts the woken up thread back on the waiters list.
 *
 * The fast atomic compare exchange based acquire and release is only
 * possible when bit 0 and 1 of lock->owner are 0.
 *
 * (*) There's a small time where the owner can be NULL and the
 * "lock has waiters" bit is set.  This can happen when grabbing the lock.
 * To prevent a cmpxchg of the owner releasing the lock, we need to set this
 * bit before looking at the lock, hence the reason this is a transitional
 * state.
 */

static void
rt_mutex_set_owner(struct rt_mutex *lock, struct task_struct *owner,
		   unsigned long mask)
{
	unsigned long val = (unsigned long)owner | mask;

	if (rt_mutex_has_waiters(lock))
		val |= RT_MUTEX_HAS_WAITERS;

	lock->owner = (struct task_struct *)val;
}

static inline void clear_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner & ~RT_MUTEX_HAS_WAITERS);
}

static void fixup_rt_mutex_waiters(struct rt_mutex *lock)
{
	if (!rt_mutex_has_waiters(lock))
		clear_rt_mutex_waiters(lock);
}

/*
 * We can speed up the acquire/release, if the architecture
 * supports cmpxchg and if there's no debugging state to be set up
 */
#if defined(__HAVE_ARCH_CMPXCHG) && !defined(CONFIG_DEBUG_RT_MUTEXES)
# define rt_mutex_cmpxchg(l,c,n)	(cmpxchg(&l->owner, c, n) == c)
static inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	unsigned long owner, *p = (unsigned long *) &lock->owner;

	do {
		owner = *p;
	} while (cmpxchg(p, owner, owner | RT_MUTEX_HAS_WAITERS) != owner);
}
#else
# define rt_mutex_cmpxchg(l,c,n)	(0)
static inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner | RT_MUTEX_HAS_WAITERS);
}
#endif

/*
 * Calculate task priority from the waiter list priority
 *
 * Return task->normal_prio when the waiter list is empty or when
 * the waiter is not allowed to do priority boosting
 */
int rt_mutex_getprio(struct task_struct *task)
{
	if (likely(!task_has_pi_waiters(task)))
		return task->normal_prio;

	return min(task_top_pi_waiter(task)->pi_list_entry.prio,
		   task->normal_prio);
}

/*
 * Adjust the priority of a task, after its pi_waiters got modified.
 *
 * This can be both boosting and unboosting. task->pi_lock must be held.
 */
static void __rt_mutex_adjust_prio(struct task_struct *task)
{
	int prio = rt_mutex_getprio(task);

	if (task->prio != prio)
		rt_mutex_setprio(task, prio);
}

/*
 * Adjust task priority (undo boosting). Called from the exit path of
 * rt_mutex_slowunlock() and rt_mutex_slowlock().
 *
 * (Note: We do this outside of the protection of lock->wait_lock to
 * allow the lock to be taken while or before we readjust the priority
 * of task. We do not use the spin_xx_mutex() variants here as we are
 * outside of the debug path.)
 */
static void rt_mutex_adjust_prio(struct task_struct *task)
{
	unsigned long flags;

	spin_lock_irqsave(&task->pi_lock, flags);
	__rt_mutex_adjust_prio(task);
	spin_unlock_irqrestore(&task->pi_lock, flags);
}

/*
 * Max number of times we'll walk the boosting chain:
 */
int max_lock_depth = 1024;

/*
 * Adjust the priority chain. Also used for deadlock detection.
 * Decreases task's usage by one - may thus free the task.
 * Returns 0 or -EDEADLK.
 */
static int rt_mutex_adjust_prio_chain(struct task_struct *task,
				      int deadlock_detect,
				      struct rt_mutex *orig_lock,
				      struct rt_mutex_waiter *orig_waiter,
				      struct task_struct *top_task)
{
	struct rt_mutex *lock;
	struct rt_mutex_waiter *waiter, *top_waiter = orig_waiter;
	int detect_deadlock, ret = 0, depth = 0;
	unsigned long flags;

	detect_deadlock = debug_rt_mutex_detect_deadlock(orig_waiter,
							 deadlock_detect);

	/*
	 * The (de)boosting is a step by step approach with a lot of
	 * pitfalls. We want this to be preemptible and we want hold a
	 * maximum of two locks per step. So we have to check
	 * carefully whether things change under us.
	 */
 again:
	if (++depth > max_lock_depth) {
		static int prev_max;

		/*
		 * Print this only once. If the admin changes the limit,
		 * print a new message when reaching the limit again.
		 */
		if (prev_max != max_lock_depth) {
			prev_max = max_lock_depth;
			printk(KERN_WARNING "Maximum lock depth %d reached "
			       "task: %s (%d)\n", max_lock_depth,
			       top_task->comm, task_pid_nr(top_task));
		}
		put_task_struct(task);

		return deadlock_detect ? -EDEADLK : 0;
	}
 retry:
	/*
	 * Task can not go away as we did a get_task() before !
	 */
	spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	/*
	 * Check whether the end of the boosting chain has been
	 * reached or the state of the chain has changed while we
	 * dropped the locks.
	 */
	if (!waiter || !waiter->task)
		goto out_unlock_pi;

	/*
	 * Check the orig_waiter state. After we dropped the locks,
	 * the previous owner of the lock might have released the lock
	 * and made us the pending owner:
	 */
	if (orig_waiter && !orig_waiter->task)
		goto out_unlock_pi;

	/*
	 * Drop out, when the task has no waiters. Note,
	 * top_waiter can be NULL, when we are in the deboosting
	 * mode!
	 */
	if (top_waiter && (!task_has_pi_waiters(task) ||
			   top_waiter != task_top_pi_waiter(task)))
		goto out_unlock_pi;

	/*
	 * When deadlock detection is off then we check, if further
	 * priority adjustment is necessary.
	 */
	if (!detect_deadlock && waiter->list_entry.prio == task->prio)
		goto out_unlock_pi;

	lock = waiter->lock;
	if (!spin_trylock(&lock->wait_lock)) {
		spin_unlock_irqrestore(&task->pi_lock, flags);
		cpu_relax();
		goto retry;
	}

	/* Deadlock detection */
	if (lock == orig_lock || rt_mutex_owner(lock) == top_task) {
		debug_rt_mutex_deadlock(deadlock_detect, orig_waiter, lock);
		spin_unlock(&lock->wait_lock);
		ret = deadlock_detect ? -EDEADLK : 0;
		goto out_unlock_pi;
	}

	top_waiter = rt_mutex_top_waiter(lock);

	/* Requeue the waiter */
	plist_del(&waiter->list_entry, &lock->wait_list);
	waiter->list_entry.prio = task->prio;
	plist_add(&waiter->list_entry, &lock->wait_list);

	/* Release the task */
	spin_unlock_irqrestore(&task->pi_lock, flags);
	put_task_struct(task);

	/* Grab the next task */
	task = rt_mutex_owner(lock);
	get_task_struct(task);
	spin_lock_irqsave(&task->pi_lock, flags);

	if (waiter == rt_mutex_top_waiter(lock)) {
		/* Boost the owner */
		plist_del(&top_waiter->pi_list_entry, &task->pi_waiters);
		waiter->pi_list_entry.prio = waiter->list_entry.prio;
		plist_add(&waiter->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);

	} else if (top_waiter == waiter) {
		/* Deboost the owner */
		plist_del(&waiter->pi_list_entry, &task->pi_waiters);
		waiter = rt_mutex_top_waiter(lock);
		waiter->pi_list_entry.prio = waiter->list_entry.prio;
		plist_add(&waiter->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);
	}

	spin_unlock_irqrestore(&task->pi_lock, flags);

	top_waiter = rt_mutex_top_waiter(lock);
	spin_unlock(&lock->wait_lock);

	if (!detect_deadlock && waiter != top_waiter)
		goto out_put_task;

	goto again;

 out_unlock_pi:
	spin_unlock_irqrestore(&task->pi_lock, flags);
 out_put_task:
	put_task_struct(task);

	return ret;
}

/*
 * Optimization: check if we can steal the lock from the
 * assigned pending owner [which might not have taken the
 * lock yet]:
 */
static inline int try_to_steal_lock(struct rt_mutex *lock,
				    struct task_struct *task)
{
	struct task_struct *pendowner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *next;
	unsigned long flags;

	if (!rt_mutex_owner_pending(lock))
		return 0;

	if (pendowner == task)
		return 1;

	spin_lock_irqsave(&pendowner->pi_lock, flags);
	if (task->prio >= pendowner->prio) {
		spin_unlock_irqrestore(&pendowner->pi_lock, flags);
		return 0;
	}

	/*
	 * Check if a waiter is enqueued on the pending owners
	 * pi_waiters list. Remove it and readjust pending owners
	 * priority.
	 */
	if (likely(!rt_mutex_has_waiters(lock))) {
		spin_unlock_irqrestore(&pendowner->pi_lock, flags);
		return 1;
	}

	/* No chain handling, pending owner is not blocked on anything: */
	next = rt_mutex_top_waiter(lock);
	plist_del(&next->pi_list_entry, &pendowner->pi_waiters);
	__rt_mutex_adjust_prio(pendowner);
	spin_unlock_irqrestore(&pendowner->pi_lock, flags);

	/*
	 * We are going to steal the lock and a waiter was
	 * enqueued on the pending owners pi_waiters queue. So
	 * we have to enqueue this waiter into
	 * task->pi_waiters list. This covers the case,
	 * where task is boosted because it holds another
	 * lock and gets unboosted because the booster is
	 * interrupted, so we would delay a waiter with higher
	 * priority as task->normal_prio.
	 *
	 * Note: in the rare case of a SCHED_OTHER task changing
	 * its priority and thus stealing the lock, next->task
	 * might be task:
	 */
	if (likely(next->task != task)) {
		spin_lock_irqsave(&task->pi_lock, flags);
		plist_add(&next->pi_list_entry, &task->pi_waiters);
		__rt_mutex_adjust_prio(task);
		spin_unlock_irqrestore(&task->pi_lock, flags);
	}
	return 1;
}

/*
 * Try to take an rt-mutex
 *
 * This fails
 * - when the lock has a real owner
 * - when a different pending owner exists and has higher priority than current
 *
 * Must be called with lock->wait_lock held.
 */
static int try_to_take_rt_mutex(struct rt_mutex *lock)
{
	/*
	 * We have to be careful here if the atomic speedups are
	 * enabled, such that, when
	 *  - no other waiter is on the lock
	 *  - the lock has been released since we did the cmpxchg
	 * the lock can be released or taken while we are doing the
	 * checks and marking the lock with RT_MUTEX_HAS_WAITERS.
	 *
	 * The atomic acquire/release aware variant of
	 * mark_rt_mutex_waiters uses a cmpxchg loop. After setting
	 * the WAITERS bit, the atomic release / acquire can not
	 * happen anymore and lock->wait_lock protects us from the
	 * non-atomic case.
	 *
	 * Note, that this might set lock->owner =
	 * RT_MUTEX_HAS_WAITERS in the case the lock is not contended
	 * any more. This is fixed up when we take the ownership.
	 * This is the transitional state explained at the top of this file.
	 */
	mark_rt_mutex_waiters(lock);

	if (rt_mutex_owner(lock) && !try_to_steal_lock(lock, current))
		return 0;

	/* We got the lock. */
	debug_rt_mutex_lock(lock);

	rt_mutex_set_owner(lock, current, 0);

	rt_mutex_deadlock_account_lock(lock, current);

	return 1;
}

/*
 * Task blocks on lock.
 *
 * Prepare waiter and propagate pi chain
 *
 * This must be called with lock->wait_lock held.
 */
static int task_blocks_on_rt_mutex(struct rt_mutex *lock,
				   struct rt_mutex_waiter *waiter,
				   struct task_struct *task,
				   int detect_deadlock)
{
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *top_waiter = waiter;
	unsigned long flags;
	int chain_walk = 0, res;

	spin_lock_irqsave(&task->pi_lock, flags);
	__rt_mutex_adjust_prio(task);
	waiter->task = task;
	waiter->lock = lock;
	plist_node_init(&waiter->list_entry, task->prio);
	plist_node_init(&waiter->pi_list_entry, task->prio);

	/* Get the top priority waiter on the lock */
	if (rt_mutex_has_waiters(lock))
		top_waiter = rt_mutex_top_waiter(lock);
	plist_add(&waiter->list_entry, &lock->wait_list);

	task->pi_blocked_on = waiter;

	spin_unlock_irqrestore(&task->pi_lock, flags);

	if (waiter == rt_mutex_top_waiter(lock)) {
		spin_lock_irqsave(&owner->pi_lock, flags);
		plist_del(&top_waiter->pi_list_entry, &owner->pi_waiters);
		plist_add(&waiter->pi_list_entry, &owner->pi_waiters);

		__rt_mutex_adjust_prio(owner);
		if (owner->pi_blocked_on)
			chain_walk = 1;
		spin_unlock_irqrestore(&owner->pi_lock, flags);
	}
	else if (debug_rt_mutex_detect_deadlock(waiter, detect_deadlock))
		chain_walk = 1;

	if (!chain_walk)
		return 0;

	/*
	 * The owner can't disappear while holding a lock,
	 * so the owner struct is protected by wait_lock.
	 * Gets dropped in rt_mutex_adjust_prio_chain()!
	 */
	get_task_struct(owner);

	spin_unlock(&lock->wait_lock);

	res = rt_mutex_adjust_prio_chain(owner, detect_deadlock, lock, waiter,
					 task);

	spin_lock(&lock->wait_lock);

	return res;
}

/*
 * Wake up the next waiter on the lock.
 *
 * Remove the top waiter from the current tasks waiter list and from
 * the lock waiter list. Set it as pending owner. Then wake it up.
 *
 * Called with lock->wait_lock held.
 */
static void wakeup_next_waiter(struct rt_mutex *lock)
{
	struct rt_mutex_waiter *waiter;
	struct task_struct *pendowner;
	unsigned long flags;

	spin_lock_irqsave(&current->pi_lock, flags);

	waiter = rt_mutex_top_waiter(lock);
	plist_del(&waiter->list_entry, &lock->wait_list);

	/*
	 * Remove it from current->pi_waiters. We do not adjust a
	 * possible priority boost right now. We execute wakeup in the
	 * boosted mode and go back to normal after releasing
	 * lock->wait_lock.
	 */
	plist_del(&waiter->pi_list_entry, &current->pi_waiters);
	pendowner = waiter->task;
	waiter->task = NULL;

	rt_mutex_set_owner(lock, pendowner, RT_MUTEX_OWNER_PENDING);

	spin_unlock_irqrestore(&current->pi_lock, flags);

	/*
	 * Clear the pi_blocked_on variable and enqueue a possible
	 * waiter into the pi_waiters list of the pending owner. This
	 * prevents that in case the pending owner gets unboosted a
	 * waiter with higher priority than pending-owner->normal_prio
	 * is blocked on the unboosted (pending) owner.
	 */
	spin_lock_irqsave(&pendowner->pi_lock, flags);

	WARN_ON(!pendowner->pi_blocked_on);
	WARN_ON(pendowner->pi_blocked_on != waiter);
	WARN_ON(pendowner->pi_blocked_on->lock != lock);

	pendowner->pi_blocked_on = NULL;

	if (rt_mutex_has_waiters(lock)) {
		struct rt_mutex_waiter *next;

		next = rt_mutex_top_waiter(lock);
		plist_add(&next->pi_list_entry, &pendowner->pi_waiters);
	}
	spin_unlock_irqrestore(&pendowner->pi_lock, flags);

	wake_up_process(pendowner);
}

/*
 * Remove a waiter from a lock
 *
 * Must be called with lock->wait_lock held
 */
static void remove_waiter(struct rt_mutex *lock,
			  struct rt_mutex_waiter *waiter)
{
	int first = (waiter == rt_mutex_top_waiter(lock));
	struct task_struct *owner = rt_mutex_owner(lock);
	unsigned long flags;
	int chain_walk = 0;

	spin_lock_irqsave(&current->pi_lock, flags);
	plist_del(&waiter->list_entry, &lock->wait_list);
	waiter->task = NULL;
	current->pi_blocked_on = NULL;
	spin_unlock_irqrestore(&current->pi_lock, flags);

	if (first && owner != current) {

		spin_lock_irqsave(&owner->pi_lock, flags);

		plist_del(&waiter->pi_list_entry, &owner->pi_waiters);

		if (rt_mutex_has_waiters(lock)) {
			struct rt_mutex_waiter *next;

			next = rt_mutex_top_waiter(lock);
			plist_add(&next->pi_list_entry, &owner->pi_waiters);
		}
		__rt_mutex_adjust_prio(owner);

		if (owner->pi_blocked_on)
			chain_walk = 1;

		spin_unlock_irqrestore(&owner->pi_lock, flags);
	}

	WARN_ON(!plist_node_empty(&waiter->pi_list_entry));

	if (!chain_walk)
		return;

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(owner);

	spin_unlock(&lock->wait_lock);

	rt_mutex_adjust_prio_chain(owner, 0, lock, NULL, current);

	spin_lock(&lock->wait_lock);
}

/*
 * Recheck the pi chain, in case we got a priority setting
 *
 * Called from sched_setscheduler
 */
void rt_mutex_adjust_pi(struct task_struct *task)
{
	struct rt_mutex_waiter *waiter;
	unsigned long flags;

	spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	if (!waiter || waiter->list_entry.prio == task->prio) {
		spin_unlock_irqrestore(&task->pi_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&task->pi_lock, flags);

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(task);
	rt_mutex_adjust_prio_chain(task, 0, NULL, NULL, task);
}

/**
 * __rt_mutex_slowlock() - Perform the wait-wake-try-to-take loop
 * @lock:		 the rt_mutex to take
 * @state:		 the state the task should block in (TASK_INTERRUPTIBLE
 * 			 or TASK_UNINTERRUPTIBLE)
 * @timeout:		 the pre-initialized and started timer, or NULL for none
 * @waiter:		 the pre-initialized rt_mutex_waiter
 * @detect_deadlock:	 passed to task_blocks_on_rt_mutex
 *
 * lock->wait_lock must be held by the caller.
 */
static int __sched
__rt_mutex_slowlock(struct rt_mutex *lock, int state,
		    struct hrtimer_sleeper *timeout,
		    struct rt_mutex_waiter *waiter,
		    int detect_deadlock)
{
	int ret = 0;

	for (;;) {
		/* Try to acquire the lock: */
		if (try_to_take_rt_mutex(lock))
			break;

		/*
		 * TASK_INTERRUPTIBLE checks for signals and
		 * timeout. Ignored otherwise.
		 */
		if (unlikely(state == TASK_INTERRUPTIBLE)) {
			/* Signal pending? */
			if (signal_pending(current))
				ret = -EINTR;
			if (timeout && !timeout->task)
				ret = -ETIMEDOUT;
			if (ret)
				break;
		}

		/*
		 * waiter->task is NULL the first time we come here and
		 * when we have been woken up by the previous owner
		 * but the lock got stolen by a higher prio task.
		 */
		if (!waiter->task) {
			ret = task_blocks_on_rt_mutex(lock, waiter, current,
						      detect_deadlock);
			/*
			 * If we got woken up by the owner then start loop
			 * all over without going into schedule to try
			 * to get the lock now:
			 */
			if (unlikely(!waiter->task)) {
				/*
				 * Reset the return value. We might
				 * have returned with -EDEADLK and the
				 * owner released the lock while we
				 * were walking the pi chain.
				 */
				ret = 0;
				continue;
			}
			if (unlikely(ret))
				break;
		}

		spin_unlock(&lock->wait_lock);

		debug_rt_mutex_print_deadlock(waiter);

		if (waiter->task)
			schedule_rt_mutex(lock);

		spin_lock(&lock->wait_lock);
		set_current_state(state);
	}

	return ret;
}

/*
 * Slow path lock function:
 */
static int __sched
rt_mutex_slowlock(struct rt_mutex *lock, int state,
		  struct hrtimer_sleeper *timeout,
		  int detect_deadlock)
{
	struct rt_mutex_waiter waiter;
	int ret = 0;

	debug_rt_mutex_init_waiter(&waiter);
	waiter.task = NULL;

	spin_lock(&lock->wait_lock);

	/* Try to acquire the lock again: */
	if (try_to_take_rt_mutex(lock)) {
		spin_unlock(&lock->wait_lock);
		return 0;
	}

	set_current_state(state);

	/* Setup the timer, when timeout != NULL */
	if (unlikely(timeout)) {
		hrtimer_start_expires(&timeout->timer, HRTIMER_MODE_ABS);
		if (!hrtimer_active(&timeout->timer))
			timeout->task = NULL;
	}

	ret = __rt_mutex_slowlock(lock, state, timeout, &waiter,
				  detect_deadlock);

	set_current_state(TASK_RUNNING);

	if (unlikely(waiter.task))
		remove_waiter(lock, &waiter);

	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	spin_unlock(&lock->wait_lock);

	/* Remove pending timer: */
	if (unlikely(timeout))
		hrtimer_cancel(&timeout->timer);

	/*
	 * Readjust priority, when we did not get the lock. We might
	 * have been the pending owner and boosted. Since we did not
	 * take the lock, the PI boost has to go.
	 */
	if (unlikely(ret))
		rt_mutex_adjust_prio(current);

	debug_rt_mutex_free_waiter(&waiter);

	return ret;
}

/*
 * Slow path try-lock function:
 */
static inline int
rt_mutex_slowtrylock(struct rt_mutex *lock)
{
	int ret = 0;

	spin_lock(&lock->wait_lock);

	if (likely(rt_mutex_owner(lock) != current)) {

		ret = try_to_take_rt_mutex(lock);
		/*
		 * try_to_take_rt_mutex() sets the lock waiters
		 * bit unconditionally. Clean this up.
		 */
		fixup_rt_mutex_waiters(lock);
	}

	spin_unlock(&lock->wait_lock);

	return ret;
}

/*
 * Slow path to release a rt-mutex:
 */
static void __sched
rt_mutex_slowunlock(struct rt_mutex *lock)
{
	spin_lock(&lock->wait_lock);

	debug_rt_mutex_unlock(lock);

	rt_mutex_deadlock_account_unlock(current);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		spin_unlock(&lock->wait_lock);
		return;
	}

	wakeup_next_waiter(lock);

	spin_unlock(&lock->wait_lock);

	/* Undo pi boosting if necessary: */
	rt_mutex_adjust_prio(current);
}

/*
 * debug aware fast / slowpath lock,trylock,unlock
 *
 * The atomic acquire/release ops are compiled away, when either the
 * architecture does not support cmpxchg or when debugging is enabled.
 */
static inline int
rt_mutex_fastlock(struct rt_mutex *lock, int state,
		  int detect_deadlock,
		  int (*slowfn)(struct rt_mutex *lock, int state,
				struct hrtimer_sleeper *timeout,
				int detect_deadlock))
{
	if (!detect_deadlock && likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 0;
	} else
		return slowfn(lock, state, NULL, detect_deadlock);
}

static inline int
rt_mutex_timed_fastlock(struct rt_mutex *lock, int state,
			struct hrtimer_sleeper *timeout, int detect_deadlock,
			int (*slowfn)(struct rt_mutex *lock, int state,
				      struct hrtimer_sleeper *timeout,
				      int detect_deadlock))
{
	if (!detect_deadlock && likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 0;
	} else
		return slowfn(lock, state, timeout, detect_deadlock);
}

static inline int
rt_mutex_fasttrylock(struct rt_mutex *lock,
		     int (*slowfn)(struct rt_mutex *lock))
{
	if (likely(rt_mutex_cmpxchg(lock, NULL, current))) {
		rt_mutex_deadlock_account_lock(lock, current);
		return 1;
	}
	return slowfn(lock);
}

static inline void
rt_mutex_fastunlock(struct rt_mutex *lock,
		    void (*slowfn)(struct rt_mutex *lock))
{
	if (likely(rt_mutex_cmpxchg(lock, current, NULL)))
		rt_mutex_deadlock_account_unlock(current);
	else
		slowfn(lock);
}

/**
 * rt_mutex_lock - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 */
void __sched rt_mutex_lock(struct rt_mutex *lock)
{
	might_sleep();

	rt_mutex_fastlock(lock, TASK_UNINTERRUPTIBLE, 0, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock);

/**
 * rt_mutex_lock_interruptible - lock a rt_mutex interruptible
 *
 * @lock: 		the rt_mutex to be locked
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int __sched rt_mutex_lock_interruptible(struct rt_mutex *lock,
						 int detect_deadlock)
{
	might_sleep();

	return rt_mutex_fastlock(lock, TASK_INTERRUPTIBLE,
				 detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_interruptible);

/**
 * rt_mutex_timed_lock - lock a rt_mutex interruptible
 *			the timeout structure is provided
 *			by the caller
 *
 * @lock: 		the rt_mutex to be locked
 * @timeout:		timeout structure or NULL (no timeout)
 * @detect_deadlock:	deadlock detection on/off
 *
 * Returns:
 *  0 		on success
 * -EINTR 	when interrupted by a signal
 * -ETIMEOUT	when the timeout expired
 * -EDEADLK	when the lock would deadlock (when deadlock detection is on)
 */
int
rt_mutex_timed_lock(struct rt_mutex *lock, struct hrtimer_sleeper *timeout,
		    int detect_deadlock)
{
	might_sleep();

	return rt_mutex_timed_fastlock(lock, TASK_INTERRUPTIBLE, timeout,
				       detect_deadlock, rt_mutex_slowlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_timed_lock);

/**
 * rt_mutex_trylock - try to lock a rt_mutex
 *
 * @lock:	the rt_mutex to be locked
 *
 * Returns 1 on success and 0 on contention
 */
int __sched rt_mutex_trylock(struct rt_mutex *lock)
{
	return rt_mutex_fasttrylock(lock, rt_mutex_slowtrylock);
}
EXPORT_SYMBOL_GPL(rt_mutex_trylock);

/**
 * rt_mutex_unlock - unlock a rt_mutex
 *
 * @lock: the rt_mutex to be unlocked
 */
void __sched rt_mutex_unlock(struct rt_mutex *lock)
{
	rt_mutex_fastunlock(lock, rt_mutex_slowunlock);
}
EXPORT_SYMBOL_GPL(rt_mutex_unlock);

/**
 * rt_mutex_destroy - mark a mutex unusable
 * @lock: the mutex to be destroyed
 *
 * This function marks the mutex uninitialized, and any subsequent
 * use of the mutex is forbidden. The mutex must not be locked when
 * this function is called.
 */
void rt_mutex_destroy(struct rt_mutex *lock)
{
	WARN_ON(rt_mutex_is_locked(lock));
#ifdef CONFIG_DEBUG_RT_MUTEXES
	lock->magic = NULL;
#endif
}

EXPORT_SYMBOL_GPL(rt_mutex_destroy);

/**
 * __rt_mutex_init - initialize the rt lock
 *
 * @lock: the rt lock to be initialized
 *
 * Initialize the rt lock to unlocked state.
 *
 * Initializing of a locked rt lock is not allowed
 */
void __rt_mutex_init(struct rt_mutex *lock, const char *name)
{
	lock->owner = NULL;
	spin_lock_init(&lock->wait_lock);
	plist_head_init(&lock->wait_list, &lock->wait_lock);

	debug_rt_mutex_init(lock, name);
}
EXPORT_SYMBOL_GPL(__rt_mutex_init);

/**
 * rt_mutex_init_proxy_locked - initialize and lock a rt_mutex on behalf of a
 *				proxy owner
 *
 * @lock: 	the rt_mutex to be locked
 * @proxy_owner:the task to set as owner
 *
 * No locking. Caller has to do serializing itself
 * Special API call for PI-futex support
 */
void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
				struct task_struct *proxy_owner)
{
	__rt_mutex_init(lock, NULL);
	debug_rt_mutex_proxy_lock(lock, proxy_owner);
	rt_mutex_set_owner(lock, proxy_owner, 0);
	rt_mutex_deadlock_account_lock(lock, proxy_owner);
}

/**
 * rt_mutex_proxy_unlock - release a lock on behalf of owner
 *
 * @lock: 	the rt_mutex to be locked
 *
 * No locking. Caller has to do serializing itself
 * Special API call for PI-futex support
 */
void rt_mutex_proxy_unlock(struct rt_mutex *lock,
			   struct task_struct *proxy_owner)
{
	debug_rt_mutex_proxy_unlock(lock);
	rt_mutex_set_owner(lock, NULL, 0);
	rt_mutex_deadlock_account_unlock(proxy_owner);
}

/**
 * rt_mutex_start_proxy_lock() - Start lock acquisition for another task
 * @lock:		the rt_mutex to take
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @task:		the task to prepare
 * @detect_deadlock:	perform deadlock detection (1) or not (0)
 *
 * Returns:
 *  0 - task blocked on lock
 *  1 - acquired the lock for task, caller should wake it up
 * <0 - error
 *
 * Special API call for FUTEX_REQUEUE_PI support.
 */
int rt_mutex_start_proxy_lock(struct rt_mutex *lock,
			      struct rt_mutex_waiter *waiter,
			      struct task_struct *task, int detect_deadlock)
{
	int ret;

	spin_lock(&lock->wait_lock);

	mark_rt_mutex_waiters(lock);

	if (!rt_mutex_owner(lock) || try_to_steal_lock(lock, task)) {
		/* We got the lock for task. */
		debug_rt_mutex_lock(lock);

		rt_mutex_set_owner(lock, task, 0);

		rt_mutex_deadlock_account_lock(lock, task);
		return 1;
	}

	ret = task_blocks_on_rt_mutex(lock, waiter, task, detect_deadlock);


	if (ret && !waiter->task) {
		/*
		 * Reset the return value. We might have
		 * returned with -EDEADLK and the owner
		 * released the lock while we were walking the
		 * pi chain.  Let the waiter sort it out.
		 */
		ret = 0;
	}
	spin_unlock(&lock->wait_lock);

	debug_rt_mutex_print_deadlock(waiter);

	return ret;
}

/**
 * rt_mutex_next_owner - return the next owner of the lock
 *
 * @lock: the rt lock query
 *
 * Returns the next owner of the lock or NULL
 *
 * Caller has to serialize against other accessors to the lock
 * itself.
 *
 * Special API call for PI-futex support
 */
struct task_struct *rt_mutex_next_owner(struct rt_mutex *lock)
{
	if (!rt_mutex_has_waiters(lock))
		return NULL;

	return rt_mutex_top_waiter(lock)->task;
}

/**
 * rt_mutex_finish_proxy_lock() - Complete lock acquisition
 * @lock:		the rt_mutex we were woken on
 * @to:			the timeout, null if none. hrtimer should already have
 * 			been started.
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @detect_deadlock:	perform deadlock detection (1) or not (0)
 *
 * Complete the lock acquisition started our behalf by another thread.
 *
 * Returns:
 *  0 - success
 * <0 - error, one of -EINTR, -ETIMEDOUT, or -EDEADLK
 *
 * Special API call for PI-futex requeue support
 */
int rt_mutex_finish_proxy_lock(struct rt_mutex *lock,
			       struct hrtimer_sleeper *to,
			       struct rt_mutex_waiter *waiter,
			       int detect_deadlock)
{
	int ret;

	spin_lock(&lock->wait_lock);

	set_current_state(TASK_INTERRUPTIBLE);

	ret = __rt_mutex_slowlock(lock, TASK_INTERRUPTIBLE, to, waiter,
				  detect_deadlock);

	set_current_state(TASK_RUNNING);

	if (unlikely(waiter->task))
		remove_waiter(lock, waiter);

	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally. We might
	 * have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	spin_unlock(&lock->wait_lock);

	/*
	 * Readjust priority, when we did not get the lock. We might have been
	 * the pending owner and boosted. Since we did not take the lock, the
	 * PI boost has to go.
	 */
	if (unlikely(ret))
		rt_mutex_adjust_prio(current);

	return ret;
}
