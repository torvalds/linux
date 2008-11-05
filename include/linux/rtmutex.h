/*
 * RT Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006, Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This file contains the public data structure and API definitions.
 */

#ifndef __LINUX_RT_MUTEX_H
#define __LINUX_RT_MUTEX_H

#include <linux/linkage.h>
#include <linux/plist.h>
#include <linux/spinlock_types.h>

/**
 * The rt_mutex structure
 *
 * @wait_lock:	spinlock to protect the structure
 * @wait_list:	pilist head to enqueue waiters in priority order
 * @owner:	the mutex owner
 */
struct rt_mutex {
	spinlock_t		wait_lock;
	struct plist_head	wait_list;
	struct task_struct	*owner;
#ifdef CONFIG_DEBUG_RT_MUTEXES
	int			save_state;
	const char 		*name, *file;
	int			line;
	void			*magic;
#endif
};

struct rt_mutex_waiter;
struct hrtimer_sleeper;

#ifdef CONFIG_DEBUG_RT_MUTEXES
 extern int rt_mutex_debug_check_no_locks_freed(const void *from,
						unsigned long len);
 extern void rt_mutex_debug_check_no_locks_held(struct task_struct *task);
#else
 static inline int rt_mutex_debug_check_no_locks_freed(const void *from,
						       unsigned long len)
 {
	return 0;
 }
# define rt_mutex_debug_check_no_locks_held(task)	do { } while (0)
#endif

#ifdef CONFIG_DEBUG_RT_MUTEXES
# define __DEBUG_RT_MUTEX_INITIALIZER(mutexname) \
	, .name = #mutexname, .file = __FILE__, .line = __LINE__
# define rt_mutex_init(mutex)			__rt_mutex_init(mutex, __func__)
 extern void rt_mutex_debug_task_free(struct task_struct *tsk);
#else
# define __DEBUG_RT_MUTEX_INITIALIZER(mutexname)
# define rt_mutex_init(mutex)			__rt_mutex_init(mutex, NULL)
# define rt_mutex_debug_task_free(t)			do { } while (0)
#endif

#define __RT_MUTEX_INITIALIZER(mutexname) \
	{ .wait_lock = __SPIN_LOCK_UNLOCKED(mutexname.wait_lock) \
	, .wait_list = PLIST_HEAD_INIT(mutexname.wait_list, mutexname.wait_lock) \
	, .owner = NULL \
	__DEBUG_RT_MUTEX_INITIALIZER(mutexname)}

#define DEFINE_RT_MUTEX(mutexname) \
	struct rt_mutex mutexname = __RT_MUTEX_INITIALIZER(mutexname)

/**
 * rt_mutex_is_locked - is the mutex locked
 * @lock: the mutex to be queried
 *
 * Returns 1 if the mutex is locked, 0 if unlocked.
 */
static inline int rt_mutex_is_locked(struct rt_mutex *lock)
{
	return lock->owner != NULL;
}

extern void __rt_mutex_init(struct rt_mutex *lock, const char *name);
extern void rt_mutex_destroy(struct rt_mutex *lock);

extern void rt_mutex_lock(struct rt_mutex *lock);
extern int rt_mutex_lock_interruptible(struct rt_mutex *lock,
						int detect_deadlock);
extern int rt_mutex_timed_lock(struct rt_mutex *lock,
					struct hrtimer_sleeper *timeout,
					int detect_deadlock);

extern int rt_mutex_trylock(struct rt_mutex *lock);

extern void rt_mutex_unlock(struct rt_mutex *lock);

#ifdef CONFIG_RT_MUTEXES
# define INIT_RT_MUTEXES(tsk)						\
	.pi_waiters	= PLIST_HEAD_INIT(tsk.pi_waiters, tsk.pi_lock),	\
	INIT_RT_MUTEX_DEBUG(tsk)
#else
# define INIT_RT_MUTEXES(tsk)
#endif

#endif
