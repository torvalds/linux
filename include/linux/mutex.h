/*
 * Mutexes: blocking mutual exclusion locks
 *
 * started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * This file contains the main data structure and API definitions.
 */
#ifndef __LINUX_MUTEX_H
#define __LINUX_MUTEX_H

#include <linux/list.h>
#include <linux/spinlock_types.h>
#include <linux/linkage.h>

#include <asm/atomic.h>

/*
 * Simple, straightforward mutexes with strict semantics:
 *
 * - only one task can hold the mutex at a time
 * - only the owner can unlock the mutex
 * - multiple unlocks are not permitted
 * - recursive locking is not permitted
 * - a mutex object must be initialized via the API
 * - a mutex object must not be initialized via memset or copying
 * - task may not exit with mutex held
 * - memory areas where held locks reside must not be freed
 * - held mutexes must not be reinitialized
 * - mutexes may not be used in irq contexts
 *
 * These semantics are fully enforced when DEBUG_MUTEXES is
 * enabled. Furthermore, besides enforcing the above rules, the mutex
 * debugging code also implements a number of additional features
 * that make lock debugging easier and faster:
 *
 * - uses symbolic names of mutexes, whenever they are printed in debug output
 * - point-of-acquire tracking, symbolic lookup of function names
 * - list of all locks held in the system, printout of them
 * - owner tracking
 * - detects self-recursing locks and prints out all relevant info
 * - detects multi-task circular deadlocks and prints out all affected
 *   locks and tasks (and only those tasks)
 */
struct mutex {
	/* 1: unlocked, 0: locked, negative: locked, possible waiters */
	atomic_t		count;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
#ifdef CONFIG_DEBUG_MUTEXES
	struct thread_info	*owner;
	struct list_head	held_list;
	unsigned long		acquire_ip;
	const char 		*name;
	void			*magic;
#endif
};

/*
 * This is the control structure for tasks blocked on mutex,
 * which resides on the blocked task's kernel stack:
 */
struct mutex_waiter {
	struct list_head	list;
	struct task_struct	*task;
#ifdef CONFIG_DEBUG_MUTEXES
	struct mutex		*lock;
	void			*magic;
#endif
};

#ifdef CONFIG_DEBUG_MUTEXES
# include <linux/mutex-debug.h>
#else
# define __DEBUG_MUTEX_INITIALIZER(lockname)
# define mutex_init(mutex)			__mutex_init(mutex, NULL)
# define mutex_destroy(mutex)				do { } while (0)
# define mutex_debug_show_all_locks()			do { } while (0)
# define mutex_debug_show_held_locks(p)			do { } while (0)
# define mutex_debug_check_no_locks_held(task)		do { } while (0)
# define mutex_debug_check_no_locks_freed(from, len)	do { } while (0)
#endif

#define __MUTEX_INITIALIZER(lockname) \
		{ .count = ATOMIC_INIT(1) \
		, .wait_lock = SPIN_LOCK_UNLOCKED \
		, .wait_list = LIST_HEAD_INIT(lockname.wait_list) \
		__DEBUG_MUTEX_INITIALIZER(lockname) }

#define DEFINE_MUTEX(mutexname) \
	struct mutex mutexname = __MUTEX_INITIALIZER(mutexname)

extern void fastcall __mutex_init(struct mutex *lock, const char *name);

/***
 * mutex_is_locked - is the mutex locked
 * @lock: the mutex to be queried
 *
 * Returns 1 if the mutex is locked, 0 if unlocked.
 */
static inline int fastcall mutex_is_locked(struct mutex *lock)
{
	return atomic_read(&lock->count) != 1;
}

/*
 * See kernel/mutex.c for detailed documentation of these APIs.
 * Also see Documentation/mutex-design.txt.
 */
extern void fastcall mutex_lock(struct mutex *lock);
extern int fastcall mutex_lock_interruptible(struct mutex *lock);
/*
 * NOTE: mutex_trylock() follows the spin_trylock() convention,
 *       not the down_trylock() convention!
 */
extern int fastcall mutex_trylock(struct mutex *lock);
extern void fastcall mutex_unlock(struct mutex *lock);

#endif
