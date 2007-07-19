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

/*
 * freezing is complete, mark current process as frozen
 */
static inline void frozen_process(void)
{
	if (!unlikely(current->flags & PF_NOFREEZE)) {
		current->flags |= PF_FROZEN;
		wmb();
	}
	clear_freeze_flag(current);
}

/* Refrigerator is place where frozen processes are stored :-). */
void refrigerator(void)
{
	/* Hmm, should we be allowed to suspend when there are realtime
	   processes around? */
	long save;

	task_lock(current);
	if (freezing(current)) {
		frozen_process();
		task_unlock(current);
	} else {
		task_unlock(current);
		return;
	}
	save = current->state;
	pr_debug("%s entered refrigerator\n", current->comm);

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
	__set_current_state(save);
}

static void freeze_task(struct task_struct *p)
{
	unsigned long flags;

	if (!freezing(p)) {
		rmb();
		if (!frozen(p)) {
			set_freeze_flag(p);
			if (p->state == TASK_STOPPED)
				force_sig_specific(SIGSTOP, p);
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
		clear_freeze_flag(p);
		spin_lock_irqsave(&p->sighand->siglock, flags);
		recalc_sigpending_and_wake(p);
		spin_unlock_irqrestore(&p->sighand->siglock, flags);
	}
}

static int try_to_freeze_tasks(int freeze_user_space)
{
	struct task_struct *g, *p;
	unsigned long end_time;
	unsigned int todo;

	end_time = jiffies + TIMEOUT;
	do {
		todo = 0;
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			if (frozen(p) || !freezeable(p))
				continue;

			if (freeze_user_space) {
				if (p->state == TASK_TRACED &&
				    frozen(p->parent)) {
					cancel_freezing(p);
					continue;
				}
				/*
				 * Kernel threads should not have TIF_FREEZE set
				 * at this point, so we must ensure that either
				 * p->mm is not NULL *and* PF_BORROWED_MM is
				 * unset, or TIF_FRREZE is left unset.
				 * The task_lock() is necessary to prevent races
				 * with exit_mm() or use_mm()/unuse_mm() from
				 * occuring.
				 */
				task_lock(p);
				if (!p->mm || (p->flags & PF_BORROWED_MM)) {
					task_unlock(p);
					continue;
				}
				freeze_task(p);
				task_unlock(p);
			} else {
				freeze_task(p);
			}
			if (!freezer_should_skip(p))
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
		printk(KERN_ERR "Freezing of %s timed out after %d seconds "
				"(%d tasks refusing to freeze):\n",
				freeze_user_space ? "user space " : "tasks ",
				TIMEOUT / HZ, todo);
		show_state();
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			task_lock(p);
			if (freezing(p) && !freezer_should_skip(p))
				printk(KERN_ERR " %s\n", p->comm);
			cancel_freezing(p);
			task_unlock(p);
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
	}

	return todo ? -EBUSY : 0;
}

/**
 *	freeze_processes - tell processes to enter the refrigerator
 */
int freeze_processes(void)
{
	int error;

	printk("Stopping tasks ... ");
	error = try_to_freeze_tasks(FREEZER_USER_SPACE);
	if (error)
		return error;

	sys_sync();
	error = try_to_freeze_tasks(FREEZER_KERNEL_THREADS);
	if (error)
		return error;

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

		if (!p->mm == thaw_user_space)
			continue;

		thaw_process(p);
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
