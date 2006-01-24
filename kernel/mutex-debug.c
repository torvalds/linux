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

static void printk_task(struct task_struct *p)
{
	if (p)
		printk("%16s:%5d [%p, %3d]", p->comm, p->pid, p, p->prio);
	else
		printk("<none>");
}

static void printk_ti(struct thread_info *ti)
{
	if (ti)
		printk_task(ti->task);
	else
		printk("<none>");
}

static void printk_task_short(struct task_struct *p)
{
	if (p)
		printk("%s/%d [%p, %3d]", p->comm, p->pid, p, p->prio);
	else
		printk("<none>");
}

static void printk_lock(struct mutex *lock, int print_owner)
{
	printk(" [%p] {%s}\n", lock, lock->name);

	if (print_owner && lock->owner) {
		printk(".. held by:  ");
		printk_ti(lock->owner);
		printk("\n");
	}
	if (lock->owner) {
		printk("... acquired at:               ");
		print_symbol("%s\n", lock->acquire_ip);
	}
}

/*
 * printk locks held by a task:
 */
static void show_task_locks(struct task_struct *p)
{
	switch (p->state) {
	case TASK_RUNNING:		printk("R"); break;
	case TASK_INTERRUPTIBLE:	printk("S"); break;
	case TASK_UNINTERRUPTIBLE:	printk("D"); break;
	case TASK_STOPPED:		printk("T"); break;
	case EXIT_ZOMBIE:		printk("Z"); break;
	case EXIT_DEAD:			printk("X"); break;
	default:			printk("?"); break;
	}
	printk_task(p);
	if (p->blocked_on) {
		struct mutex *lock = p->blocked_on->lock;

		printk(" blocked on mutex:");
		printk_lock(lock, 1);
	} else
		printk(" (not blocked on mutex)\n");
}

/*
 * printk all locks held in the system (if filter == NULL),
 * or all locks belonging to a single task (if filter != NULL):
 */
void show_held_locks(struct task_struct *filter)
{
	struct list_head *curr, *cursor = NULL;
	struct mutex *lock;
	struct thread_info *t;
	unsigned long flags;
	int count = 0;

	if (filter) {
		printk("------------------------------\n");
		printk("| showing all locks held by: |  (");
		printk_task_short(filter);
		printk("):\n");
		printk("------------------------------\n");
	} else {
		printk("---------------------------\n");
		printk("| showing all locks held: |\n");
		printk("---------------------------\n");
	}

	/*
	 * Play safe and acquire the global trace lock. We
	 * cannot printk with that lock held so we iterate
	 * very carefully:
	 */
next:
	debug_spin_lock_save(&debug_mutex_lock, flags);
	list_for_each(curr, &debug_mutex_held_locks) {
		if (cursor && curr != cursor)
			continue;
		lock = list_entry(curr, struct mutex, held_list);
		t = lock->owner;
		if (filter && (t != filter->thread_info))
			continue;
		count++;
		cursor = curr->next;
		debug_spin_lock_restore(&debug_mutex_lock, flags);

		printk("\n#%03d:            ", count);
		printk_lock(lock, filter ? 0 : 1);
		goto next;
	}
	debug_spin_lock_restore(&debug_mutex_lock, flags);
	printk("\n");
}

void mutex_debug_show_all_locks(void)
{
	struct task_struct *g, *p;
	int count = 10;
	int unlock = 1;

	printk("\nShowing all blocking locks in the system:\n");

	/*
	 * Here we try to get the tasklist_lock as hard as possible,
	 * if not successful after 2 seconds we ignore it (but keep
	 * trying). This is to enable a debug printout even if a
	 * tasklist_lock-holding task deadlocks or crashes.
	 */
retry:
	if (!read_trylock(&tasklist_lock)) {
		if (count == 10)
			printk("hm, tasklist_lock locked, retrying... ");
		if (count) {
			count--;
			printk(" #%d", 10-count);
			mdelay(200);
			goto retry;
		}
		printk(" ignoring it.\n");
		unlock = 0;
	}
	if (count != 10)
		printk(" locked it.\n");

	do_each_thread(g, p) {
		show_task_locks(p);
		if (!unlock)
			if (read_trylock(&tasklist_lock))
				unlock = 1;
	} while_each_thread(g, p);

	printk("\n");
	show_held_locks(NULL);
	printk("=============================================\n\n");

	if (unlock)
		read_unlock(&tasklist_lock);
}

static void report_deadlock(struct task_struct *task, struct mutex *lock,
			    struct mutex *lockblk, unsigned long ip)
{
	printk("\n%s/%d is trying to acquire this lock:\n",
		current->comm, current->pid);
	printk_lock(lock, 1);
	printk("... trying at:                 ");
	print_symbol("%s\n", ip);
	show_held_locks(current);

	if (lockblk) {
		printk("but %s/%d is deadlocking current task %s/%d!\n\n",
			task->comm, task->pid, current->comm, current->pid);
		printk("\n%s/%d is blocked on this lock:\n",
			task->comm, task->pid);
		printk_lock(lockblk, 1);

		show_held_locks(task);

		printk("\n%s/%d's [blocked] stackdump:\n\n",
			task->comm, task->pid);
		show_stack(task, NULL);
	}

	printk("\n%s/%d's [current] stackdump:\n\n",
		current->comm, current->pid);
	dump_stack();
	mutex_debug_show_all_locks();
	printk("[ turning off deadlock detection. Please report this. ]\n\n");
	local_irq_disable();
}

/*
 * Recursively check for mutex deadlocks:
 */
static int check_deadlock(struct mutex *lock, int depth,
			  struct thread_info *ti, unsigned long ip)
{
	struct mutex *lockblk;
	struct task_struct *task;

	if (!debug_mutex_on)
		return 0;

	ti = lock->owner;
	if (!ti)
		return 0;

	task = ti->task;
	lockblk = NULL;
	if (task->blocked_on)
		lockblk = task->blocked_on->lock;

	/* Self-deadlock: */
	if (current == task) {
		DEBUG_OFF();
		if (depth)
			return 1;
		printk("\n==========================================\n");
		printk(  "[ BUG: lock recursion deadlock detected! |\n");
		printk(  "------------------------------------------\n");
		report_deadlock(task, lock, NULL, ip);
		return 0;
	}

	/* Ugh, something corrupted the lock data structure? */
	if (depth > 20) {
		DEBUG_OFF();
		printk("\n===========================================\n");
		printk(  "[ BUG: infinite lock dependency detected!? |\n");
		printk(  "-------------------------------------------\n");
		report_deadlock(task, lock, lockblk, ip);
		return 0;
	}

	/* Recursively check for dependencies: */
	if (lockblk && check_deadlock(lockblk, depth+1, ti, ip)) {
		printk("\n============================================\n");
		printk(  "[ BUG: circular locking deadlock detected! ]\n");
		printk(  "--------------------------------------------\n");
		report_deadlock(task, lock, lockblk, ip);
		return 0;
	}
	return 0;
}

/*
 * Called when a task exits, this function checks whether the
 * task is holding any locks, and reports the first one if so:
 */
void mutex_debug_check_no_locks_held(struct task_struct *task)
{
	struct list_head *curr, *next;
	struct thread_info *t;
	unsigned long flags;
	struct mutex *lock;

	if (!debug_mutex_on)
		return;

	debug_spin_lock_save(&debug_mutex_lock, flags);
	list_for_each_safe(curr, next, &debug_mutex_held_locks) {
		lock = list_entry(curr, struct mutex, held_list);
		t = lock->owner;
		if (t != task->thread_info)
			continue;
		list_del_init(curr);
		DEBUG_OFF();
		debug_spin_lock_restore(&debug_mutex_lock, flags);

		printk("BUG: %s/%d, lock held at task exit time!\n",
			task->comm, task->pid);
		printk_lock(lock, 1);
		if (lock->owner != task->thread_info)
			printk("exiting task is not even the owner??\n");
		return;
	}
	debug_spin_lock_restore(&debug_mutex_lock, flags);
}

/*
 * Called when kernel memory is freed (or unmapped), or if a mutex
 * is destroyed or reinitialized - this code checks whether there is
 * any held lock in the memory range of <from> to <to>:
 */
void mutex_debug_check_no_locks_freed(const void *from, unsigned long len)
{
	struct list_head *curr, *next;
	const void *to = from + len;
	unsigned long flags;
	struct mutex *lock;
	void *lock_addr;

	if (!debug_mutex_on)
		return;

	debug_spin_lock_save(&debug_mutex_lock, flags);
	list_for_each_safe(curr, next, &debug_mutex_held_locks) {
		lock = list_entry(curr, struct mutex, held_list);
		lock_addr = lock;
		if (lock_addr < from || lock_addr >= to)
			continue;
		list_del_init(curr);
		DEBUG_OFF();
		debug_spin_lock_restore(&debug_mutex_lock, flags);

		printk("BUG: %s/%d, active lock [%p(%p-%p)] freed!\n",
			current->comm, current->pid, lock, from, to);
		dump_stack();
		printk_lock(lock, 1);
		if (lock->owner != current_thread_info())
			printk("freeing task is not even the owner??\n");
		return;
	}
	debug_spin_lock_restore(&debug_mutex_lock, flags);
}

/*
 * Must be called with lock->wait_lock held.
 */
void debug_mutex_set_owner(struct mutex *lock,
			   struct thread_info *new_owner __IP_DECL__)
{
	lock->owner = new_owner;
	DEBUG_WARN_ON(!list_empty(&lock->held_list));
	if (debug_mutex_on) {
		list_add_tail(&lock->held_list, &debug_mutex_held_locks);
		lock->acquire_ip = ip;
	}
}

void debug_mutex_init_waiter(struct mutex_waiter *waiter)
{
	memset(waiter, 0x11, sizeof(*waiter));
	waiter->magic = waiter;
	INIT_LIST_HEAD(&waiter->list);
}

void debug_mutex_wake_waiter(struct mutex *lock, struct mutex_waiter *waiter)
{
	SMP_DEBUG_WARN_ON(!spin_is_locked(&lock->wait_lock));
	DEBUG_WARN_ON(list_empty(&lock->wait_list));
	DEBUG_WARN_ON(waiter->magic != waiter);
	DEBUG_WARN_ON(list_empty(&waiter->list));
}

void debug_mutex_free_waiter(struct mutex_waiter *waiter)
{
	DEBUG_WARN_ON(!list_empty(&waiter->list));
	memset(waiter, 0x22, sizeof(*waiter));
}

void debug_mutex_add_waiter(struct mutex *lock, struct mutex_waiter *waiter,
			    struct thread_info *ti __IP_DECL__)
{
	SMP_DEBUG_WARN_ON(!spin_is_locked(&lock->wait_lock));
	check_deadlock(lock, 0, ti, ip);
	/* Mark the current thread as blocked on the lock: */
	ti->task->blocked_on = waiter;
	waiter->lock = lock;
}

void mutex_remove_waiter(struct mutex *lock, struct mutex_waiter *waiter,
			 struct thread_info *ti)
{
	DEBUG_WARN_ON(list_empty(&waiter->list));
	DEBUG_WARN_ON(waiter->task != ti->task);
	DEBUG_WARN_ON(ti->task->blocked_on != waiter);
	ti->task->blocked_on = NULL;

	list_del_init(&waiter->list);
	waiter->task = NULL;
}

void debug_mutex_unlock(struct mutex *lock)
{
	DEBUG_WARN_ON(lock->magic != lock);
	DEBUG_WARN_ON(!lock->wait_list.prev && !lock->wait_list.next);
	DEBUG_WARN_ON(lock->owner != current_thread_info());
	if (debug_mutex_on) {
		DEBUG_WARN_ON(list_empty(&lock->held_list));
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
	DEBUG_WARN_ON(mutex_is_locked(lock));
	lock->magic = NULL;
}

EXPORT_SYMBOL_GPL(mutex_destroy);
