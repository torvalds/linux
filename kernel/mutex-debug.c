/*
 * kernel/mutex-debug.c
 *
 * Debugging code for mutexes
 *
 * Started by Ingo Molnar:
 *
 *  Copyright (C) 2004, 2005, 2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *
 * lock debugging, locking tree, deadlock detection started by:
 *
 *  Copyright (C) 2004, LynuxWorks, Inc., Igor Manyilov, Bill Huey
 *  Released under the General Public License (GPL).
 */
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/poison.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/interrupt.h>

#include "mutex-debug.h"

/*
 * We need a global lock when we walk through the multi-process
 * lock tree. Only used in the deadlock-debugging case.
 */
DEFINE_SPINLOCK(debug_mutex_lock);

/*
 * All locks held by all tasks, in a single global list:
 */
LIST_HEAD(debug_mutex_held_locks);

/*
 * In the debug case we carry the caller's instruction pointer into
 * other functions, but we dont want the function argument overhead
 * in the nondebug case - hence these macros:
 */
#define __IP_DECL__		, unsigned long ip
#define __IP__			, ip
#define __RET_IP__		, (unsigned long)__builtin_return_address(0)

/*
 * "mutex debugging enabled" flag. We turn it off when we detect
 * the first problem because we dont want to recurse back
 * into the tracing code when doing error printk or
 * executing a BUG():
 */
int debug_mutex_on = 1;

/*
 * Must be called with lock->wait_lock held.
 */
void debug_mutex_set_owner(struct mutex *lock,
			   struct thread_info *new_owner __IP_DECL__)
{
	lock->owner = new_owner;
	DEBUG_LOCKS_WARN_ON(!list_empty(&lock->held_list));
	if (debug_mutex_on) {
		list_add_tail(&lock->held_list, &debug_mutex_held_locks);
		lock->acquire_ip = ip;
	}
}

void debug_mutex_init_waiter(struct mutex_waiter *waiter)
{
	memset(waiter, MUTEX_DEBUG_INIT, sizeof(*waiter));
	waiter->magic = waiter;
	INIT_LIST_HEAD(&waiter->list);
}

void debug_mutex_wake_waiter(struct mutex *lock, struct mutex_waiter *waiter)
{
	SMP_DEBUG_LOCKS_WARN_ON(!spin_is_locked(&lock->wait_lock));
	DEBUG_LOCKS_WARN_ON(list_empty(&lock->wait_list));
	DEBUG_LOCKS_WARN_ON(waiter->magic != waiter);
	DEBUG_LOCKS_WARN_ON(list_empty(&waiter->list));
}

void debug_mutex_free_waiter(struct mutex_waiter *waiter)
{
	DEBUG_LOCKS_WARN_ON(!list_empty(&waiter->list));
	memset(waiter, MUTEX_DEBUG_FREE, sizeof(*waiter));
}

void debug_mutex_add_waiter(struct mutex *lock, struct mutex_waiter *waiter,
			    struct thread_info *ti __IP_DECL__)
{
	SMP_DEBUG_LOCKS_WARN_ON(!spin_is_locked(&lock->wait_lock));
	/* Mark the current thread as blocked on the lock: */
	ti->task->blocked_on = waiter;
	waiter->lock = lock;
}

void mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter,
			 struct thread_info *ti)
{
	DEBUG_LOCKS_WARN_ON(list_empty(&waiter->list));
	DEBUG_LOCKS_WARN_ON(waiter->task != ti->task);
	DEBUG_LOCKS_WARN_ON(ti->task->blocked_on != waiter);
	ti->task->blocked_on = NULL;

	list_del_init(&waiter->list);
	waiter->task = NULL;
}

void debug_mutex_unlock(struct mutex *lock)
{
	DEBUG_LOCKS_WARN_ON(lock->magic != lock);
	DEBUG_LOCKS_WARN_ON(!lock->wait_list.prev && !lock->wait_list.next);
	DEBUG_LOCKS_WARN_ON(lock->owner != current_thread_info());
	if (debug_mutex_on) {
		DEBUG_LOCKS_WARN_ON(list_empty(&lock->held_list));
		list_del_init(&lock->held_list);
	}
}

void debug_mutex_init(struct mutex *lock, const char *name)
{
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	mutex_debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lock->owner = NULL;
	INIT_LIST_HEAD(&lock->held_list);
	lock->name = name;
	lock->magic = lock;
}

/***
 * mutex_destroy - mark a mutex unusable
 * @lock: the mutex to be destroyed
 *
 * This function marks the mutex uninitialized, and any subsequent
 * use of the mutex is forbidden. The mutex must not be locked when
 * this function is called.
 */
void fastcall mutex_destroy(struct mutex *lock)
{
	DEBUG_LOCKS_WARN_ON(mutex_is_locked(lock));
	lock->magic = NULL;
}

EXPORT_SYMBOL_GPL(mutex_destroy);
