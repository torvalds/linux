// SPDX-License-Identifier: GPL-2.0-only
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
 *  See Documentation/locking/rt-mutex-design.rst for details.
 */
#include <linux/spinlock.h>
#include <linux/export.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/deadline.h>
#include <linux/sched/wake_q.h>
#include <linux/sched/debug.h>
#include <linux/timer.h>
#include <trace/hooks/dtask.h>

#include "rtmutex_common.h"

/*
 * lock->owner state tracking:
 *
 * lock->owner holds the task_struct pointer of the owner. Bit 0
 * is used to keep track of the "lock has waiters" state.
 *
 * owner	bit0
 * NULL		0	lock is free (fast acquire possible)
 * NULL		1	lock is free and has waiters and the top waiter
 *				is going to take the lock*
 * taskpointer	0	lock is held (fast release possible)
 * taskpointer	1	lock is held and has waiters**
 *
 * The fast atomic compare exchange based acquire and release is only
 * possible when bit 0 of lock->owner is 0.
 *
 * (*) It also can be a transitional state when grabbing the lock
 * with ->wait_lock is held. To prevent any fast path cmpxchg to the lock,
 * we need to set the bit0 before looking at the lock, and the owner may be
 * NULL in this small time, hence this can be a transitional state.
 *
 * (**) There is a small time when bit 0 is set but there are no
 * waiters. This can happen when grabbing the lock in the slow path.
 * To prevent a cmpxchg of the owner releasing the lock, we need to
 * set this bit before looking at the lock.
 */

static __always_inline void
rt_mutex_set_owner(struct rt_mutex *lock, struct task_struct *owner)
{
	unsigned long val = (unsigned long)owner;

	if (rt_mutex_has_waiters(lock))
		val |= RT_MUTEX_HAS_WAITERS;

	WRITE_ONCE(lock->owner, (struct task_struct *)val);
}

static __always_inline void clear_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner & ~RT_MUTEX_HAS_WAITERS);
}

static __always_inline void fixup_rt_mutex_waiters(struct rt_mutex *lock)
{
	unsigned long owner, *p = (unsigned long *) &lock->owner;

	if (rt_mutex_has_waiters(lock))
		return;

	/*
	 * The rbtree has no waiters enqueued, now make sure that the
	 * lock->owner still has the waiters bit set, otherwise the
	 * following can happen:
	 *
	 * CPU 0	CPU 1		CPU2
	 * l->owner=T1
	 *		rt_mutex_lock(l)
	 *		lock(l->lock)
	 *		l->owner = T1 | HAS_WAITERS;
	 *		enqueue(T2)
	 *		boost()
	 *		  unlock(l->lock)
	 *		block()
	 *
	 *				rt_mutex_lock(l)
	 *				lock(l->lock)
	 *				l->owner = T1 | HAS_WAITERS;
	 *				enqueue(T3)
	 *				boost()
	 *				  unlock(l->lock)
	 *				block()
	 *		signal(->T2)	signal(->T3)
	 *		lock(l->lock)
	 *		dequeue(T2)
	 *		deboost()
	 *		  unlock(l->lock)
	 *				lock(l->lock)
	 *				dequeue(T3)
	 *				 ==> wait list is empty
	 *				deboost()
	 *				 unlock(l->lock)
	 *		lock(l->lock)
	 *		fixup_rt_mutex_waiters()
	 *		  if (wait_list_empty(l) {
	 *		    l->owner = owner
	 *		    owner = l->owner & ~HAS_WAITERS;
	 *		      ==> l->owner = T1
	 *		  }
	 *				lock(l->lock)
	 * rt_mutex_unlock(l)		fixup_rt_mutex_waiters()
	 *				  if (wait_list_empty(l) {
	 *				    owner = l->owner & ~HAS_WAITERS;
	 * cmpxchg(l->owner, T1, NULL)
	 *  ===> Success (l->owner = NULL)
	 *
	 *				    l->owner = owner
	 *				      ==> l->owner = T1
	 *				  }
	 *
	 * With the check for the waiter bit in place T3 on CPU2 will not
	 * overwrite. All tasks fiddling with the waiters bit are
	 * serialized by l->lock, so nothing else can modify the waiters
	 * bit. If the bit is set then nothing can change l->owner either
	 * so the simple RMW is safe. The cmpxchg() will simply fail if it
	 * happens in the middle of the RMW because the waiters bit is
	 * still set.
	 */
	owner = READ_ONCE(*p);
	if (owner & RT_MUTEX_HAS_WAITERS)
		WRITE_ONCE(*p, owner & ~RT_MUTEX_HAS_WAITERS);
}

/*
 * We can speed up the acquire/release, if there's no debugging state to be
 * set up.
 */
#ifndef CONFIG_DEBUG_RT_MUTEXES
# define rt_mutex_cmpxchg_acquire(l,c,n) (cmpxchg_acquire(&l->owner, c, n) == c)
# define rt_mutex_cmpxchg_release(l,c,n) (cmpxchg_release(&l->owner, c, n) == c)

/*
 * Callers must hold the ->wait_lock -- which is the whole purpose as we force
 * all future threads that attempt to [Rmw] the lock to the slowpath. As such
 * relaxed semantics suffice.
 */
static __always_inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	unsigned long owner, *p = (unsigned long *) &lock->owner;

	do {
		owner = *p;
	} while (cmpxchg_relaxed(p, owner,
				 owner | RT_MUTEX_HAS_WAITERS) != owner);
}

/*
 * Safe fastpath aware unlock:
 * 1) Clear the waiters bit
 * 2) Drop lock->wait_lock
 * 3) Try to unlock the lock with cmpxchg
 */
static __always_inline bool unlock_rt_mutex_safe(struct rt_mutex *lock,
						 unsigned long flags)
	__releases(lock->wait_lock)
{
	struct task_struct *owner = rt_mutex_owner(lock);

	clear_rt_mutex_waiters(lock);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
	/*
	 * If a new waiter comes in between the unlock and the cmpxchg
	 * we have two situations:
	 *
	 * unlock(wait_lock);
	 *					lock(wait_lock);
	 * cmpxchg(p, owner, 0) == owner
	 *					mark_rt_mutex_waiters(lock);
	 *					acquire(lock);
	 * or:
	 *
	 * unlock(wait_lock);
	 *					lock(wait_lock);
	 *					mark_rt_mutex_waiters(lock);
	 *
	 * cmpxchg(p, owner, 0) != owner
	 *					enqueue_waiter();
	 *					unlock(wait_lock);
	 * lock(wait_lock);
	 * wake waiter();
	 * unlock(wait_lock);
	 *					lock(wait_lock);
	 *					acquire(lock);
	 */
	return rt_mutex_cmpxchg_release(lock, owner, NULL);
}

#else
# define rt_mutex_cmpxchg_acquire(l,c,n)	(0)
# define rt_mutex_cmpxchg_release(l,c,n)	(0)

static __always_inline void mark_rt_mutex_waiters(struct rt_mutex *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner | RT_MUTEX_HAS_WAITERS);
}

/*
 * Simple slow path only version: lock->owner is protected by lock->wait_lock.
 */
static __always_inline bool unlock_rt_mutex_safe(struct rt_mutex *lock,
						 unsigned long flags)
	__releases(lock->wait_lock)
{
	lock->owner = NULL;
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
	return true;
}
#endif

/*
 * Only use with rt_mutex_waiter_{less,equal}()
 */
#define task_to_waiter(p)	\
	&(struct rt_mutex_waiter){ .prio = (p)->prio, .deadline = (p)->dl.deadline }

static __always_inline int rt_mutex_waiter_less(struct rt_mutex_waiter *left,
						struct rt_mutex_waiter *right)
{
	if (left->prio < right->prio)
		return 1;

	/*
	 * If both waiters have dl_prio(), we check the deadlines of the
	 * associated tasks.
	 * If left waiter has a dl_prio(), and we didn't return 1 above,
	 * then right waiter has a dl_prio() too.
	 */
	if (dl_prio(left->prio))
		return dl_time_before(left->deadline, right->deadline);

	return 0;
}

static __always_inline int rt_mutex_waiter_equal(struct rt_mutex_waiter *left,
						 struct rt_mutex_waiter *right)
{
	if (left->prio != right->prio)
		return 0;

	/*
	 * If both waiters have dl_prio(), we check the deadlines of the
	 * associated tasks.
	 * If left waiter has a dl_prio(), and we didn't return 0 above,
	 * then right waiter has a dl_prio() too.
	 */
	if (dl_prio(left->prio))
		return left->deadline == right->deadline;

	return 1;
}

#define __node_2_waiter(node) \
	rb_entry((node), struct rt_mutex_waiter, tree_entry)

static __always_inline bool __waiter_less(struct rb_node *a, const struct rb_node *b)
{
	return rt_mutex_waiter_less(__node_2_waiter(a), __node_2_waiter(b));
}

static __always_inline void
rt_mutex_enqueue(struct rt_mutex *lock, struct rt_mutex_waiter *waiter)
{
	rb_add_cached(&waiter->tree_entry, &lock->waiters, __waiter_less);
}

static __always_inline void
rt_mutex_dequeue(struct rt_mutex *lock, struct rt_mutex_waiter *waiter)
{
	if (RB_EMPTY_NODE(&waiter->tree_entry))
		return;

	rb_erase_cached(&waiter->tree_entry, &lock->waiters);
	RB_CLEAR_NODE(&waiter->tree_entry);
}

#define __node_2_pi_waiter(node) \
	rb_entry((node), struct rt_mutex_waiter, pi_tree_entry)

static __always_inline bool
__pi_waiter_less(struct rb_node *a, const struct rb_node *b)
{
	return rt_mutex_waiter_less(__node_2_pi_waiter(a), __node_2_pi_waiter(b));
}

static __always_inline void
rt_mutex_enqueue_pi(struct task_struct *task, struct rt_mutex_waiter *waiter)
{
	rb_add_cached(&waiter->pi_tree_entry, &task->pi_waiters, __pi_waiter_less);
}

static __always_inline void
rt_mutex_dequeue_pi(struct task_struct *task, struct rt_mutex_waiter *waiter)
{
	if (RB_EMPTY_NODE(&waiter->pi_tree_entry))
		return;

	rb_erase_cached(&waiter->pi_tree_entry, &task->pi_waiters);
	RB_CLEAR_NODE(&waiter->pi_tree_entry);
}

static __always_inline void rt_mutex_adjust_prio(struct task_struct *p)
{
	struct task_struct *pi_task = NULL;

	lockdep_assert_held(&p->pi_lock);

	if (task_has_pi_waiters(p))
		pi_task = task_top_pi_waiter(p)->task;

	rt_mutex_setprio(p, pi_task);
}

/*
 * Deadlock detection is conditional:
 *
 * If CONFIG_DEBUG_RT_MUTEXES=n, deadlock detection is only conducted
 * if the detect argument is == RT_MUTEX_FULL_CHAINWALK.
 *
 * If CONFIG_DEBUG_RT_MUTEXES=y, deadlock detection is always
 * conducted independent of the detect argument.
 *
 * If the waiter argument is NULL this indicates the deboost path and
 * deadlock detection is disabled independent of the detect argument
 * and the config settings.
 */
static __always_inline bool
rt_mutex_cond_detect_deadlock(struct rt_mutex_waiter *waiter,
			      enum rtmutex_chainwalk chwalk)
{
	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES))
		return waiter != NULL;
	return chwalk == RT_MUTEX_FULL_CHAINWALK;
}

/*
 * Max number of times we'll walk the boosting chain:
 */
int max_lock_depth = 1024;

static __always_inline struct rt_mutex *task_blocked_on_lock(struct task_struct *p)
{
	return p->pi_blocked_on ? p->pi_blocked_on->lock : NULL;
}

/*
 * Adjust the priority chain. Also used for deadlock detection.
 * Decreases task's usage by one - may thus free the task.
 *
 * @task:	the task owning the mutex (owner) for which a chain walk is
 *		probably needed
 * @chwalk:	do we have to carry out deadlock detection?
 * @orig_lock:	the mutex (can be NULL if we are walking the chain to recheck
 *		things for a task that has just got its priority adjusted, and
 *		is waiting on a mutex)
 * @next_lock:	the mutex on which the owner of @orig_lock was blocked before
 *		we dropped its pi_lock. Is never dereferenced, only used for
 *		comparison to detect lock chain changes.
 * @orig_waiter: rt_mutex_waiter struct for the task that has just donated
 *		its priority to the mutex owner (can be NULL in the case
 *		depicted above or if the top waiter is gone away and we are
 *		actually deboosting the owner)
 * @top_task:	the current top waiter
 *
 * Returns 0 or -EDEADLK.
 *
 * Chain walk basics and protection scope
 *
 * [R] refcount on task
 * [P] task->pi_lock held
 * [L] rtmutex->wait_lock held
 *
 * Step	Description				Protected by
 *	function arguments:
 *	@task					[R]
 *	@orig_lock if != NULL			@top_task is blocked on it
 *	@next_lock				Unprotected. Cannot be
 *						dereferenced. Only used for
 *						comparison.
 *	@orig_waiter if != NULL			@top_task is blocked on it
 *	@top_task				current, or in case of proxy
 *						locking protected by calling
 *						code
 *	again:
 *	  loop_sanity_check();
 *	retry:
 * [1]	  lock(task->pi_lock);			[R] acquire [P]
 * [2]	  waiter = task->pi_blocked_on;		[P]
 * [3]	  check_exit_conditions_1();		[P]
 * [4]	  lock = waiter->lock;			[P]
 * [5]	  if (!try_lock(lock->wait_lock)) {	[P] try to acquire [L]
 *	    unlock(task->pi_lock);		release [P]
 *	    goto retry;
 *	  }
 * [6]	  check_exit_conditions_2();		[P] + [L]
 * [7]	  requeue_lock_waiter(lock, waiter);	[P] + [L]
 * [8]	  unlock(task->pi_lock);		release [P]
 *	  put_task_struct(task);		release [R]
 * [9]	  check_exit_conditions_3();		[L]
 * [10]	  task = owner(lock);			[L]
 *	  get_task_struct(task);		[L] acquire [R]
 *	  lock(task->pi_lock);			[L] acquire [P]
 * [11]	  requeue_pi_waiter(tsk, waiters(lock));[P] + [L]
 * [12]	  check_exit_conditions_4();		[P] + [L]
 * [13]	  unlock(task->pi_lock);		release [P]
 *	  unlock(lock->wait_lock);		release [L]
 *	  goto again;
 */
static int __sched rt_mutex_adjust_prio_chain(struct task_struct *task,
					      enum rtmutex_chainwalk chwalk,
					      struct rt_mutex *orig_lock,
					      struct rt_mutex *next_lock,
					      struct rt_mutex_waiter *orig_waiter,
					      struct task_struct *top_task)
{
	struct rt_mutex_waiter *waiter, *top_waiter = orig_waiter;
	struct rt_mutex_waiter *prerequeue_top_waiter;
	int ret = 0, depth = 0;
	struct rt_mutex *lock;
	bool detect_deadlock;
	bool requeue = true;

	detect_deadlock = rt_mutex_cond_detect_deadlock(orig_waiter, chwalk);

	/*
	 * The (de)boosting is a step by step approach with a lot of
	 * pitfalls. We want this to be preemptible and we want hold a
	 * maximum of two locks per step. So we have to check
	 * carefully whether things change under us.
	 */
 again:
	/*
	 * We limit the lock chain length for each invocation.
	 */
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

		return -EDEADLK;
	}

	/*
	 * We are fully preemptible here and only hold the refcount on
	 * @task. So everything can have changed under us since the
	 * caller or our own code below (goto retry/again) dropped all
	 * locks.
	 */
 retry:
	/*
	 * [1] Task cannot go away as we did a get_task() before !
	 */
	raw_spin_lock_irq(&task->pi_lock);

	/*
	 * [2] Get the waiter on which @task is blocked on.
	 */
	waiter = task->pi_blocked_on;

	/*
	 * [3] check_exit_conditions_1() protected by task->pi_lock.
	 */

	/*
	 * Check whether the end of the boosting chain has been
	 * reached or the state of the chain has changed while we
	 * dropped the locks.
	 */
	if (!waiter)
		goto out_unlock_pi;

	/*
	 * Check the orig_waiter state. After we dropped the locks,
	 * the previous owner of the lock might have released the lock.
	 */
	if (orig_waiter && !rt_mutex_owner(orig_lock))
		goto out_unlock_pi;

	/*
	 * We dropped all locks after taking a refcount on @task, so
	 * the task might have moved on in the lock chain or even left
	 * the chain completely and blocks now on an unrelated lock or
	 * on @orig_lock.
	 *
	 * We stored the lock on which @task was blocked in @next_lock,
	 * so we can detect the chain change.
	 */
	if (next_lock != waiter->lock)
		goto out_unlock_pi;

	/*
	 * Drop out, when the task has no waiters. Note,
	 * top_waiter can be NULL, when we are in the deboosting
	 * mode!
	 */
	if (top_waiter) {
		if (!task_has_pi_waiters(task))
			goto out_unlock_pi;
		/*
		 * If deadlock detection is off, we stop here if we
		 * are not the top pi waiter of the task. If deadlock
		 * detection is enabled we continue, but stop the
		 * requeueing in the chain walk.
		 */
		if (top_waiter != task_top_pi_waiter(task)) {
			if (!detect_deadlock)
				goto out_unlock_pi;
			else
				requeue = false;
		}
	}

	/*
	 * If the waiter priority is the same as the task priority
	 * then there is no further priority adjustment necessary.  If
	 * deadlock detection is off, we stop the chain walk. If its
	 * enabled we continue, but stop the requeueing in the chain
	 * walk.
	 */
	if (rt_mutex_waiter_equal(waiter, task_to_waiter(task))) {
		if (!detect_deadlock)
			goto out_unlock_pi;
		else
			requeue = false;
	}

	/*
	 * [4] Get the next lock
	 */
	lock = waiter->lock;
	/*
	 * [5] We need to trylock here as we are holding task->pi_lock,
	 * which is the reverse lock order versus the other rtmutex
	 * operations.
	 */
	if (!raw_spin_trylock(&lock->wait_lock)) {
		raw_spin_unlock_irq(&task->pi_lock);
		cpu_relax();
		goto retry;
	}

	/*
	 * [6] check_exit_conditions_2() protected by task->pi_lock and
	 * lock->wait_lock.
	 *
	 * Deadlock detection. If the lock is the same as the original
	 * lock which caused us to walk the lock chain or if the
	 * current lock is owned by the task which initiated the chain
	 * walk, we detected a deadlock.
	 */
	if (lock == orig_lock || rt_mutex_owner(lock) == top_task) {
		raw_spin_unlock(&lock->wait_lock);
		ret = -EDEADLK;
		goto out_unlock_pi;
	}

	/*
	 * If we just follow the lock chain for deadlock detection, no
	 * need to do all the requeue operations. To avoid a truckload
	 * of conditionals around the various places below, just do the
	 * minimum chain walk checks.
	 */
	if (!requeue) {
		/*
		 * No requeue[7] here. Just release @task [8]
		 */
		raw_spin_unlock(&task->pi_lock);
		put_task_struct(task);

		/*
		 * [9] check_exit_conditions_3 protected by lock->wait_lock.
		 * If there is no owner of the lock, end of chain.
		 */
		if (!rt_mutex_owner(lock)) {
			raw_spin_unlock_irq(&lock->wait_lock);
			return 0;
		}

		/* [10] Grab the next task, i.e. owner of @lock */
		task = get_task_struct(rt_mutex_owner(lock));
		raw_spin_lock(&task->pi_lock);

		/*
		 * No requeue [11] here. We just do deadlock detection.
		 *
		 * [12] Store whether owner is blocked
		 * itself. Decision is made after dropping the locks
		 */
		next_lock = task_blocked_on_lock(task);
		/*
		 * Get the top waiter for the next iteration
		 */
		top_waiter = rt_mutex_top_waiter(lock);

		/* [13] Drop locks */
		raw_spin_unlock(&task->pi_lock);
		raw_spin_unlock_irq(&lock->wait_lock);

		/* If owner is not blocked, end of chain. */
		if (!next_lock)
			goto out_put_task;
		goto again;
	}

	/*
	 * Store the current top waiter before doing the requeue
	 * operation on @lock. We need it for the boost/deboost
	 * decision below.
	 */
	prerequeue_top_waiter = rt_mutex_top_waiter(lock);

	/* [7] Requeue the waiter in the lock waiter tree. */
	rt_mutex_dequeue(lock, waiter);

	/*
	 * Update the waiter prio fields now that we're dequeued.
	 *
	 * These values can have changed through either:
	 *
	 *   sys_sched_set_scheduler() / sys_sched_setattr()
	 *
	 * or
	 *
	 *   DL CBS enforcement advancing the effective deadline.
	 *
	 * Even though pi_waiters also uses these fields, and that tree is only
	 * updated in [11], we can do this here, since we hold [L], which
	 * serializes all pi_waiters access and rb_erase() does not care about
	 * the values of the node being removed.
	 */
	waiter->prio = task->prio;
	waiter->deadline = task->dl.deadline;

	rt_mutex_enqueue(lock, waiter);

	/* [8] Release the task */
	raw_spin_unlock(&task->pi_lock);
	put_task_struct(task);

	/*
	 * [9] check_exit_conditions_3 protected by lock->wait_lock.
	 *
	 * We must abort the chain walk if there is no lock owner even
	 * in the dead lock detection case, as we have nothing to
	 * follow here. This is the end of the chain we are walking.
	 */
	if (!rt_mutex_owner(lock)) {
		/*
		 * If the requeue [7] above changed the top waiter,
		 * then we need to wake the new top waiter up to try
		 * to get the lock.
		 */
		if (prerequeue_top_waiter != rt_mutex_top_waiter(lock))
			wake_up_process(rt_mutex_top_waiter(lock)->task);
		raw_spin_unlock_irq(&lock->wait_lock);
		return 0;
	}

	/* [10] Grab the next task, i.e. the owner of @lock */
	task = get_task_struct(rt_mutex_owner(lock));
	raw_spin_lock(&task->pi_lock);

	/* [11] requeue the pi waiters if necessary */
	if (waiter == rt_mutex_top_waiter(lock)) {
		/*
		 * The waiter became the new top (highest priority)
		 * waiter on the lock. Replace the previous top waiter
		 * in the owner tasks pi waiters tree with this waiter
		 * and adjust the priority of the owner.
		 */
		rt_mutex_dequeue_pi(task, prerequeue_top_waiter);
		rt_mutex_enqueue_pi(task, waiter);
		rt_mutex_adjust_prio(task);

	} else if (prerequeue_top_waiter == waiter) {
		/*
		 * The waiter was the top waiter on the lock, but is
		 * no longer the top priority waiter. Replace waiter in
		 * the owner tasks pi waiters tree with the new top
		 * (highest priority) waiter and adjust the priority
		 * of the owner.
		 * The new top waiter is stored in @waiter so that
		 * @waiter == @top_waiter evaluates to true below and
		 * we continue to deboost the rest of the chain.
		 */
		rt_mutex_dequeue_pi(task, waiter);
		waiter = rt_mutex_top_waiter(lock);
		rt_mutex_enqueue_pi(task, waiter);
		rt_mutex_adjust_prio(task);
	} else {
		/*
		 * Nothing changed. No need to do any priority
		 * adjustment.
		 */
	}

	/*
	 * [12] check_exit_conditions_4() protected by task->pi_lock
	 * and lock->wait_lock. The actual decisions are made after we
	 * dropped the locks.
	 *
	 * Check whether the task which owns the current lock is pi
	 * blocked itself. If yes we store a pointer to the lock for
	 * the lock chain change detection above. After we dropped
	 * task->pi_lock next_lock cannot be dereferenced anymore.
	 */
	next_lock = task_blocked_on_lock(task);
	/*
	 * Store the top waiter of @lock for the end of chain walk
	 * decision below.
	 */
	top_waiter = rt_mutex_top_waiter(lock);

	/* [13] Drop the locks */
	raw_spin_unlock(&task->pi_lock);
	raw_spin_unlock_irq(&lock->wait_lock);

	/*
	 * Make the actual exit decisions [12], based on the stored
	 * values.
	 *
	 * We reached the end of the lock chain. Stop right here. No
	 * point to go back just to figure that out.
	 */
	if (!next_lock)
		goto out_put_task;

	/*
	 * If the current waiter is not the top waiter on the lock,
	 * then we can stop the chain walk here if we are not in full
	 * deadlock detection mode.
	 */
	if (!detect_deadlock && waiter != top_waiter)
		goto out_put_task;

	goto again;

 out_unlock_pi:
	raw_spin_unlock_irq(&task->pi_lock);
 out_put_task:
	put_task_struct(task);

	return ret;
}

/*
 * Try to take an rt-mutex
 *
 * Must be called with lock->wait_lock held and interrupts disabled
 *
 * @lock:   The lock to be acquired.
 * @task:   The task which wants to acquire the lock
 * @waiter: The waiter that is queued to the lock's wait tree if the
 *	    callsite called task_blocked_on_lock(), otherwise NULL
 */
static int __sched
try_to_take_rt_mutex(struct rt_mutex *lock, struct task_struct *task,
		     struct rt_mutex_waiter *waiter)
{
	lockdep_assert_held(&lock->wait_lock);

	/*
	 * Before testing whether we can acquire @lock, we set the
	 * RT_MUTEX_HAS_WAITERS bit in @lock->owner. This forces all
	 * other tasks which try to modify @lock into the slow path
	 * and they serialize on @lock->wait_lock.
	 *
	 * The RT_MUTEX_HAS_WAITERS bit can have a transitional state
	 * as explained at the top of this file if and only if:
	 *
	 * - There is a lock owner. The caller must fixup the
	 *   transient state if it does a trylock or leaves the lock
	 *   function due to a signal or timeout.
	 *
	 * - @task acquires the lock and there are no other
	 *   waiters. This is undone in rt_mutex_set_owner(@task) at
	 *   the end of this function.
	 */
	mark_rt_mutex_waiters(lock);

	/*
	 * If @lock has an owner, give up.
	 */
	if (rt_mutex_owner(lock))
		return 0;

	/*
	 * If @waiter != NULL, @task has already enqueued the waiter
	 * into @lock waiter tree. If @waiter == NULL then this is a
	 * trylock attempt.
	 */
	if (waiter) {
		/*
		 * If waiter is not the highest priority waiter of
		 * @lock, give up.
		 */
		if (waiter != rt_mutex_top_waiter(lock))
			return 0;

		/*
		 * We can acquire the lock. Remove the waiter from the
		 * lock waiters tree.
		 */
		rt_mutex_dequeue(lock, waiter);

	} else {
		/*
		 * If the lock has waiters already we check whether @task is
		 * eligible to take over the lock.
		 *
		 * If there are no other waiters, @task can acquire
		 * the lock.  @task->pi_blocked_on is NULL, so it does
		 * not need to be dequeued.
		 */
		if (rt_mutex_has_waiters(lock)) {
			/*
			 * If @task->prio is greater than or equal to
			 * the top waiter priority (kernel view),
			 * @task lost.
			 */
			if (!rt_mutex_waiter_less(task_to_waiter(task),
						  rt_mutex_top_waiter(lock)))
				return 0;

			/*
			 * The current top waiter stays enqueued. We
			 * don't have to change anything in the lock
			 * waiters order.
			 */
		} else {
			/*
			 * No waiters. Take the lock without the
			 * pi_lock dance.@task->pi_blocked_on is NULL
			 * and we have no waiters to enqueue in @task
			 * pi waiters tree.
			 */
			goto takeit;
		}
	}

	/*
	 * Clear @task->pi_blocked_on. Requires protection by
	 * @task->pi_lock. Redundant operation for the @waiter == NULL
	 * case, but conditionals are more expensive than a redundant
	 * store.
	 */
	raw_spin_lock(&task->pi_lock);
	task->pi_blocked_on = NULL;
	/*
	 * Finish the lock acquisition. @task is the new owner. If
	 * other waiters exist we have to insert the highest priority
	 * waiter into @task->pi_waiters tree.
	 */
	if (rt_mutex_has_waiters(lock))
		rt_mutex_enqueue_pi(task, rt_mutex_top_waiter(lock));
	raw_spin_unlock(&task->pi_lock);

takeit:
	/*
	 * This either preserves the RT_MUTEX_HAS_WAITERS bit if there
	 * are still waiters or clears it.
	 */
	rt_mutex_set_owner(lock, task);

	return 1;
}

/*
 * Task blocks on lock.
 *
 * Prepare waiter and propagate pi chain
 *
 * This must be called with lock->wait_lock held and interrupts disabled
 */
static int __sched task_blocks_on_rt_mutex(struct rt_mutex *lock,
					   struct rt_mutex_waiter *waiter,
					   struct task_struct *task,
					   enum rtmutex_chainwalk chwalk)
{
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *top_waiter = waiter;
	struct rt_mutex *next_lock;
	int chain_walk = 0, res;

	lockdep_assert_held(&lock->wait_lock);

	/*
	 * Early deadlock detection. We really don't want the task to
	 * enqueue on itself just to untangle the mess later. It's not
	 * only an optimization. We drop the locks, so another waiter
	 * can come in before the chain walk detects the deadlock. So
	 * the other will detect the deadlock and return -EDEADLOCK,
	 * which is wrong, as the other waiter is not in a deadlock
	 * situation.
	 */
	if (owner == task)
		return -EDEADLK;

	raw_spin_lock(&task->pi_lock);
	waiter->task = task;
	waiter->lock = lock;
	waiter->prio = task->prio;
	waiter->deadline = task->dl.deadline;

	/* Get the top priority waiter on the lock */
	if (rt_mutex_has_waiters(lock))
		top_waiter = rt_mutex_top_waiter(lock);
	rt_mutex_enqueue(lock, waiter);

	task->pi_blocked_on = waiter;

	raw_spin_unlock(&task->pi_lock);

	if (!owner)
		return 0;

	raw_spin_lock(&owner->pi_lock);
	if (waiter == rt_mutex_top_waiter(lock)) {
		rt_mutex_dequeue_pi(owner, top_waiter);
		rt_mutex_enqueue_pi(owner, waiter);

		rt_mutex_adjust_prio(owner);
		if (owner->pi_blocked_on)
			chain_walk = 1;
	} else if (rt_mutex_cond_detect_deadlock(waiter, chwalk)) {
		chain_walk = 1;
	}

	/* Store the lock on which owner is blocked or NULL */
	next_lock = task_blocked_on_lock(owner);

	raw_spin_unlock(&owner->pi_lock);
	/*
	 * Even if full deadlock detection is on, if the owner is not
	 * blocked itself, we can avoid finding this out in the chain
	 * walk.
	 */
	if (!chain_walk || !next_lock)
		return 0;

	/*
	 * The owner can't disappear while holding a lock,
	 * so the owner struct is protected by wait_lock.
	 * Gets dropped in rt_mutex_adjust_prio_chain()!
	 */
	get_task_struct(owner);

	raw_spin_unlock_irq(&lock->wait_lock);

	res = rt_mutex_adjust_prio_chain(owner, chwalk, lock,
					 next_lock, waiter, task);

	raw_spin_lock_irq(&lock->wait_lock);

	return res;
}

/*
 * Remove the top waiter from the current tasks pi waiter tree and
 * queue it up.
 *
 * Called with lock->wait_lock held and interrupts disabled.
 */
static void __sched mark_wakeup_next_waiter(struct wake_q_head *wake_q,
					    struct rt_mutex *lock)
{
	struct rt_mutex_waiter *waiter;

	raw_spin_lock(&current->pi_lock);

	waiter = rt_mutex_top_waiter(lock);

	/*
	 * Remove it from current->pi_waiters and deboost.
	 *
	 * We must in fact deboost here in order to ensure we call
	 * rt_mutex_setprio() to update p->pi_top_task before the
	 * task unblocks.
	 */
	rt_mutex_dequeue_pi(current, waiter);
	rt_mutex_adjust_prio(current);

	/*
	 * As we are waking up the top waiter, and the waiter stays
	 * queued on the lock until it gets the lock, this lock
	 * obviously has waiters. Just set the bit here and this has
	 * the added benefit of forcing all new tasks into the
	 * slow path making sure no task of lower priority than
	 * the top waiter can steal this lock.
	 */
	lock->owner = (void *) RT_MUTEX_HAS_WAITERS;

	/*
	 * We deboosted before waking the top waiter task such that we don't
	 * run two tasks with the 'same' priority (and ensure the
	 * p->pi_top_task pointer points to a blocked task). This however can
	 * lead to priority inversion if we would get preempted after the
	 * deboost but before waking our donor task, hence the preempt_disable()
	 * before unlock.
	 *
	 * Pairs with preempt_enable() in rt_mutex_postunlock();
	 */
	preempt_disable();
	wake_q_add(wake_q, waiter->task);
	raw_spin_unlock(&current->pi_lock);
}

/*
 * Remove a waiter from a lock and give up
 *
 * Must be called with lock->wait_lock held and interrupts disabled. I must
 * have just failed to try_to_take_rt_mutex().
 */
static void __sched remove_waiter(struct rt_mutex *lock,
				  struct rt_mutex_waiter *waiter)
{
	bool is_top_waiter = (waiter == rt_mutex_top_waiter(lock));
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex *next_lock;

	lockdep_assert_held(&lock->wait_lock);

	raw_spin_lock(&current->pi_lock);
	rt_mutex_dequeue(lock, waiter);
	current->pi_blocked_on = NULL;
	raw_spin_unlock(&current->pi_lock);

	/*
	 * Only update priority if the waiter was the highest priority
	 * waiter of the lock and there is an owner to update.
	 */
	if (!owner || !is_top_waiter)
		return;

	raw_spin_lock(&owner->pi_lock);

	rt_mutex_dequeue_pi(owner, waiter);

	if (rt_mutex_has_waiters(lock))
		rt_mutex_enqueue_pi(owner, rt_mutex_top_waiter(lock));

	rt_mutex_adjust_prio(owner);

	/* Store the lock on which owner is blocked or NULL */
	next_lock = task_blocked_on_lock(owner);

	raw_spin_unlock(&owner->pi_lock);

	/*
	 * Don't walk the chain, if the owner task is not blocked
	 * itself.
	 */
	if (!next_lock)
		return;

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(owner);

	raw_spin_unlock_irq(&lock->wait_lock);

	rt_mutex_adjust_prio_chain(owner, RT_MUTEX_MIN_CHAINWALK, lock,
				   next_lock, NULL, current);

	raw_spin_lock_irq(&lock->wait_lock);
}

/*
 * Recheck the pi chain, in case we got a priority setting
 *
 * Called from sched_setscheduler
 */
void __sched rt_mutex_adjust_pi(struct task_struct *task)
{
	struct rt_mutex_waiter *waiter;
	struct rt_mutex *next_lock;
	unsigned long flags;

	raw_spin_lock_irqsave(&task->pi_lock, flags);

	waiter = task->pi_blocked_on;
	if (!waiter || rt_mutex_waiter_equal(waiter, task_to_waiter(task))) {
		raw_spin_unlock_irqrestore(&task->pi_lock, flags);
		return;
	}
	next_lock = waiter->lock;
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	/* gets dropped in rt_mutex_adjust_prio_chain()! */
	get_task_struct(task);

	rt_mutex_adjust_prio_chain(task, RT_MUTEX_MIN_CHAINWALK, NULL,
				   next_lock, NULL, task);
}

void __sched rt_mutex_init_waiter(struct rt_mutex_waiter *waiter)
{
	debug_rt_mutex_init_waiter(waiter);
	RB_CLEAR_NODE(&waiter->pi_tree_entry);
	RB_CLEAR_NODE(&waiter->tree_entry);
	waiter->task = NULL;
}

/**
 * __rt_mutex_slowlock() - Perform the wait-wake-try-to-take loop
 * @lock:		 the rt_mutex to take
 * @state:		 the state the task should block in (TASK_INTERRUPTIBLE
 *			 or TASK_UNINTERRUPTIBLE)
 * @timeout:		 the pre-initialized and started timer, or NULL for none
 * @waiter:		 the pre-initialized rt_mutex_waiter
 *
 * Must be called with lock->wait_lock held and interrupts disabled
 */
static int __sched __rt_mutex_slowlock(struct rt_mutex *lock, unsigned int state,
				       struct hrtimer_sleeper *timeout,
				       struct rt_mutex_waiter *waiter)
{
	int ret = 0;

	trace_android_vh_rtmutex_wait_start(lock);
	for (;;) {
		/* Try to acquire the lock: */
		if (try_to_take_rt_mutex(lock, current, waiter))
			break;

		if (timeout && !timeout->task) {
			ret = -ETIMEDOUT;
			break;
		}
		if (signal_pending_state(state, current)) {
			ret = -EINTR;
			break;
		}

		raw_spin_unlock_irq(&lock->wait_lock);

		schedule();

		raw_spin_lock_irq(&lock->wait_lock);
		set_current_state(state);
	}

	trace_android_vh_rtmutex_wait_finish(lock);
	__set_current_state(TASK_RUNNING);
	return ret;
}

static void __sched rt_mutex_handle_deadlock(int res, int detect_deadlock,
					     struct rt_mutex_waiter *w)
{
	/*
	 * If the result is not -EDEADLOCK or the caller requested
	 * deadlock detection, nothing to do here.
	 */
	if (res != -EDEADLOCK || detect_deadlock)
		return;

	/*
	 * Yell loudly and stop the task right here.
	 */
	WARN(1, "rtmutex deadlock detected\n");
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
	}
}

/*
 * Slow path lock function:
 */
static int __sched rt_mutex_slowlock(struct rt_mutex *lock, unsigned int state,
				     struct hrtimer_sleeper *timeout,
				     enum rtmutex_chainwalk chwalk)
{
	struct rt_mutex_waiter waiter;
	unsigned long flags;
	int ret = 0;

	rt_mutex_init_waiter(&waiter);

	/*
	 * Technically we could use raw_spin_[un]lock_irq() here, but this can
	 * be called in early boot if the cmpxchg() fast path is disabled
	 * (debug, no architecture support). In this case we will acquire the
	 * rtmutex with lock->wait_lock held. But we cannot unconditionally
	 * enable interrupts in that early boot case. So we need to use the
	 * irqsave/restore variants.
	 */
	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	/* Try to acquire the lock again: */
	if (try_to_take_rt_mutex(lock, current, NULL)) {
		raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
		return 0;
	}

	set_current_state(state);

	/* Setup the timer, when timeout != NULL */
	if (unlikely(timeout))
		hrtimer_start_expires(&timeout->timer, HRTIMER_MODE_ABS);

	ret = task_blocks_on_rt_mutex(lock, &waiter, current, chwalk);

	if (likely(!ret))
		/* sleep on the mutex */
		ret = __rt_mutex_slowlock(lock, state, timeout, &waiter);

	if (unlikely(ret)) {
		__set_current_state(TASK_RUNNING);
		remove_waiter(lock, &waiter);
		rt_mutex_handle_deadlock(ret, chwalk, &waiter);
	}

	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	/* Remove pending timer: */
	if (unlikely(timeout))
		hrtimer_cancel(&timeout->timer);

	debug_rt_mutex_free_waiter(&waiter);

	return ret;
}

static int __sched __rt_mutex_slowtrylock(struct rt_mutex *lock)
{
	int ret = try_to_take_rt_mutex(lock, current, NULL);

	/*
	 * try_to_take_rt_mutex() sets the lock waiters bit
	 * unconditionally. Clean this up.
	 */
	fixup_rt_mutex_waiters(lock);

	return ret;
}

/*
 * Slow path try-lock function:
 */
static int __sched rt_mutex_slowtrylock(struct rt_mutex *lock)
{
	unsigned long flags;
	int ret;

	/*
	 * If the lock already has an owner we fail to get the lock.
	 * This can be done without taking the @lock->wait_lock as
	 * it is only being read, and this is a trylock anyway.
	 */
	if (rt_mutex_owner(lock))
		return 0;

	/*
	 * The mutex has currently no owner. Lock the wait lock and try to
	 * acquire the lock. We use irqsave here to support early boot calls.
	 */
	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	ret = __rt_mutex_slowtrylock(lock);

	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	return ret;
}

/*
 * Performs the wakeup of the top-waiter and re-enables preemption.
 */
void __sched rt_mutex_postunlock(struct wake_q_head *wake_q)
{
	wake_up_q(wake_q);

	/* Pairs with preempt_disable() in mark_wakeup_next_waiter() */
	preempt_enable();
}

/*
 * Slow path to release a rt-mutex.
 *
 * Return whether the current task needs to call rt_mutex_postunlock().
 */
static void __sched rt_mutex_slowunlock(struct rt_mutex *lock)
{
	DEFINE_WAKE_Q(wake_q);
	unsigned long flags;

	/* irqsave required to support early boot calls */
	raw_spin_lock_irqsave(&lock->wait_lock, flags);

	debug_rt_mutex_unlock(lock);

	/*
	 * We must be careful here if the fast path is enabled. If we
	 * have no waiters queued we cannot set owner to NULL here
	 * because of:
	 *
	 * foo->lock->owner = NULL;
	 *			rtmutex_lock(foo->lock);   <- fast path
	 *			free = atomic_dec_and_test(foo->refcnt);
	 *			rtmutex_unlock(foo->lock); <- fast path
	 *			if (free)
	 *				kfree(foo);
	 * raw_spin_unlock(foo->lock->wait_lock);
	 *
	 * So for the fastpath enabled kernel:
	 *
	 * Nothing can set the waiters bit as long as we hold
	 * lock->wait_lock. So we do the following sequence:
	 *
	 *	owner = rt_mutex_owner(lock);
	 *	clear_rt_mutex_waiters(lock);
	 *	raw_spin_unlock(&lock->wait_lock);
	 *	if (cmpxchg(&lock->owner, owner, 0) == owner)
	 *		return;
	 *	goto retry;
	 *
	 * The fastpath disabled variant is simple as all access to
	 * lock->owner is serialized by lock->wait_lock:
	 *
	 *	lock->owner = NULL;
	 *	raw_spin_unlock(&lock->wait_lock);
	 */
	while (!rt_mutex_has_waiters(lock)) {
		/* Drops lock->wait_lock ! */
		if (unlock_rt_mutex_safe(lock, flags) == true)
			return;
		/* Relock the rtmutex and try again */
		raw_spin_lock_irqsave(&lock->wait_lock, flags);
	}

	/*
	 * The wakeup next waiter path does not suffer from the above
	 * race. See the comments there.
	 *
	 * Queue the next waiter for wakeup once we release the wait_lock.
	 */
	mark_wakeup_next_waiter(&wake_q, lock);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	rt_mutex_postunlock(&wake_q);
}

/*
 * debug aware fast / slowpath lock,trylock,unlock
 *
 * The atomic acquire/release ops are compiled away, when either the
 * architecture does not support cmpxchg or when debugging is enabled.
 */
static __always_inline int __rt_mutex_lock(struct rt_mutex *lock, long state,
					   unsigned int subclass)
{
	int ret;

	might_sleep();
	mutex_acquire(&lock->dep_map, subclass, 0, _RET_IP_);

	if (likely(rt_mutex_cmpxchg_acquire(lock, NULL, current)))
		return 0;

	ret = rt_mutex_slowlock(lock, state, NULL, RT_MUTEX_MIN_CHAINWALK);
	if (ret)
		mutex_release(&lock->dep_map, _RET_IP_);
	return ret;
}

#ifdef CONFIG_DEBUG_LOCK_ALLOC
/**
 * rt_mutex_lock_nested - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 * @subclass: the lockdep subclass
 */
void __sched rt_mutex_lock_nested(struct rt_mutex *lock, unsigned int subclass)
{
	__rt_mutex_lock(lock, TASK_UNINTERRUPTIBLE, subclass);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_nested);

#else /* !CONFIG_DEBUG_LOCK_ALLOC */

/**
 * rt_mutex_lock - lock a rt_mutex
 *
 * @lock: the rt_mutex to be locked
 */
void __sched rt_mutex_lock(struct rt_mutex *lock)
{
	__rt_mutex_lock(lock, TASK_UNINTERRUPTIBLE, 0);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock);
#endif

/**
 * rt_mutex_lock_interruptible - lock a rt_mutex interruptible
 *
 * @lock:		the rt_mutex to be locked
 *
 * Returns:
 *  0		on success
 * -EINTR	when interrupted by a signal
 */
int __sched rt_mutex_lock_interruptible(struct rt_mutex *lock)
{
	return __rt_mutex_lock(lock, TASK_INTERRUPTIBLE, 0);
}
EXPORT_SYMBOL_GPL(rt_mutex_lock_interruptible);

/**
 * rt_mutex_trylock - try to lock a rt_mutex
 *
 * @lock:	the rt_mutex to be locked
 *
 * This function can only be called in thread context. It's safe to call it
 * from atomic regions, but not from hard or soft interrupt context.
 *
 * Returns:
 *  1 on success
 *  0 on contention
 */
int __sched rt_mutex_trylock(struct rt_mutex *lock)
{
	int ret;

	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES) && WARN_ON_ONCE(!in_task()))
		return 0;

	/*
	 * No lockdep annotation required because lockdep disables the fast
	 * path.
	 */
	if (likely(rt_mutex_cmpxchg_acquire(lock, NULL, current)))
		return 1;

	ret = rt_mutex_slowtrylock(lock);
	if (ret)
		mutex_acquire(&lock->dep_map, 0, 1, _RET_IP_);

	return ret;
}
EXPORT_SYMBOL_GPL(rt_mutex_trylock);

/**
 * rt_mutex_unlock - unlock a rt_mutex
 *
 * @lock: the rt_mutex to be unlocked
 */
void __sched rt_mutex_unlock(struct rt_mutex *lock)
{
	mutex_release(&lock->dep_map, _RET_IP_);
	if (likely(rt_mutex_cmpxchg_release(lock, current, NULL)))
		return;

	rt_mutex_slowunlock(lock);
}
EXPORT_SYMBOL_GPL(rt_mutex_unlock);

/*
 * Futex variants, must not use fastpath.
 */
int __sched rt_mutex_futex_trylock(struct rt_mutex *lock)
{
	return rt_mutex_slowtrylock(lock);
}

int __sched __rt_mutex_futex_trylock(struct rt_mutex *lock)
{
	return __rt_mutex_slowtrylock(lock);
}

/**
 * __rt_mutex_futex_unlock - Futex variant, that since futex variants
 * do not use the fast-path, can be simple and will not need to retry.
 *
 * @lock:	The rt_mutex to be unlocked
 * @wake_q:	The wake queue head from which to get the next lock waiter
 */
bool __sched __rt_mutex_futex_unlock(struct rt_mutex *lock,
				     struct wake_q_head *wake_q)
{
	lockdep_assert_held(&lock->wait_lock);

	debug_rt_mutex_unlock(lock);

	if (!rt_mutex_has_waiters(lock)) {
		lock->owner = NULL;
		return false; /* done */
	}

	/*
	 * We've already deboosted, mark_wakeup_next_waiter() will
	 * retain preempt_disabled when we drop the wait_lock, to
	 * avoid inversion prior to the wakeup.  preempt_disable()
	 * therein pairs with rt_mutex_postunlock().
	 */
	mark_wakeup_next_waiter(wake_q, lock);

	return true; /* call postunlock() */
}

void __sched rt_mutex_futex_unlock(struct rt_mutex *lock)
{
	DEFINE_WAKE_Q(wake_q);
	unsigned long flags;
	bool postunlock;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	postunlock = __rt_mutex_futex_unlock(lock, &wake_q);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	if (postunlock)
		rt_mutex_postunlock(&wake_q);
}

/**
 * __rt_mutex_init - initialize the rt_mutex
 *
 * @lock:	The rt_mutex to be initialized
 * @name:	The lock name used for debugging
 * @key:	The lock class key used for debugging
 *
 * Initialize the rt_mutex to unlocked state.
 *
 * Initializing of a locked rt_mutex is not allowed
 */
void __sched __rt_mutex_init(struct rt_mutex *lock, const char *name,
		     struct lock_class_key *key)
{
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lockdep_init_map(&lock->dep_map, name, key, 0);

	__rt_mutex_basic_init(lock);
}
EXPORT_SYMBOL_GPL(__rt_mutex_init);

/**
 * rt_mutex_init_proxy_locked - initialize and lock a rt_mutex on behalf of a
 *				proxy owner
 *
 * @lock:	the rt_mutex to be locked
 * @proxy_owner:the task to set as owner
 *
 * No locking. Caller has to do serializing itself
 *
 * Special API call for PI-futex support. This initializes the rtmutex and
 * assigns it to @proxy_owner. Concurrent operations on the rtmutex are not
 * possible at this point because the pi_state which contains the rtmutex
 * is not yet visible to other tasks.
 */
void __sched rt_mutex_init_proxy_locked(struct rt_mutex *lock,
					struct task_struct *proxy_owner)
{
	__rt_mutex_basic_init(lock);
	rt_mutex_set_owner(lock, proxy_owner);
}

/**
 * rt_mutex_proxy_unlock - release a lock on behalf of owner
 *
 * @lock:	the rt_mutex to be locked
 *
 * No locking. Caller has to do serializing itself
 *
 * Special API call for PI-futex support. This merrily cleans up the rtmutex
 * (debugging) state. Concurrent operations on this rt_mutex are not
 * possible because it belongs to the pi_state which is about to be freed
 * and it is not longer visible to other tasks.
 */
void __sched rt_mutex_proxy_unlock(struct rt_mutex *lock)
{
	debug_rt_mutex_proxy_unlock(lock);
	rt_mutex_set_owner(lock, NULL);
}

/**
 * __rt_mutex_start_proxy_lock() - Start lock acquisition for another task
 * @lock:		the rt_mutex to take
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @task:		the task to prepare
 *
 * Starts the rt_mutex acquire; it enqueues the @waiter and does deadlock
 * detection. It does not wait, see rt_mutex_wait_proxy_lock() for that.
 *
 * NOTE: does _NOT_ remove the @waiter on failure; must either call
 * rt_mutex_wait_proxy_lock() or rt_mutex_cleanup_proxy_lock() after this.
 *
 * Returns:
 *  0 - task blocked on lock
 *  1 - acquired the lock for task, caller should wake it up
 * <0 - error
 *
 * Special API call for PI-futex support.
 */
int __sched __rt_mutex_start_proxy_lock(struct rt_mutex *lock,
					struct rt_mutex_waiter *waiter,
					struct task_struct *task)
{
	int ret;

	lockdep_assert_held(&lock->wait_lock);

	if (try_to_take_rt_mutex(lock, task, NULL))
		return 1;

	/* We enforce deadlock detection for futexes */
	ret = task_blocks_on_rt_mutex(lock, waiter, task,
				      RT_MUTEX_FULL_CHAINWALK);

	if (ret && !rt_mutex_owner(lock)) {
		/*
		 * Reset the return value. We might have
		 * returned with -EDEADLK and the owner
		 * released the lock while we were walking the
		 * pi chain.  Let the waiter sort it out.
		 */
		ret = 0;
	}

	return ret;
}

/**
 * rt_mutex_start_proxy_lock() - Start lock acquisition for another task
 * @lock:		the rt_mutex to take
 * @waiter:		the pre-initialized rt_mutex_waiter
 * @task:		the task to prepare
 *
 * Starts the rt_mutex acquire; it enqueues the @waiter and does deadlock
 * detection. It does not wait, see rt_mutex_wait_proxy_lock() for that.
 *
 * NOTE: unlike __rt_mutex_start_proxy_lock this _DOES_ remove the @waiter
 * on failure.
 *
 * Returns:
 *  0 - task blocked on lock
 *  1 - acquired the lock for task, caller should wake it up
 * <0 - error
 *
 * Special API call for PI-futex support.
 */
int __sched rt_mutex_start_proxy_lock(struct rt_mutex *lock,
				      struct rt_mutex_waiter *waiter,
				      struct task_struct *task)
{
	int ret;

	raw_spin_lock_irq(&lock->wait_lock);
	ret = __rt_mutex_start_proxy_lock(lock, waiter, task);
	if (unlikely(ret))
		remove_waiter(lock, waiter);
	raw_spin_unlock_irq(&lock->wait_lock);

	return ret;
}

/**
 * rt_mutex_wait_proxy_lock() - Wait for lock acquisition
 * @lock:		the rt_mutex we were woken on
 * @to:			the timeout, null if none. hrtimer should already have
 *			been started.
 * @waiter:		the pre-initialized rt_mutex_waiter
 *
 * Wait for the lock acquisition started on our behalf by
 * rt_mutex_start_proxy_lock(). Upon failure, the caller must call
 * rt_mutex_cleanup_proxy_lock().
 *
 * Returns:
 *  0 - success
 * <0 - error, one of -EINTR, -ETIMEDOUT
 *
 * Special API call for PI-futex support
 */
int __sched rt_mutex_wait_proxy_lock(struct rt_mutex *lock,
				     struct hrtimer_sleeper *to,
				     struct rt_mutex_waiter *waiter)
{
	int ret;

	raw_spin_lock_irq(&lock->wait_lock);
	/* sleep on the mutex */
	set_current_state(TASK_INTERRUPTIBLE);
	ret = __rt_mutex_slowlock(lock, TASK_INTERRUPTIBLE, to, waiter);
	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally. We might
	 * have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);
	raw_spin_unlock_irq(&lock->wait_lock);

	return ret;
}

/**
 * rt_mutex_cleanup_proxy_lock() - Cleanup failed lock acquisition
 * @lock:		the rt_mutex we were woken on
 * @waiter:		the pre-initialized rt_mutex_waiter
 *
 * Attempt to clean up after a failed __rt_mutex_start_proxy_lock() or
 * rt_mutex_wait_proxy_lock().
 *
 * Unless we acquired the lock; we're still enqueued on the wait-list and can
 * in fact still be granted ownership until we're removed. Therefore we can
 * find we are in fact the owner and must disregard the
 * rt_mutex_wait_proxy_lock() failure.
 *
 * Returns:
 *  true  - did the cleanup, we done.
 *  false - we acquired the lock after rt_mutex_wait_proxy_lock() returned,
 *          caller should disregards its return value.
 *
 * Special API call for PI-futex support
 */
bool __sched rt_mutex_cleanup_proxy_lock(struct rt_mutex *lock,
					 struct rt_mutex_waiter *waiter)
{
	bool cleanup = false;

	raw_spin_lock_irq(&lock->wait_lock);
	/*
	 * Do an unconditional try-lock, this deals with the lock stealing
	 * state where __rt_mutex_futex_unlock() -> mark_wakeup_next_waiter()
	 * sets a NULL owner.
	 *
	 * We're not interested in the return value, because the subsequent
	 * test on rt_mutex_owner() will infer that. If the trylock succeeded,
	 * we will own the lock and it will have removed the waiter. If we
	 * failed the trylock, we're still not owner and we need to remove
	 * ourselves.
	 */
	try_to_take_rt_mutex(lock, current, waiter);
	/*
	 * Unless we're the owner; we're still enqueued on the wait_list.
	 * So check if we became owner, if not, take us off the wait_list.
	 */
	if (rt_mutex_owner(lock) != current) {
		remove_waiter(lock, waiter);
		cleanup = true;
	}
	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally. We might
	 * have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock);

	raw_spin_unlock_irq(&lock->wait_lock);

	return cleanup;
}

#ifdef CONFIG_DEBUG_RT_MUTEXES
void rt_mutex_debug_task_free(struct task_struct *task)
{
	DEBUG_LOCKS_WARN_ON(!RB_EMPTY_ROOT(&task->pi_waiters.rb_root));
	DEBUG_LOCKS_WARN_ON(task->pi_blocked_on);
}
#endif
