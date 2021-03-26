// SPDX-License-Identifier: GPL-2.0
/*
 * RT-Mutexes: blocking mutual exclusion locks with PI support
 *
 * started by Ingo Molnar and Thomas Gleixner:
 *
 *  Copyright (C) 2004-2006 Red Hat, Inc., Ingo Molnar <mingo@redhat.com>
 *  Copyright (C) 2006 Timesys Corp., Thomas Gleixner <tglx@timesys.com>
 *
 * This code is based on the rt.c implementation in the preempt-rt tree.
 * Portions of said code are
 *
 *  Copyright (C) 2004  LynuxWorks, Inc., Igor Manyilov, Bill Huey
 *  Copyright (C) 2006  Esben Nielsen
 *  Copyright (C) 2006  Kihon Technologies Inc.,
 *			Steven Rostedt <rostedt@goodmis.org>
 *
 * See rt.c in preempt-rt for proper credits and further information
 */
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/sched/debug.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/rbtree.h>
#include <linux/fs.h>
#include <linux/debug_locks.h>

#include "rtmutex_common.h"

void rt_mutex_debug_task_free(struct task_struct *task)
{
	DEBUG_LOCKS_WARN_ON(!RB_EMPTY_ROOT(&task->pi_waiters.rb_root));
	DEBUG_LOCKS_WARN_ON(task->pi_blocked_on);
}

void debug_rt_mutex_lock(struct rt_mutex *lock)
{
}

void debug_rt_mutex_unlock(struct rt_mutex *lock)
{
	DEBUG_LOCKS_WARN_ON(rt_mutex_owner(lock) != current);
}

void
debug_rt_mutex_proxy_lock(struct rt_mutex *lock, struct task_struct *powner)
{
}

void debug_rt_mutex_proxy_unlock(struct rt_mutex *lock)
{
	DEBUG_LOCKS_WARN_ON(!rt_mutex_owner(lock));
}

void debug_rt_mutex_init_waiter(struct rt_mutex_waiter *waiter)
{
	memset(waiter, 0x11, sizeof(*waiter));
}

void debug_rt_mutex_free_waiter(struct rt_mutex_waiter *waiter)
{
	memset(waiter, 0x22, sizeof(*waiter));
}

void debug_rt_mutex_init(struct rt_mutex *lock, const char *name, struct lock_class_key *key)
{
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif
}
