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

#include <linux/rtmutex.h>
#include <linux/sched/wake_q.h>

/*
 * This is the control structure for tasks blocked on a rt_mutex,
 * which is allocated on the kernel stack on of the blocked task.
 *
 * @tree_entry:		pi node to enqueue into the mutex waiters tree
 * @pi_tree_entry:	pi node to enqueue into the mutex owner waiters tree
 * @task:		task reference to the blocked task
 */
struct rt_mutex_waiter {
	struct rb_node          tree_entry;
	struct rb_node          pi_tree_entry;
	struct task_struct	*task;
	struct rt_mutex		*lock;
#ifdef CONFIG_DEBUG_RT_MUTEXES
	unsigned long		ip;
	struct pid		*deadlock_task_pid;
	struct rt_mutex		*deadlock_lock;
#endif
	int prio;
	u64 deadline;
};

/*
 * Various helpers to access the waiters-tree:
 */

#ifdef CONFIG_RT_MUTEXES

static inline int rt_mutex_has_waiters(struct rt_mutex *lock)
{
	return !RB_EMPTY_ROOT(&lock->waiters);
}

static inline struct rt_mutex_waiter *
rt_mutex_top_waiter(struct rt_mutex *lock)
{
	struct rt_mutex_waiter *w;

	w = rb_entry(lock->waiters_leftmost, struct rt_mutex_waiter,
		     tree_entry);
	BUG_ON(w->lock != lock);

	return w;
}

static inline int task_has_pi_waiters(struct task_struct *p)
{
	return !RB_EMPTY_ROOT(&p->pi_waiters);
}

static inline struct rt_mutex_waiter *
task_top_pi_waiter(struct task_struct *p)
{
	return rb_entry(p->pi_waiters_leftmost, struct rt_mutex_waiter,
			pi_tree_entry);
}

#else

static inline int rt_mutex_has_waiters(struct rt_mutex *lock)
{
	return false;
}

static inline struct rt_mutex_waiter *
rt_mutex_top_waiter(struct rt_mutex *lock)
{
	return NULL;
}

static inline int task_has_pi_waiters(struct task_struct *p)
{
	return false;
}

static inline struct rt_mutex_waiter *
task_top_pi_waiter(struct task_struct *p)
{
	return NULL;
}

#endif

/*
 * lock->owner state tracking:
 */
#define RT_MUTEX_HAS_WAITERS	1UL

static inline struct task_struct *rt_mutex_owner(struct rt_mutex *lock)
{
	unsigned long owner = (unsigned long) READ_ONCE(lock->owner);

	return (struct task_struct *) (owner & ~RT_MUTEX_HAS_WAITERS);
}

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

/*
 * PI-futex support (proxy locking functions, etc.):
 */
extern struct task_struct *rt_mutex_next_owner(struct rt_mutex *lock);
extern void rt_mutex_init_proxy_locked(struct rt_mutex *lock,
				       struct task_struct *proxy_owner);
extern void rt_mutex_proxy_unlock(struct rt_mutex *lock,
				  struct task_struct *proxy_owner);
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

extern void rt_mutex_futex_unlock(struct rt_mutex *lock);
extern bool __rt_mutex_futex_unlock(struct rt_mutex *lock,
				 struct wake_q_head *wqh);

extern void rt_mutex_postunlock(struct wake_q_head *wake_q);

#ifdef CONFIG_DEBUG_RT_MUTEXES
# include "rtmutex-debug.h"
#else
# include "rtmutex.h"
#endif

#endif
