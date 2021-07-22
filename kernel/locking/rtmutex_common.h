/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RT Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This file contains the private data structure and API definitions.
 */

#ifndef __KERNEL_RTMUTEX_COMMON_H
#define __KERNEL_RTMUTEX_COMMON_H

#include <linux/debug_locks.h>
#include <linux/rtmutex.h>
#include <linux/sched/wake_q.h>

/*
 * This is the control structure for tasks blocked on a rt_mutex,
 * which is allocated on the kernel stack on of the blocked task.
 *
 * @tree_entry:		pi node to enqueue into the mutex waiters tree
 * @pi_tree_entry:	pi node to enqueue into the mutex owner waiters tree
 * @task:		task reference to the blocked task
 * @lock:		Pointer to the rt_mutex on which the waiter blocks
 * @prio:		Priority of the waiter
 * @deadline:		Deadline of the waiter if applicable
 */
struct rt_mutex_waiter {
	struct rb_node		tree_entry;
	struct rb_node		pi_tree_entry;
	struct task_struct	*task;
	struct rt_mutex		*lock;
	int			prio;
	u64			deadline;
};

/*
 * Must be guarded because this header is included from rcu/tree_plugin.h
 * unconditionally.
 */
#ifdef CONFIG_RT_MUTEXES
static inline int rt_mutex_has_waiters(struct rt_mutex *lock)
{
	return !RB_EMPTY_ROOT(&lock->waiters.rb_root);
}

static inline struct rt_mutex_waiter *rt_mutex_top_waiter(struct rt_mutex *lock)
{
	struct rb_node *leftmost = rb_first_cached(&lock->waiters);
	struct rt_mutex_waiter *w = NULL;

	if (leftmost) {
		w = rb_entry(leftmost, struct rt_mutex_waiter, tree_entry);
		BUG_ON(w->lock != lock);
	}
	return w;
}

static inline int task_has_pi_waiters(struct task_struct *p)
{
	return !RB_EMPTY_ROOT(&p->pi_waiters.rb_root);
}

static inline struct rt_mutex_waiter *task_top_pi_waiter(struct task_struct *p)
{
	return rb_entry(p->pi_waiters.rb_leftmost, struct rt_mutex_waiter,
			pi_tree_entry);
}

#define RT_MUTEX_HAS_WAITERS	1UL

static inline struct task_struct *rt_mutex_owner(struct rt_mutex *lock)
{
	unsigned long owner = (unsigned long) READ_ONCE(lock->owner);

	return (struct task_struct *) (owner & ~RT_MUTEX_HAS_WAITERS);
}
#else /* CONFIG_RT_MUTEXES */
/* Used in rcu/tree_plugin.h */
static inline struct task_struct *rt_mutex_owner(struct rt_mutex *lock)
{
	return NULL;
}
#endif  /* !CONFIG_RT_MUTEXES */

/*
 * Constants for rt mutex functions which have a selectable deadlock
 * detection.
 *
 * RT_MUTEX_MIN_CHAINWALK:	Stops the lock chain walk when there are
 *				no further PI adjustments to be made.
 *
 * RT_MUTEX_FULL_CHAINWALK:	Invoke deadlock detection with a full
 *				walk of the lock chain.
 */
enum rtmutex_chainwalk {
	RT_MUTEX_MIN_CHAINWALK,
	RT_MUTEX_FULL_CHAINWALK,
};

static inline void __rt_mutex_basic_init(struct rt_mutex *lock)
{
	lock->owner = NULL;
	raw_spin_lock_init(&lock->wait_lock);
	lock->waiters = RB_ROOT_CACHED;
}

/*
 * PI-futex support (proxy locking functions, etc.):
 */
extern void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
				       struct task_struct *proxy_owner);
extern void rt_mutex_proxy_unlock(struct rt_mutex *lock);
extern void rt_mutex_init_waiter(struct rt_mutex_waiter *waiter);
extern int __rt_mutex_start_proxy_lock(struct rt_mutex *lock,
				     struct rt_mutex_waiter *waiter,
				     struct task_struct *task);
extern int rt_mutex_start_proxy_lock(struct rt_mutex *lock,
				     struct rt_mutex_waiter *waiter,
				     struct task_struct *task);
extern int rt_mutex_wait_proxy_lock(struct rt_mutex *lock,
			       struct hrtimer_sleeper *to,
			       struct rt_mutex_waiter *waiter);
extern bool rt_mutex_cleanup_proxy_lock(struct rt_mutex *lock,
				 struct rt_mutex_waiter *waiter);

extern int rt_mutex_futex_trylock(struct rt_mutex *l);
extern int __rt_mutex_futex_trylock(struct rt_mutex *l);

extern void rt_mutex_futex_unlock(struct rt_mutex *lock);
extern bool __rt_mutex_futex_unlock(struct rt_mutex *lock,
				 struct wake_q_head *wqh);

extern void rt_mutex_postunlock(struct wake_q_head *wake_q);

/* Debug functions */
static inline void debug_rt_mutex_unlock(struct rt_mutex *lock)
{
	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES))
		DEBUG_LOCKS_WARN_ON(rt_mutex_owner(lock) != current);
}

static inline void debug_rt_mutex_proxy_unlock(struct rt_mutex *lock)
{
	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES))
		DEBUG_LOCKS_WARN_ON(!rt_mutex_owner(lock));
}

static inline void debug_rt_mutex_init_waiter(struct rt_mutex_waiter *waiter)
{
	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES))
		memset(waiter, 0x11, sizeof(*waiter));
}

static inline void debug_rt_mutex_free_waiter(struct rt_mutex_waiter *waiter)
{
	if (IS_ENABLED(CONFIG_DEBUG_RT_MUTEXES))
		memset(waiter, 0x22, sizeof(*waiter));
}

#endif
