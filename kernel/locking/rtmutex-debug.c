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

static void printk_task(struct task_struct *p)
{
	if (p)
		printk("%16s:%5d [%p, %3d]", p->comm, task_pid_nr(p), p, p->prio);
	else
		printk("<none>");
}

static void printk_lock(struct rt_mutex *lock, int print_owner)
{
	printk(" [%p] {%s}\n", lock, lock->name);

	if (print_owner && rt_mutex_owner(lock)) {
		printk(".. ->owner: %p\n", lock->owner);
		printk(".. held by:  ");
		printk_task(rt_mutex_owner(lock));
		printk("\n");
	}
}

void rt_mutex_debug_task_free(struct task_struct *task)
{
	DEBUG_LOCKS_WARN_ON(!RB_EMPTY_ROOT(&task->pi_waiters.rb_root));
	DEBUG_LOCKS_WARN_ON(task->pi_blocked_on);
}

/*
 * We fill out the fields in the waiter to store the information about
 * the deadlock. We print when we return. act_waiter can be NULL in
 * case of a remove waiter operation.
 */
void debug_rt_mutex_deadlock(enum rtmutex_chainwalk chwalk,
			     struct rt_mutex_waiter *act_waiter,
			     struct rt_mutex *lock)
{
	struct task_struct *task;

	if (!debug_locks || chwalk == RT_MUTEX_FULL_CHAINWALK || !act_waiter)
		return;

	task = rt_mutex_owner(act_waiter->lock);
	if (task && task != current) {
		act_waiter->deadlock_task_pid = get_pid(task_pid(task));
		act_waiter->deadlock_lock = lock;
	}
}

void debug_rt_mutex_print_deadlock(struct rt_mutex_waiter *waiter)
{
	struct task_struct *task;

	if (!waiter->deadlock_lock || !debug_locks)
		return;

	rcu_read_lock();
	task = pid_task(waiter->deadlock_task_pid, PIDTYPE_PID);
	if (!task) {
		rcu_read_unlock();
		return;
	}

	if (!debug_locks_off()) {
		rcu_read_unlock();
		return;
	}

	pr_warn("\n");
	pr_warn("============================================\n");
	pr_warn("WARNING: circular locking deadlock detected!\n");
	pr_warn("%s\n", print_tainted());
	pr_warn("--------------------------------------------\n");
	printk("%s/%d is deadlocking current task %s/%d\n\n",
	       task->comm, task_pid_nr(task),
	       current->comm, task_pid_nr(current));

	printk("\n1) %s/%d is trying to acquire this lock:\n",
	       current->comm, task_pid_nr(current));
	printk_lock(waiter->lock, 1);

	printk("\n2) %s/%d is blocked on this lock:\n",
		task->comm, task_pid_nr(task));
	printk_lock(waiter->deadlock_lock, 1);

	debug_show_held_locks(current);
	debug_show_held_locks(task);

	printk("\n%s/%d's [blocked] stackdump:\n\n",
		task->comm, task_pid_nr(task));
	show_stack(task, NULL, KERN_DEFAULT);
	printk("\n%s/%d's [current] stackdump:\n\n",
		current->comm, task_pid_nr(current));
	dump_stack();
	debug_show_all_locks();
	rcu_read_unlock();

	printk("[ turning off deadlock detection."
	       "Please report this trace. ]\n\n");
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
	waiter->deadlock_task_pid = NULL;
}

void debug_rt_mutex_free_waiter(struct rt_mutex_waiter *waiter)
{
	put_pid(waiter->deadlock_task_pid);
	memset(waiter, 0x22, sizeof(*waiter));
}

void debug_rt_mutex_init(struct rt_mutex *lock, const char *name, struct lock_class_key *key)
{
	/*
	 * Make sure we are not reinitializing a held lock:
	 */
	debug_check_no_locks_freed((void *)lock, sizeof(*lock));
	lock->name = name;

#ifdef CONFIG_DEBUG_LOCK_ALLOC
	lockdep_init_map(&lock->dep_map, name, key, 0);
#endif
}

