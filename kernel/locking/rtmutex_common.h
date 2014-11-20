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

/*
 * The rtmutex in kernel tester is independent of rtmutex debugging. We
 * call schedule_rt_mutex_test() instead of schedule() for the tasks which
 * belong to the tester. That way we can delay the wakeup path of those
 * threads to provoke lock stealing and testing of  complex boosting scenarios.
 */
#ifdef CONFIG_RT_MUTEX_TESTER

extern void schedule_rt_mutex_test(struct rt_mutex *lock);

#define schedule_rt_mutex(_lock)				\
  do {								\
	if (!(current->flags & PF_MUTEX_TESTER))		\
		schedule();					\
	else							\
		schedule_rt_mutex_test(_lock);			\
  } while (0)

#else
# define schedule_rt_mutex(_lock)			schedule()
#endif

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
};

/*
 * Various helpers to access the waiters-tree:
 */
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

/*
 * lock->owner state tracking:
 */
#define RT_MUTEX_HAS_WAITERS	1UL
#define RT_MUTEX_OWNER_MASKALL	1UL

static inline struct task_struct *rt_mutex_owner(struct rt_mutex *lock)
{
	return (struct task_struct *)
		((unsigned long)lock->owner & ~RT_MUTEX_OWNER_MASKALL);
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
extern int rt_mutex_start_proxy_lock(struct rt_mutex *lock,
				     struct rt_mutex_waiter *waiter,
				     struct task_struct *task);
extern int rt_mutex_finish_proxy_lock(struct rt_mutex *lock,
				      struct hrtimer_sleeper *to,
				      struct rt_mutex_waiter *waiter);
extern int rt_mutex_timed_futex_lock(struct rt_mutex *l, struct hrtimer_sleeper *to);

#ifdef CONFIG_DEBUG_RT_MUTEXES
# include "rtmutex-debug.h"
#else
# include "rtmutex.h"
#endif

#endif
