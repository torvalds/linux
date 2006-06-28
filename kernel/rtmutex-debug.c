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
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/kallsyms.h>
#include <linux/syscalls.h>
#include <linux/interrupt.h>
#include <linux/plist.h>
#include <linux/fs.h>

#include "rtmutex_common.h"

#ifdef CONFIG_DEBUG_RT_MUTEXES
# include "rtmutex-debug.h"
#else
# include "rtmutex.h"
#endif

# define TRACE_WARN_ON(x)			WARN_ON(x)
# define TRACE_BUG_ON(x)			BUG_ON(x)

# define TRACE_OFF()						\
do {								\
	if (rt_trace_on) {					\
		rt_trace_on = 0;				\
		console_verbose();				\
		if (spin_is_locked(&current->pi_lock))		\
			spin_unlock(&current->pi_lock);		\
		if (spin_is_locked(&current->held_list_lock))	\
			spin_unlock(&current->held_list_lock);	\
	}							\
} while (0)

# define TRACE_OFF_NOLOCK()					\
do {								\
	if (rt_trace_on) {					\
		rt_trace_on = 0;				\
		console_verbose();				\
	}							\
} while (0)

# define TRACE_BUG_LOCKED()			\
do {						\
	TRACE_OFF();				\
	BUG();					\
} while (0)

# define TRACE_WARN_ON_LOCKED(c)		\
do {						\
	if (unlikely(c)) {			\
		TRACE_OFF();			\
		WARN_ON(1);			\
	}					\
} while (0)

# define TRACE_BUG_ON_LOCKED(c)			\
do {						\
	if (unlikely(c))			\
		TRACE_BUG_LOCKED();		\
} while (0)

#ifdef CONFIG_SMP
# define SMP_TRACE_BUG_ON_LOCKED(c)	TRACE_BUG_ON_LOCKED(c)
#else
# define SMP_TRACE_BUG_ON_LOCKED(c)	do { } while (0)
#endif

/*
 * deadlock detection flag. We turn it off when we detect
 * the first problem because we dont want to recurse back
 * into the tracing code when doing error printk or
 * executing a BUG():
 */
int rt_trace_on = 1;

void deadlock_trace_off(void)
{
	rt_trace_on = 0;
}

static void printk_task(task_t *p)
{
	if (p)
		printk("%16s:%5d [%p, %3d]", p->comm, p->pid, p, p->prio);
	else
		printk("<none>");
}

static void printk_task_short(task_t *p)
{
	if (p)
		printk("%s/%d [%p, %3d]", p->comm, p->pid, p, p->prio);
	else
		printk("<none>");
}

static void printk_lock(struct rt_mutex *lock, int print_owner)
{
	if (lock->name)
		printk(" [%p] {%s}\n",
			lock, lock->name);
	else
		printk(" [%p] {%s:%d}\n",
			lock, lock->file, lock->line);

	if (print_owner && rt_mutex_owner(lock)) {
		printk(".. ->owner: %p\n", lock->owner);
		printk(".. held by:  ");
		printk_task(rt_mutex_owner(lock));
		printk("\n");
	}
	if (rt_mutex_owner(lock)) {
		printk("... acquired at:               ");
		print_symbol("%s\n", lock->acquire_ip);
	}
}

static void printk_waiter(struct rt_mutex_waiter *w)
{
	printk("-------------------------\n");
	printk("| waiter struct %p:\n", w);
	printk("| w->list_entry: [DP:%p/%p|SP:%p/%p|PRI:%d]\n",
	       w->list_entry.plist.prio_list.prev, w->list_entry.plist.prio_list.next,
	       w->list_entry.plist.node_list.prev, w->list_entry.plist.node_list.next,
	       w->list_entry.prio);
	printk("| w->pi_list_entry: [DP:%p/%p|SP:%p/%p|PRI:%d]\n",
	       w->pi_list_entry.plist.prio_list.prev, w->pi_list_entry.plist.prio_list.next,
	       w->pi_list_entry.plist.node_list.prev, w->pi_list_entry.plist.node_list.next,
	       w->pi_list_entry.prio);
	printk("\n| lock:\n");
	printk_lock(w->lock, 1);
	printk("| w->ti->task:\n");
	printk_task(w->task);
	printk("| blocked at:  ");
	print_symbol("%s\n", w->ip);
	printk("-------------------------\n");
}

static void show_task_locks(task_t *p)
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
	if (p->pi_blocked_on) {
		struct rt_mutex *lock = p->pi_blocked_on->lock;

		printk(" blocked on:");
		printk_lock(lock, 1);
	} else
		printk(" (not blocked)\n");
}

void rt_mutex_show_held_locks(task_t *task, int verbose)
{
	struct list_head *curr, *cursor = NULL;
	struct rt_mutex *lock;
	task_t *t;
	unsigned long flags;
	int count = 0;

	if (!rt_trace_on)
		return;

	if (verbose) {
		printk("------------------------------\n");
		printk("| showing all locks held by: |  (");
		printk_task_short(task);
		printk("):\n");
		printk("------------------------------\n");
	}

next:
	spin_lock_irqsave(&task->held_list_lock, flags);
	list_for_each(curr, &task->held_list_head) {
		if (cursor && curr != cursor)
			continue;
		lock = list_entry(curr, struct rt_mutex, held_list_entry);
		t = rt_mutex_owner(lock);
		WARN_ON(t != task);
		count++;
		cursor = curr->next;
		spin_unlock_irqrestore(&task->held_list_lock, flags);

		printk("\n#%03d:            ", count);
		printk_lock(lock, 0);
		goto next;
	}
	spin_unlock_irqrestore(&task->held_list_lock, flags);

	printk("\n");
}

void rt_mutex_show_all_locks(void)
{
	task_t *g, *p;
	int count = 10;
	int unlock = 1;

	printk("\n");
	printk("----------------------\n");
	printk("| showing all tasks: |\n");
	printk("----------------------\n");

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

	printk("-----------------------------------------\n");
	printk("| showing all locks held in the system: |\n");
	printk("-----------------------------------------\n");

	do_each_thread(g, p) {
		rt_mutex_show_held_locks(p, 0);
		if (!unlock)
			if (read_trylock(&tasklist_lock))
				unlock = 1;
	} while_each_thread(g, p);


	printk("=============================================\n\n");

	if (unlock)
		read_unlock(&tasklist_lock);
}

void rt_mutex_debug_check_no_locks_held(task_t *task)
{
	struct rt_mutex_waiter *w;
	struct list_head *curr;
	struct rt_mutex *lock;

	if (!rt_trace_on)
		return;
	if (!rt_prio(task->normal_prio) && rt_prio(task->prio)) {
		printk("BUG: PI priority boost leaked!\n");
		printk_task(task);
		printk("\n");
	}
	if (list_empty(&task->held_list_head))
		return;

	spin_lock(&task->pi_lock);
	plist_for_each_entry(w, &task->pi_waiters, pi_list_entry) {
		TRACE_OFF();

		printk("hm, PI interest held at exit time? Task:\n");
		printk_task(task);
		printk_waiter(w);
		return;
	}
	spin_unlock(&task->pi_lock);

	list_for_each(curr, &task->held_list_head) {
		lock = list_entry(curr, struct rt_mutex, held_list_entry);

		printk("BUG: %s/%d, lock held at task exit time!\n",
		       task->comm, task->pid);
		printk_lock(lock, 1);
		if (rt_mutex_owner(lock) != task)
			printk("exiting task is not even the owner??\n");
	}
}

int rt_mutex_debug_check_no_locks_freed(const void *from, unsigned long len)
{
	const void *to = from + len;
	struct list_head *curr;
	struct rt_mutex *lock;
	unsigned long flags;
	void *lock_addr;

	if (!rt_trace_on)
		return 0;

	spin_lock_irqsave(&current->held_list_lock, flags);
	list_for_each(curr, &current->held_list_head) {
		lock = list_entry(curr, struct rt_mutex, held_list_entry);
		lock_addr = lock;
		if (lock_addr < from || lock_addr >= to)
			continue;
		TRACE_OFF();

		printk("BUG: %s/%d, active lock [%p(%p-%p)] freed!\n",
			current->comm, current->pid, lock, from, to);
		dump_stack();
		printk_lock(lock, 1);
		if (rt_mutex_owner(lock) != current)
			printk("freeing task is not even the owner??\n");
		return 1;
	}
	spin_unlock_irqrestore(&current->held_list_lock, flags);

	return 0;
}

void rt_mutex_debug_task_free(struct task_struct *task)
{
	WARN_ON(!plist_head_empty(&task->pi_waiters));
	WARN_ON(task->pi_blocked_on);
}

/*
 * We fill out the fields in the waiter to store the information about
 * the deadlock. We print when we return. act_waiter can be NULL in
 * case of a remove waiter operation.
 */
void debug_rt_mutex_deadlock(int detect, struct rt_mutex_waiter *act_waiter,
			     struct rt_mutex *lock)
{
	struct task_struct *task;

	if (!rt_trace_on || detect || !act_waiter)
		return;

	task = rt_mutex_owner(act_waiter->lock);
	if (task && task != current) {
		act_waiter->deadlock_task_pid = task->pid;
		act_waiter->deadlock_lock = lock;
	}
}

void debug_rt_mutex_print_deadlock(struct rt_mutex_waiter *waiter)
{
	struct task_struct *task;

	if (!waiter->deadlock_lock || !rt_trace_on)
		return;

	task = find_task_by_pid(waiter->deadlock_task_pid);
	if (!task)
		return;

	TRACE_OFF_NOLOCK();

	printk("\n============================================\n");
	printk(  "[ BUG: circular locking deadlock detected! ]\n");
	printk(  "--------------------------------------------\n");
	printk("%s/%d is deadlocking current task %s/%d\n\n",
	       task->comm, task->pid, current->comm, current->pid);

	printk("\n1) %s/%d is trying to acquire this lock:\n",
	       current->comm, current->pid);
	printk_lock(waiter->lock, 1);

	printk("... trying at:                 ");
	print_symbol("%s\n", waiter->ip);

	printk("\n2) %s/%d is blocked on this lock:\n", task->comm, task->pid);
	printk_lock(waiter->deadlock_lock, 1);

	rt_mutex_show_held_locks(current, 1);
	rt_mutex_show_held_locks(task, 1);

	printk("\n%s/%d's [blocked] stackdump:\n\n", task->comm, task->pid);
	show_stack(task, NULL);
	printk("\n%s/%d's [current] stackdump:\n\n",
	       current->comm, current->pid);
	dump_stack();
	rt_mutex_show_all_locks();
	printk("[ turning off deadlock detection."
	       "Please report this trace. ]\n\n");
	local_irq_disable();
}

void debug_rt_mutex_lock(struct rt_mutex *lock __IP_DECL__)
{
	unsigned long flags;

	if (rt_trace_on) {
		TRACE_WARN_ON_LOCKED(!list_empty(&lock->held_list_entry));

		spin_lock_irqsave(&current->held_list_lock, flags);
		list_add_tail(&lock->held_list_entry, &current->held_list_head);
		spin_unlock_irqrestore(&current->held_list_lock, flags);

		lock->acquire_ip = ip;
	}
}

void debug_rt_mutex_unlock(struct rt_mutex *lock)
{
	unsigned long flags;

	if (rt_trace_on) {
		TRACE_WARN_ON_LOCKED(rt_mutex_owner(lock) != current);
		TRACE_WARN_ON_LOCKED(list_empty(&lock->held_list_entry));

		spin_lock_irqsave(&current->held_list_lock, flags);
		list_del_init(&lock->held_list_entry);
		spin_unlock_irqrestore(&current->held_list_lock, flags);
	}
}

void debug_rt_mutex_proxy_lock(struct rt_mutex *lock,
			       struct task_struct *powner __IP_DECL__)
{
	unsigned long flags;

	if (rt_trace_on) {
		TRACE_WARN_ON_LOCKED(!list_empty(&lock->held_list_entry));

		spin_lock_irqsave(&powner->held_list_lock, flags);
		list_add_tail(&lock->held_list_entry, &powner->held_list_head);
		spin_unlock_irqrestore(&powner->held_list_lock, flags);

		lock->acquire_ip = ip;
	}
}

void debug_rt_mutex_proxy_unlock(struct rt_mutex *lock)
{
	unsigned long flags;

	if (rt_trace_on) {
		struct task_struct *owner = rt_mutex_owner(lock);

		TRACE_WARN_ON_LOCKED(!owner);
		TRACE_WARN_ON_LOCKED(list_empty(&lock->held_list_entry));

		spin_lock_irqsave(&owner->held_list_lock, flags);
		list_del_init(&lock->held_list_entry);
		spin_unlock_irqrestore(&owner->held_list_lock, flags);
	}
}

void debug_rt_mutex_init_waiter(struct rt_mutex_waiter *waiter)
{
	memset(waiter, 0x11, sizeof(*waiter));
	plist_node_init(&waiter->list_entry, MAX_PRIO);
	plist_node_init(&waiter->pi_list_entry, MAX_PRIO);
}

void debug_rt_mutex_free_waiter(struct rt_mutex_waiter *waiter)
{
	TRACE_WARN_ON(!plist_node_empty(&waiter->list_entry));
	TRACE_WARN_ON(!plist_node_empty(&waiter->pi_list_entry));
	TRACE_WARN_ON(waiter->task);
	memset(waiter, 0x22, sizeof(*waiter));
}

void debug_rt_mutex_init(struct rt_mutex *lock, const char *name)
{
	void *addr = lock;

	if (rt_trace_on) {
		rt_mutex_debug_check_no_locks_freed(addr,
						    sizeof(struct rt_mutex));
		INIT_LIST_HEAD(&lock->held_list_entry);
		lock->name = name;
	}
}

void rt_mutex_deadlock_account_lock(struct rt_mutex *lock, task_t *task)
{
}

void rt_mutex_deadlock_account_unlock(struct task_struct *task)
{
}

