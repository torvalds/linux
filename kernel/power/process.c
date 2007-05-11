/*
 * drivers/power/process.c - Functions for starting/stopping processes on 
 *                           suspend transitions.
 *
 * Originally from swsusp.
 */


#undef DEBUG

#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/freezer.h>

/* 
 * Timeout for stopping processes
 */
#define TIMEOUT	(20 * HZ)

#define FREEZER_KERNEL_THREADS 0
#define FREEZER_USER_SPACE 1

static inline int freezeable(struct task_struct * p)
{
	if ((p == current) ||
	    (p->flags & PF_NOFREEZE) ||
	    (p->exit_state != 0))
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

	frozen_process(current);
	spin_lock_irq(&current->sighand->siglock);
	recalc_sigpending(); /* We sent fake signal, clean it up */
	spin_unlock_irq(&current->sighand->siglock);

	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!frozen(current))
			break;
		schedule();
	}
	pr_debug("%s left refrigerator\n", current->comm);
	current->state = save;
}

static inline void freeze_process(struct task_struct *p)
{
	unsigned long flags;

	if (!freezing(p)) {
		rmb();
		if (!frozen(p)) {
			if (p->state == TASK_STOPPED)
				force_sig_specific(SIGSTOP, p);

			freeze(p);
			spin_lock_irqsave(&p->sighand->siglock, flags);
			signal_wake_up(p, p->state == TASK_STOPPED);
			spin_unlock_irqrestore(&p->sighand->siglock, flags);
		}
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

static inline int is_user_space(struct task_struct *p)
{
	return p->mm && !(p->flags & PF_BORROWED_MM);
}

static unsigned int try_to_freeze_tasks(int freeze_user_space)
{
	struct task_struct *g, *p;
	unsigned long end_time;
	unsigned int todo;

	end_time = jiffies + TIMEOUT;
	do {
		todo = 0;
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
			if (is_user_space(p)) {
				if (!freeze_user_space)
					continue;

				/* Freeze the task unless there is a vfork
				 * completion pending
				 */
				if (!p->vfork_done)
					freeze_process(p);
			} else {
				if (freeze_user_space)
					continue;

				freeze_process(p);
			}
			todo++;
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
		yield();			/* Yield is okay here */
		if (todo && time_after(jiffies, end_time))
			break;
	} while (todo);

	if (todo) {
		/* This does not unfreeze processes that are already frozen
		 * (we have slightly ugly calling convention in that respect,
		 * and caller must call thaw_processes() if something fails),
		 * but it cleans up leftover PF_FREEZE requests.
		 */
		printk("\n");
		printk(KERN_ERR "Stopping %s timed out after %d seconds "
				"(%d tasks refusing to freeze):\n",
				freeze_user_space ? "user space processes" :
					"kernel threads",
				TIMEOUT / HZ, todo);
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			if (is_user_space(p) == !freeze_user_space)
				continue;

			if (freezeable(p) && !frozen(p))
				printk(KERN_ERR " %s\n", p->comm);

			cancel_freezing(p);
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
	}

	return todo;
}

/**
 *	freeze_processes - tell processes to enter the refrigerator
 *
 *	Returns 0 on success, or the number of processes that didn't freeze,
 *	although they were told to.
 */
int freeze_processes(void)
{
	unsigned int nr_unfrozen;

	printk("Stopping tasks ... ");
	nr_unfrozen = try_to_freeze_tasks(FREEZER_USER_SPACE);
	if (nr_unfrozen)
		return nr_unfrozen;

	sys_sync();
	nr_unfrozen = try_to_freeze_tasks(FREEZER_KERNEL_THREADS);
	if (nr_unfrozen)
		return nr_unfrozen;

	printk("done.\n");
	BUG_ON(in_atomic());
	return 0;
}

static void thaw_tasks(int thaw_user_space)
{
	struct task_struct *g, *p;

	read_lock(&tasklist_lock);
	do_each_thread(g, p) {
		if (!freezeable(p))
			continue;

		if (is_user_space(p) == !thaw_user_space)
			continue;

		if (!thaw_process(p))
			printk(KERN_WARNING " Strange, %s not stopped\n",
				p->comm );
	} while_each_thread(g, p);
	read_unlock(&tasklist_lock);
}

void thaw_processes(void)
{
	printk("Restarting tasks ... ");
	thaw_tasks(FREEZER_KERNEL_THREADS);
	thaw_tasks(FREEZER_USER_SPACE);
	schedule();
	printk("done.\n");
}

EXPORT_SYMBOL(refrigerator);
