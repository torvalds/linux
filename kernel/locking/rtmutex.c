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
 * Adaptive Spinlocks:
 *  Copyright (C) 2008 Novell, Inc., Gregory Haskins, Sven Dietrich,
 *				     and Peter Morreale,
 * Adaptive Spinlocks simplification:
 *  Copyright (C) 2008 Red Hat, Inc., Steven Rostedt <srostedt@redhat.com>
 *
 *  See Documentation/locking/rt-mutex-design.rst for details.
 */
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/sched/deadline.h>
#include <linux/sched/signal.h>
#include <linux/sched/rt.h>
#include <linux/sched/wake_q.h>
#include <linux/ww_mutex.h>

#include <trace/events/lock.h>

#include "rtmutex_common.h"

#ifndef WW_RT
# define build_ww_mutex()	(false)
# define ww_container_of(rtm)	NULL

static inline int __ww_mutex_add_waiter(struct rt_mutex_waiter *waiter,
					struct rt_mutex *lock,
					struct ww_acquire_ctx *ww_ctx)
{
	return 0;
}

static inline void __ww_mutex_check_waiters(struct rt_mutex *lock,
					    struct ww_acquire_ctx *ww_ctx)
{
}

static inline void ww_mutex_lock_acquired(struct ww_mutex *lock,
					  struct ww_acquire_ctx *ww_ctx)
{
}

static inline int __ww_mutex_check_kill(struct rt_mutex *lock,
					struct rt_mutex_waiter *waiter,
					struct ww_acquire_ctx *ww_ctx)
{
	return 0;
}

#else
# define build_ww_mutex()	(true)
# define ww_container_of(rtm)	container_of(rtm, struct ww_mutex, base)
# include "ww_mutex.h"
#endif

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

static __always_inline struct task_struct *
rt_mutex_owner_encode(struct rt_mutex_base *lock, struct task_struct *owner)
{
	unsigned long val = (unsigned long)owner;

	if (rt_mutex_has_waiters(lock))
		val |= RT_MUTEX_HAS_WAITERS;

	return (struct task_struct *)val;
}

static __always_inline void
rt_mutex_set_owner(struct rt_mutex_base *lock, struct task_struct *owner)
{
	/*
	 * lock->wait_lock is held but explicit acquire semantics are needed
	 * for a new lock owner so WRITE_ONCE is insufficient.
	 */
	xchg_acquire(&lock->owner, rt_mutex_owner_encode(lock, owner));
}

static __always_inline void rt_mutex_clear_owner(struct rt_mutex_base *lock)
{
	/* lock->wait_lock is held so the unlock provides release semantics. */
	WRITE_ONCE(lock->owner, rt_mutex_owner_encode(lock, NULL));
}

static __always_inline void clear_rt_mutex_waiters(struct rt_mutex_base *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner & ~RT_MUTEX_HAS_WAITERS);
}

static __always_inline void
fixup_rt_mutex_waiters(struct rt_mutex_base *lock, bool acquire_lock)
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
	if (owner & RT_MUTEX_HAS_WAITERS) {
		/*
		 * See rt_mutex_set_owner() and rt_mutex_clear_owner() on
		 * why xchg_acquire() is used for updating owner for
		 * locking and WRITE_ONCE() for unlocking.
		 *
		 * WRITE_ONCE() would work for the acquire case too, but
		 * in case that the lock acquisition failed it might
		 * force other lockers into the slow path unnecessarily.
		 */
		if (acquire_lock)
			xchg_acquire(p, owner & ~RT_MUTEX_HAS_WAITERS);
		else
			WRITE_ONCE(*p, owner & ~RT_MUTEX_HAS_WAITERS);
	}
}

/*
 * We can speed up the acquire/release, if there's no debugging state to be
 * set up.
 */
#ifndef CONFIG_DEBUG_RT_MUTEXES
static __always_inline bool rt_mutex_cmpxchg_acquire(struct rt_mutex_base *lock,
						     struct task_struct *old,
						     struct task_struct *new)
{
	return try_cmpxchg_acquire(&lock->owner, &old, new);
}

static __always_inline bool rt_mutex_try_acquire(struct rt_mutex_base *lock)
{
	return rt_mutex_cmpxchg_acquire(lock, NULL, current);
}

static __always_inline bool rt_mutex_cmpxchg_release(struct rt_mutex_base *lock,
						     struct task_struct *old,
						     struct task_struct *new)
{
	return try_cmpxchg_release(&lock->owner, &old, new);
}

/*
 * Callers must hold the ->wait_lock -- which is the whole purpose as we force
 * all future threads that attempt to [Rmw] the lock to the slowpath. As such
 * relaxed semantics suffice.
 */
static __always_inline void mark_rt_mutex_waiters(struct rt_mutex_base *lock)
{
	unsigned long owner, *p = (unsigned long *) &lock->owner;

	do {
		owner = *p;
	} while (cmpxchg_relaxed(p, owner,
				 owner | RT_MUTEX_HAS_WAITERS) != owner);

	/*
	 * The cmpxchg loop above is relaxed to avoid back-to-back ACQUIRE
	 * operations in the event of contention. Ensure the successful
	 * cmpxchg is visible.
	 */
	smp_mb__after_atomic();
}

/*
 * Safe fastpath aware unlock:
 * 1) Clear the waiters bit
 * 2) Drop lock->wait_lock
 * 3) Try to unlock the lock with cmpxchg
 */
static __always_inline bool unlock_rt_mutex_safe(struct rt_mutex_base *lock,
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
static __always_inline bool rt_mutex_cmpxchg_acquire(struct rt_mutex_base *lock,
						     struct task_struct *old,
						     struct task_struct *new)
{
	return false;

}

static int __sched rt_mutex_slowtrylock(struct rt_mutex_base *lock);

static __always_inline bool rt_mutex_try_acquire(struct rt_mutex_base *lock)
{
	/*
	 * With debug enabled rt_mutex_cmpxchg trylock() will always fail.
	 *
	 * Avoid unconditionally taking the slow path by using
	 * rt_mutex_slow_trylock() which is covered by the debug code and can
	 * acquire a non-contended rtmutex.
	 */
	return rt_mutex_slowtrylock(lock);
}

static __always_inline bool rt_mutex_cmpxchg_release(struct rt_mutex_base *lock,
						     struct task_struct *old,
						     struct task_struct *new)
{
	return false;
}

static __always_inline void mark_rt_mutex_waiters(struct rt_mutex_base *lock)
{
	lock->owner = (struct task_struct *)
			((unsigned long)lock->owner | RT_MUTEX_HAS_WAITERS);
}

/*
 * Simple slow path only version: lock->owner is protected by lock->wait_lock.
 */
static __always_inline bool unlock_rt_mutex_safe(struct rt_mutex_base *lock,
						 unsigned long flags)
	__releases(lock->wait_lock)
{
	lock->owner = NULL;
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
	return true;
}
#endif

static __always_inline int __waiter_prio(struct task_struct *task)
{
	int prio = task->prio;

	if (!rt_prio(prio))
		return DEFAULT_PRIO;

	return prio;
}

/*
 * Update the waiter->tree copy of the sort keys.
 */
static __always_inline void
waiter_update_prio(struct rt_mutex_waiter *waiter, struct task_struct *task)
{
	lockdep_assert_held(&waiter->lock->wait_lock);
	lockdep_assert(RB_EMPTY_NODE(&waiter->tree.entry));

	waiter->tree.prio = __waiter_prio(task);
	waiter->tree.deadline = task->dl.deadline;
}

/*
 * Update the waiter->pi_tree copy of the sort keys (from the tree copy).
 */
static __always_inline void
waiter_clone_prio(struct rt_mutex_waiter *waiter, struct task_struct *task)
{
	lockdep_assert_held(&waiter->lock->wait_lock);
	lockdep_assert_held(&task->pi_lock);
	lockdep_assert(RB_EMPTY_NODE(&waiter->pi_tree.entry));

	waiter->pi_tree.prio = waiter->tree.prio;
	waiter->pi_tree.deadline = waiter->tree.deadline;
}

/*
 * Only use with rt_waiter_node_{less,equal}()
 */
#define task_to_waiter_node(p)	\
	&(struct rt_waiter_node){ .prio = __waiter_prio(p), .deadline = (p)->dl.deadline }
#define task_to_waiter(p)	\
	&(struct rt_mutex_waiter){ .tree = *task_to_waiter_node(p) }

static __always_inline int rt_waiter_node_less(struct rt_waiter_node *left,
					       struct rt_waiter_node *right)
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

static __always_inline int rt_waiter_node_equal(struct rt_waiter_node *left,
						 struct rt_waiter_node *right)
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

static inline bool rt_mutex_steal(struct rt_mutex_waiter *waiter,
				  struct rt_mutex_waiter *top_waiter)
{
	if (rt_waiter_node_less(&waiter->tree, &top_waiter->tree))
		return true;

#ifdef RT_MUTEX_BUILD_SPINLOCKS
	/*
	 * Note that RT tasks are excluded from same priority (lateral)
	 * steals to prevent the introduction of an unbounded latency.
	 */
	if (rt_prio(waiter->tree.prio) || dl_prio(waiter->tree.prio))
		return false;

	return rt_waiter_node_equal(&waiter->tree, &top_waiter->tree);
#else
	return false;
#endif
}

#define __node_2_waiter(node) \
	rb_entry((node), struct rt_mutex_waiter, tree.entry)

static __always_inline bool __waiter_less(struct rb_node *a, const struct rb_node *b)
{
	struct rt_mutex_waiter *aw = __node_2_waiter(a);
	struct rt_mutex_waiter *bw = __node_2_waiter(b);

	if (rt_waiter_node_less(&aw->tree, &bw->tree))
		return 1;

	if (!build_ww_mutex())
		return 0;

	if (rt_waiter_node_less(&bw->tree, &aw->tree))
		return 0;

	/* NOTE: relies on waiter->ww_ctx being set before insertion */
	if (aw->ww_ctx) {
		if (!bw->ww_ctx)
			return 1;

		return (signed long)(aw->ww_ctx->stamp -
				     bw->ww_ctx->stamp) < 0;
	}

	return 0;
}

static __always_inline void
rt_mutex_enqueue(struct rt_mutex_base *lock, struct rt_mutex_waiter *waiter)
{
	lockdep_assert_held(&lock->wait_lock);

	rb_add_cached(&waiter->tree.entry, &lock->waiters, __waiter_less);
}

static __always_inline void
rt_mutex_dequeue(struct rt_mutex_base *lock, struct rt_mutex_waiter *waiter)
{
	lockdep_assert_held(&lock->wait_lock);

	if (RB_EMPTY_NODE(&waiter->tree.entry))
		return;

	rb_erase_cached(&waiter->tree.entry, &lock->waiters);
	RB_CLEAR_NODE(&waiter->tree.entry);
}

#define __node_2_rt_node(node) \
	rb_entry((node), struct rt_waiter_node, entry)

static __always_inline bool __pi_waiter_less(struct rb_node *a, const struct rb_node *b)
{
	return rt_waiter_node_less(__node_2_rt_node(a), __node_2_rt_node(b));
}

static __always_inline void
rt_mutex_enqueue_pi(struct task_struct *task, struct rt_mutex_waiter *waiter)
{
	lockdep_assert_held(&task->pi_lock);

	rb_add_cached(&waiter->pi_tree.entry, &task->pi_waiters, __pi_waiter_less);
}

static __always_inline void
rt_mutex_dequeue_pi(struct task_struct *task, struct rt_mutex_waiter *waiter)
{
	lockdep_assert_held(&task->pi_lock);

	if (RB_EMPTY_NODE(&waiter->pi_tree.entry))
		return;

	rb_erase_cached(&waiter->pi_tree.entry, &task->pi_waiters);
	RB_CLEAR_NODE(&waiter->pi_tree.entry);
}

static __always_inline void rt_mutex_adjust_prio(struct rt_mutex_base *lock,
						 struct task_struct *p)
{
	struct task_struct *pi_task = NULL;

	lockdep_assert_held(&lock->wait_lock);
	lockdep_assert(rt_mutex_owner(lock) == p);
	lockdep_assert_held(&p->pi_lock);

	if (task_has_pi_waiters(p))
		pi_task = task_top_pi_waiter(p)->task;

	rt_mutex_setprio(p, pi_task);
}

/* RT mutex specific wake_q wrappers */
static __always_inline void rt_mutex_wake_q_add_task(struct rt_wake_q_head *wqh,
						     struct task_struct *task,
						     unsigned int wake_state)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && wake_state == TASK_RTLOCK_WAIT) {
		if (IS_ENABLED(CONFIG_PROVE_LOCKING))
			WARN_ON_ONCE(wqh->rtlock_task);
		get_task_struct(task);
		wqh->rtlock_task = task;
	} else {
		wake_q_add(&wqh->head, task);
	}
}

static __always_inline void rt_mutex_wake_q_add(struct rt_wake_q_head *wqh,
						struct rt_mutex_waiter *w)
{
	rt_mutex_wake_q_add_task(wqh, w->task, w->wake_state);
}

static __always_inline void rt_mutex_wake_up_q(struct rt_wake_q_head *wqh)
{
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && wqh->rtlock_task) {
		wake_up_state(wqh->rtlock_task, TASK_RTLOCK_WAIT);
		put_task_struct(wqh->rtlock_task);
		wqh->rtlock_task = NULL;
	}

	if (!wake_q_empty(&wqh->head))
		wake_up_q(&wqh->head);

	/* Pairs with preempt_disable() in mark_wakeup_next_waiter() */
	preempt_enable();
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

static __always_inline struct rt_mutex_base *task_blocked_on_lock(struct task_struct *p)
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
 * [Pn] task->pi_lock held
 * [L] rtmutex->wait_lock held
 *
 * Normal locking order:
 *
 *   rtmutex->wait_lock
 *     task->pi_lock
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
 * [1]	  lock(task->pi_lock);			[R] acquire [P1]
 * [2]	  waiter = task->pi_blocked_on;		[P1]
 * [3]	  check_exit_conditions_1();		[P1]
 * [4]	  lock = waiter->lock;			[P1]
 * [5]	  if (!try_lock(lock->wait_lock)) {	[P1] try to acquire [L]
 *	    unlock(task->pi_lock);		release [P1]
 *	    goto retry;
 *	  }
 * [6]	  check_exit_conditions_2();		[P1] + [L]
 * [7]	  requeue_lock_waiter(lock, waiter);	[P1] + [L]
 * [8]	  unlock(task->pi_lock);		release [P1]
 *	  put_task_struct(task);		release [R]
 * [9]	  check_exit_conditions_3();		[L]
 * [10]	  task = owner(lock);			[L]
 *	  get_task_struct(task);		[L] acquire [R]
 *	  lock(task->pi_lock);			[L] acquire [P2]
 * [11]	  requeue_pi_waiter(tsk, waiters(lock));[P2] + [L]
 * [12]	  check_exit_conditions_4();		[P2] + [L]
 * [13]	  unlock(task->pi_lock);		release [P2]
 *	  unlock(lock->wait_lock);		release [L]
 *	  goto again;
 *
 * Where P1 is the blocking task and P2 is the lock owner; going up one step
 * the owner becomes the next blocked task etc..
 *
*
 */
static int __sched rt_mutex_adjust_prio_chain(struct task_struct *task,
					      enum rtmutex_chainwalk chwalk,
					      struct rt_mutex_base *orig_lock,
					      struct rt_mutex_base *next_lock,
					      struct rt_mutex_waiter *orig_waiter,
					      struct task_struct *top_task)
{
	struct rt_mutex_waiter *waiter, *top_waiter = orig_waiter;
	struct rt_mutex_waiter *prerequeue_top_waiter;
	int ret = 0, depth = 0;
	struct rt_mutex_base *lock;
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
	 * There could be 'spurious' loops in the lock graph due to ww_mutex,
	 * consider:
	 *
	 *   P1: A, ww_A, ww_B
	 *   P2: ww_B, ww_A
	 *   P3: A
	 *
	 * P3 should not return -EDEADLK because it gets trapped in the cycle
	 * created by P1 and P2 (which will resolve -- and runs into
	 * max_lock_depth above). Therefore disable detect_deadlock such that
	 * the below termination condition can trigger once all relevant tasks
	 * are boosted.
	 *
	 * Even when we start with ww_mutex we can disable deadlock detection,
	 * since we would supress a ww_mutex induced deadlock at [6] anyway.
	 * Supressing it here however is not sufficient since we might still
	 * hit [6] due to adjustment driven iteration.
	 *
	 * NOTE: if someone were to create a deadlock between 2 ww_classes we'd
	 * utterly fail to report it; lockdep should.
	 */
	if (IS_ENABLED(CONFIG_PREEMPT_RT) && waiter->ww_ctx && detect_deadlock)
		detect_deadlock = false;

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
	if (rt_waiter_node_equal(&waiter->tree, task_to_waiter_node(task))) {
		if (!detect_deadlock)
			goto out_unlock_pi;
		else
			requeue = false;
	}

	/*
	 * [4] Get the next lock; per holding task->pi_lock we can't unblock
	 * and guarantee @lock's existence.
	 */
	lock = waiter->lock;
	/*
	 * [5] We need to trylock here as we are holding task->pi_lock,
	 * which is the reverse lock order versus the other rtmutex
	 * operations.
	 *
	 * Per the above, holding task->pi_lock guarantees lock exists, so
	 * inverting this lock order is infeasible from a life-time
	 * perspective.
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
		ret = -EDEADLK;

		/*
		 * When the deadlock is due to ww_mutex; also see above. Don't
		 * report the deadlock and instead let the ww_mutex wound/die
		 * logic pick which of the contending threads gets -EDEADLK.
		 *
		 * NOTE: assumes the cycle only contains a single ww_class; any
		 * other configuration and we fail to report; also, see
		 * lockdep.
		 */
		if (IS_ENABLED(CONFIG_PREEMPT_RT) && orig_waiter && orig_waiter->ww_ctx)
			ret = 0;

		raw_spin_unlock(&lock->wait_lock);
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
	 */
	waiter_update_prio(waiter, task);

	rt_mutex_enqueue(lock, waiter);

	/*
	 * [8] Release the (blocking) task in preparation for
	 * taking the owner task in [10].
	 *
	 * Since we hold lock->waiter_lock, task cannot unblock, even if we
	 * release task->pi_lock.
	 */
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
		top_waiter = rt_mutex_top_waiter(lock);
		if (prerequeue_top_waiter != top_waiter)
			wake_up_state(top_waiter->task, top_waiter->wake_state);
		raw_spin_unlock_irq(&lock->wait_lock);
		return 0;
	}

	/*
	 * [10] Grab the next task, i.e. the owner of @lock
	 *
	 * Per holding lock->wait_lock and checking for !owner above, there
	 * must be an owner and it cannot go away.
	 */
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
		waiter_clone_prio(waiter, task);
		rt_mutex_enqueue_pi(task, waiter);
		rt_mutex_adjust_prio(lock, task);

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
		waiter_clone_prio(waiter, task);
		rt_mutex_enqueue_pi(task, waiter);
		rt_mutex_adjust_prio(lock, task);
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
try_to_take_rt_mutex(struct rt_mutex_base *lock, struct task_struct *task,
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
		struct rt_mutex_waiter *top_waiter = rt_mutex_top_waiter(lock);

		/*
		 * If waiter is the highest priority waiter of @lock,
		 * or allowed to steal it, take it over.
		 */
		if (waiter == top_waiter || rt_mutex_steal(waiter, top_waiter)) {
			/*
			 * We can acquire the lock. Remove the waiter from the
			 * lock waiters tree.
			 */
			rt_mutex_dequeue(lock, waiter);
		} else {
			return 0;
		}
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
			/* Check whether the trylock can steal it. */
			if (!rt_mutex_steal(task_to_waiter(task),
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
static int __sched task_blocks_on_rt_mutex(struct rt_mutex_base *lock,
					   struct rt_mutex_waiter *waiter,
					   struct task_struct *task,
					   struct ww_acquire_ctx *ww_ctx,
					   enum rtmutex_chainwalk chwalk)
{
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex_waiter *top_waiter = waiter;
	struct rt_mutex_base *next_lock;
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
	 *
	 * Except for ww_mutex, in that case the chain walk must already deal
	 * with spurious cycles, see the comments at [3] and [6].
	 */
	if (owner == task && !(build_ww_mutex() && ww_ctx))
		return -EDEADLK;

	raw_spin_lock(&task->pi_lock);
	waiter->task = task;
	waiter->lock = lock;
	waiter_update_prio(waiter, task);
	waiter_clone_prio(waiter, task);

	/* Get the top priority waiter on the lock */
	if (rt_mutex_has_waiters(lock))
		top_waiter = rt_mutex_top_waiter(lock);
	rt_mutex_enqueue(lock, waiter);

	task->pi_blocked_on = waiter;

	raw_spin_unlock(&task->pi_lock);

	if (build_ww_mutex() && ww_ctx) {
		struct rt_mutex *rtm;

		/* Check whether the waiter should back out immediately */
		rtm = container_of(lock, struct rt_mutex, rtmutex);
		res = __ww_mutex_add_waiter(waiter, rtm, ww_ctx);
		if (res) {
			raw_spin_lock(&task->pi_lock);
			rt_mutex_dequeue(lock, waiter);
			task->pi_blocked_on = NULL;
			raw_spin_unlock(&task->pi_lock);
			return res;
		}
	}

	if (!owner)
		return 0;

	raw_spin_lock(&owner->pi_lock);
	if (waiter == rt_mutex_top_waiter(lock)) {
		rt_mutex_dequeue_pi(owner, top_waiter);
		rt_mutex_enqueue_pi(owner, waiter);

		rt_mutex_adjust_prio(lock, owner);
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
static void __sched mark_wakeup_next_waiter(struct rt_wake_q_head *wqh,
					    struct rt_mutex_base *lock)
{
	struct rt_mutex_waiter *waiter;

	lockdep_assert_held(&lock->wait_lock);

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
	rt_mutex_adjust_prio(lock, current);

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
	 * Pairs with preempt_enable() in rt_mutex_wake_up_q();
	 */
	preempt_disable();
	rt_mutex_wake_q_add(wqh, waiter);
	raw_spin_unlock(&current->pi_lock);
}

static int __sched __rt_mutex_slowtrylock(struct rt_mutex_base *lock)
{
	int ret = try_to_take_rt_mutex(lock, current, NULL);

	/*
	 * try_to_take_rt_mutex() sets the lock waiters bit
	 * unconditionally. Clean this up.
	 */
	fixup_rt_mutex_waiters(lock, true);

	return ret;
}

/*
 * Slow path try-lock function:
 */
static int __sched rt_mutex_slowtrylock(struct rt_mutex_base *lock)
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

static __always_inline int __rt_mutex_trylock(struct rt_mutex_base *lock)
{
	if (likely(rt_mutex_cmpxchg_acquire(lock, NULL, current)))
		return 1;

	return rt_mutex_slowtrylock(lock);
}

/*
 * Slow path to release a rt-mutex.
 */
static void __sched rt_mutex_slowunlock(struct rt_mutex_base *lock)
{
	DEFINE_RT_WAKE_Q(wqh);
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
	mark_wakeup_next_waiter(&wqh, lock);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);

	rt_mutex_wake_up_q(&wqh);
}

static __always_inline void __rt_mutex_unlock(struct rt_mutex_base *lock)
{
	if (likely(rt_mutex_cmpxchg_release(lock, current, NULL)))
		return;

	rt_mutex_slowunlock(lock);
}

#ifdef CONFIG_SMP
static bool rtmutex_spin_on_owner(struct rt_mutex_base *lock,
				  struct rt_mutex_waiter *waiter,
				  struct task_struct *owner)
{
	bool res = true;

	rcu_read_lock();
	for (;;) {
		/* If owner changed, trylock again. */
		if (owner != rt_mutex_owner(lock))
			break;
		/*
		 * Ensure that @owner is dereferenced after checking that
		 * the lock owner still matches @owner. If that fails,
		 * @owner might point to freed memory. If it still matches,
		 * the rcu_read_lock() ensures the memory stays valid.
		 */
		barrier();
		/*
		 * Stop spinning when:
		 *  - the lock owner has been scheduled out
		 *  - current is not longer the top waiter
		 *  - current is requested to reschedule (redundant
		 *    for CONFIG_PREEMPT_RCU=y)
		 *  - the VCPU on which owner runs is preempted
		 */
		if (!owner_on_cpu(owner) || need_resched() ||
		    !rt_mutex_waiter_is_top_waiter(lock, waiter)) {
			res = false;
			break;
		}
		cpu_relax();
	}
	rcu_read_unlock();
	return res;
}
#else
static bool rtmutex_spin_on_owner(struct rt_mutex_base *lock,
				  struct rt_mutex_waiter *waiter,
				  struct task_struct *owner)
{
	return false;
}
#endif

#ifdef RT_MUTEX_BUILD_MUTEX
/*
 * Functions required for:
 *	- rtmutex, futex on all kernels
 *	- mutex and rwsem substitutions on RT kernels
 */

/*
 * Remove a waiter from a lock and give up
 *
 * Must be called with lock->wait_lock held and interrupts disabled. It must
 * have just failed to try_to_take_rt_mutex().
 */
static void __sched remove_waiter(struct rt_mutex_base *lock,
				  struct rt_mutex_waiter *waiter)
{
	bool is_top_waiter = (waiter == rt_mutex_top_waiter(lock));
	struct task_struct *owner = rt_mutex_owner(lock);
	struct rt_mutex_base *next_lock;

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

	rt_mutex_adjust_prio(lock, owner);

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

/**
 * rt_mutex_slowlock_block() - Perform the wait-wake-try-to-take loop
 * @lock:		 the rt_mutex to take
 * @ww_ctx:		 WW mutex context pointer
 * @state:		 the state the task should block in (TASK_INTERRUPTIBLE
 *			 or TASK_UNINTERRUPTIBLE)
 * @timeout:		 the pre-initialized and started timer, or NULL for none
 * @waiter:		 the pre-initialized rt_mutex_waiter
 *
 * Must be called with lock->wait_lock held and interrupts disabled
 */
static int __sched rt_mutex_slowlock_block(struct rt_mutex_base *lock,
					   struct ww_acquire_ctx *ww_ctx,
					   unsigned int state,
					   struct hrtimer_sleeper *timeout,
					   struct rt_mutex_waiter *waiter)
{
	struct rt_mutex *rtm = container_of(lock, struct rt_mutex, rtmutex);
	struct task_struct *owner;
	int ret = 0;

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

		if (build_ww_mutex() && ww_ctx) {
			ret = __ww_mutex_check_kill(rtm, waiter, ww_ctx);
			if (ret)
				break;
		}

		if (waiter == rt_mutex_top_waiter(lock))
			owner = rt_mutex_owner(lock);
		else
			owner = NULL;
		raw_spin_unlock_irq(&lock->wait_lock);

		if (!owner || !rtmutex_spin_on_owner(lock, waiter, owner))
			rt_mutex_schedule();

		raw_spin_lock_irq(&lock->wait_lock);
		set_current_state(state);
	}

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

	if (build_ww_mutex() && w->ww_ctx)
		return;

	/*
	 * Yell loudly and stop the task right here.
	 */
	WARN(1, "rtmutex deadlock detected\n");
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		rt_mutex_schedule();
	}
}

/**
 * __rt_mutex_slowlock - Locking slowpath invoked with lock::wait_lock held
 * @lock:	The rtmutex to block lock
 * @ww_ctx:	WW mutex context pointer
 * @state:	The task state for sleeping
 * @chwalk:	Indicator whether full or partial chainwalk is requested
 * @waiter:	Initializer waiter for blocking
 */
static int __sched __rt_mutex_slowlock(struct rt_mutex_base *lock,
				       struct ww_acquire_ctx *ww_ctx,
				       unsigned int state,
				       enum rtmutex_chainwalk chwalk,
				       struct rt_mutex_waiter *waiter)
{
	struct rt_mutex *rtm = container_of(lock, struct rt_mutex, rtmutex);
	struct ww_mutex *ww = ww_container_of(rtm);
	int ret;

	lockdep_assert_held(&lock->wait_lock);

	/* Try to acquire the lock again: */
	if (try_to_take_rt_mutex(lock, current, NULL)) {
		if (build_ww_mutex() && ww_ctx) {
			__ww_mutex_check_waiters(rtm, ww_ctx);
			ww_mutex_lock_acquired(ww, ww_ctx);
		}
		return 0;
	}

	set_current_state(state);

	trace_contention_begin(lock, LCB_F_RT);

	ret = task_blocks_on_rt_mutex(lock, waiter, current, ww_ctx, chwalk);
	if (likely(!ret))
		ret = rt_mutex_slowlock_block(lock, ww_ctx, state, NULL, waiter);

	if (likely(!ret)) {
		/* acquired the lock */
		if (build_ww_mutex() && ww_ctx) {
			if (!ww_ctx->is_wait_die)
				__ww_mutex_check_waiters(rtm, ww_ctx);
			ww_mutex_lock_acquired(ww, ww_ctx);
		}
	} else {
		__set_current_state(TASK_RUNNING);
		remove_waiter(lock, waiter);
		rt_mutex_handle_deadlock(ret, chwalk, waiter);
	}

	/*
	 * try_to_take_rt_mutex() sets the waiter bit
	 * unconditionally. We might have to fix that up.
	 */
	fixup_rt_mutex_waiters(lock, true);

	trace_contention_end(lock, ret);

	return ret;
}

static inline int __rt_mutex_slowlock_locked(struct rt_mutex_base *lock,
					     struct ww_acquire_ctx *ww_ctx,
					     unsigned int state)
{
	struct rt_mutex_waiter waiter;
	int ret;

	rt_mutex_init_waiter(&waiter);
	waiter.ww_ctx = ww_ctx;

	ret = __rt_mutex_slowlock(lock, ww_ctx, state, RT_MUTEX_MIN_CHAINWALK,
				  &waiter);

	debug_rt_mutex_free_waiter(&waiter);
	return ret;
}

/*
 * rt_mutex_slowlock - Locking slowpath invoked when fast path fails
 * @lock:	The rtmutex to block lock
 * @ww_ctx:	WW mutex context pointer
 * @state:	The task state for sleeping
 */
static int __sched rt_mutex_slowlock(struct rt_mutex_base *lock,
				     struct ww_acquire_ctx *ww_ctx,
				     unsigned int state)
{
	unsigned long flags;
	int ret;

	/*
	 * Do all pre-schedule work here, before we queue a waiter and invoke
	 * PI -- any such work that trips on rtlock (PREEMPT_RT spinlock) would
	 * otherwise recurse back into task_blocks_on_rt_mutex() through
	 * rtlock_slowlock() and will then enqueue a second waiter for this
	 * same task and things get really confusing real fast.
	 */
	rt_mutex_pre_schedule();

	/*
	 * Technically we could use raw_spin_[un]lock_irq() here, but this can
	 * be called in early boot if the cmpxchg() fast path is disabled
	 * (debug, no architecture support). In this case we will acquire the
	 * rtmutex with lock->wait_lock held. But we cannot unconditionally
	 * enable interrupts in that early boot case. So we need to use the
	 * irqsave/restore variants.
	 */
	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	ret = __rt_mutex_slowlock_locked(lock, ww_ctx, state);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
	rt_mutex_post_schedule();

	return ret;
}

static __always_inline int __rt_mutex_lock(struct rt_mutex_base *lock,
					   unsigned int state)
{
	lockdep_assert(!current->pi_blocked_on);

	if (likely(rt_mutex_try_acquire(lock)))
		return 0;

	return rt_mutex_slowlock(lock, NULL, state);
}
#endif /* RT_MUTEX_BUILD_MUTEX */

#ifdef RT_MUTEX_BUILD_SPINLOCKS
/*
 * Functions required for spin/rw_lock substitution on RT kernels
 */

/**
 * rtlock_slowlock_locked - Slow path lock acquisition for RT locks
 * @lock:	The underlying RT mutex
 */
static void __sched rtlock_slowlock_locked(struct rt_mutex_base *lock)
{
	struct rt_mutex_waiter waiter;
	struct task_struct *owner;

	lockdep_assert_held(&lock->wait_lock);

	if (try_to_take_rt_mutex(lock, current, NULL))
		return;

	rt_mutex_init_rtlock_waiter(&waiter);

	/* Save current state and set state to TASK_RTLOCK_WAIT */
	current_save_and_set_rtlock_wait_state();

	trace_contention_begin(lock, LCB_F_RT);

	task_blocks_on_rt_mutex(lock, &waiter, current, NULL, RT_MUTEX_MIN_CHAINWALK);

	for (;;) {
		/* Try to acquire the lock again */
		if (try_to_take_rt_mutex(lock, current, &waiter))
			break;

		if (&waiter == rt_mutex_top_waiter(lock))
			owner = rt_mutex_owner(lock);
		else
			owner = NULL;
		raw_spin_unlock_irq(&lock->wait_lock);

		if (!owner || !rtmutex_spin_on_owner(lock, &waiter, owner))
			schedule_rtlock();

		raw_spin_lock_irq(&lock->wait_lock);
		set_current_state(TASK_RTLOCK_WAIT);
	}

	/* Restore the task state */
	current_restore_rtlock_saved_state();

	/*
	 * try_to_take_rt_mutex() sets the waiter bit unconditionally.
	 * We might have to fix that up:
	 */
	fixup_rt_mutex_waiters(lock, true);
	debug_rt_mutex_free_waiter(&waiter);

	trace_contention_end(lock, 0);
}

static __always_inline void __sched rtlock_slowlock(struct rt_mutex_base *lock)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&lock->wait_lock, flags);
	rtlock_slowlock_locked(lock);
	raw_spin_unlock_irqrestore(&lock->wait_lock, flags);
}

#endif /* RT_MUTEX_BUILD_SPINLOCKS */
