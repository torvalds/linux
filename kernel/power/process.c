/*
 * drivers/power/process.c - Functions for starting/stopping processes on 
 *                           suspend transitions.
 *
 * Originally from swsusp.
 */


#undef DEBUG

#include <linux/smp_lock.h>
#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/freezer.h>

/* 
 * Timeout for stopping processes
 */
#define TIMEOUT	(20 * HZ)


static inline int freezeable(struct task_struct * p)
{
	if ((p == current) || 
	    (p->flags & PF_NOFREEZE) ||
	    (p->exit_state == EXIT_ZOMBIE) ||
	    (p->exit_state == EXIT_DEAD) ||
	    (p->state == TASK_STOPPED))
		return 0;
	return 1;
}

/* Refrigerator is place where frozen processes are stored :-). */
void refrigerator(void)
{
	/* Hmm, should we be allowed to suspend when there are realtime
	   processes around? */
	long save;
	save = current->state;
	pr_debug("%s entered refrigerator\n", current->comm);
	printk("=");

	frozen_process(current);
	spin_lock_irq(&current->sighand->siglock);
	recalc_sigpending(); /* We sent fake signal, clean it up */
	spin_unlock_irq(&current->sighand->siglock);

	while (frozen(current)) {
		current->state = TASK_UNINTERRUPTIBLE;
		schedule();
	}
	pr_debug("%s left refrigerator\n", current->comm);
	current->state = save;
}

static inline void freeze_process(struct task_struct *p)
{
	unsigned long flags;

	if (!freezing(p)) {
		freeze(p);
		spin_lock_irqsave(&p->sighand->siglock, flags);
		signal_wake_up(p, 0);
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
	}
}

static void cancel_freezing(struct task_struct *p)
{
	unsigned long flags;

	if (freezing(p)) {
		pr_debug("  clean up: %s\n", p->comm);
		do_not_freeze(p);
		spin_lock_irqsave(&p->sighand->siglock, flags);
		recalc_sigpending_tsk(p);
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
	}
}

/* 0 = success, else # of processes that we failed to stop */
int freeze_processes(void)
{
	int todo, nr_user, user_frozen;
	unsigned long start_time;
	struct task_struct *g, *p;

	printk( "Stopping tasks: " );
	start_time = jiffies;
	user_frozen = 0;
	do {
		nr_user = todo = 0;
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			if (!freezeable(p))
				continue;
			if (frozen(p))
				continue;
			if (p->state == TASK_TRACED && frozen(p->parent)) {
				cancel_freezing(p);
				continue;
			}
			if (p->mm && !(p->flags & PF_BORROWED_MM)) {
				/* The task is a user-space one.
				 * Freeze it unless there's a vfork completion
				 * pending
				 */
				if (!p->vfork_done)
					freeze_process(p);
				nr_user++;
			} else {
				/* Freeze only if the user space is frozen */
				if (user_frozen)
					freeze_process(p);
				todo++;
			}
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
		todo += nr_user;
		if (!user_frozen && !nr_user) {
			sys_sync();
			start_time = jiffies;
		}
		user_frozen = !nr_user;
		yield();			/* Yield is okay here */
		if (todo && time_after(jiffies, start_time + TIMEOUT))
			break;
	} while(todo);

	/* This does not unfreeze processes that are already frozen
	 * (we have slightly ugly calling convention in that respect,
	 * and caller must call thaw_processes() if something fails),
	 * but it cleans up leftover PF_FREEZE requests.
	 */
	if (todo) {
		printk( "\n" );
		printk(KERN_ERR " stopping tasks timed out "
			"after %d seconds (%d tasks remaining):\n",
			TIMEOUT / HZ, todo);
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			if (freezeable(p) && !frozen(p))
				printk(KERN_ERR "  %s\n", p->comm);
			cancel_freezing(p);
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
		return todo;
	}

	printk( "|\n" );
	BUG_ON(in_atomic());
	return 0;
}

void thaw_processes(void)
{
	struct task_struct *g, *p;

	printk( "Restarting tasks..." );
	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (!freezeable(p))
			continue;
		if (!thaw_process(p))
			printk(KERN_INFO " Strange, %s not stopped\n", p->comm );
	} while_each_thread(g, p);

	read_unlock(&tasklist_lock);
	schedule();
	printk( " done\n" );
}

EXPORT_SYMBOL(refrigerator);
