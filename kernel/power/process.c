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

static void fake_signal_wake_up(struct task_struct *p, int resume)
{
	unsigned long flags;

	spin_lock_irqsave(&p->sighand->siglock, flags);
	signal_wake_up(p, resume);
	spin_unlock_irqrestore(&p->sighand->siglock, flags);
}

static void send_fake_signal(struct task_struct *p)
{
	if (task_is_stopped(p))
		force_sig_specific(SIGSTOP, p);
	fake_signal_wake_up(p, task_is_stopped(p));
}

static int has_mm(struct task_struct *p)
{
	return (p->mm && !(p->flags & PF_BORROWED_MM));
}

/**
 *	freeze_task - send a freeze request to given task
 *	@p: task to send the request to
 *	@with_mm_only: if set, the request will only be sent if the task has its
 *		own mm
 *	Return value: 0, if @with_mm_only is set and the task has no mm of its
 *		own or the task is frozen, 1, otherwise
 *
 *	The freeze request is sent by seting the tasks's TIF_FREEZE flag and
 *	either sending a fake signal to it or waking it up, depending on whether
 *	or not it has its own mm (ie. it is a user land task).  If @with_mm_only
 *	is set and the task has no mm of its own (ie. it is a kernel thread),
 *	its TIF_FREEZE flag should not be set.
 *
 *	The task_lock() is necessary to prevent races with exit_mm() or
 *	use_mm()/unuse_mm() from occuring.
 */
static int freeze_task(struct task_struct *p, int with_mm_only)
{
	int ret = 1;

	task_lock(p);
	if (freezing(p)) {
		if (has_mm(p)) {
			if (!signal_pending(p))
				fake_signal_wake_up(p, 0);
		} else {
			if (with_mm_only)
				ret = 0;
			else
				wake_up_state(p, TASK_INTERRUPTIBLE);
		}
	} else {
		rmb();
		if (frozen(p)) {
			ret = 0;
		} else {
			if (has_mm(p)) {
				set_freeze_flag(p);
				send_fake_signal(p);
			} else {
				if (with_mm_only) {
					ret = 0;
				} else {
					set_freeze_flag(p);
					wake_up_state(p, TASK_INTERRUPTIBLE);
				}
			}
		}
	}
	task_unlock(p);
	return ret;
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
	struct timeval start, end;
	s64 elapsed_csecs64;
	unsigned int elapsed_csecs;

	do_gettimeofday(&start);

	end_time = jiffies + TIMEOUT;
	do {
		todo = 0;
		read_lock(&tasklist_lock);
		do_each_thread(g, p) {
			if (frozen(p) || !freezeable(p))
				continue;

			if (task_is_traced(p) && frozen(p->parent)) {
				cancel_freezing(p);
				continue;
			}

			if (!freeze_task(p, freeze_user_space))
				continue;

			if (!freezer_should_skip(p))
				todo++;
		} while_each_thread(g, p);
		read_unlock(&tasklist_lock);
		yield();			/* Yield is okay here */
		if (time_after(jiffies, end_time))
			break;
	} while (todo);

	do_gettimeofday(&end);
	elapsed_csecs64 = timeval_to_ns(&end) - timeval_to_ns(&start);
	do_div(elapsed_csecs64, NSEC_PER_SEC / 100);
	elapsed_csecs = elapsed_csecs64;

	if (todo) {
		/* This does not unfreeze processes that are already frozen
		 * (we have slightly ugly calling convention in that respect,
		 * and caller must call thaw_processes() if something fails),
		 * but it cleans up leftover PF_FREEZE requests.
		 */
		printk("\n");
		printk(KERN_ERR "Freezing of tasks failed after %d.%02d seconds "
				"(%d tasks refusing to freeze):\n",
				elapsed_csecs / 100, elapsed_csecs % 100, todo);
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
	} else {
		printk("(elapsed %d.%02d seconds) ", elapsed_csecs / 100,
			elapsed_csecs % 100);
	}

	return todo ? -EBUSY : 0;
}

/**
 *	freeze_processes - tell processes to enter the refrigerator
 */
int freeze_processes(void)
{
	int error;

	printk("Freezing user space processes ... ");
	error = try_to_freeze_tasks(FREEZER_USER_SPACE);
	if (error)
		goto Exit;
	printk("done.\n");

	printk("Freezing remaining freezable tasks ... ");
	error = try_to_freeze_tasks(FREEZER_KERNEL_THREADS);
	if (error)
		goto Exit;
	printk("done.");
 Exit:
	BUG_ON(in_atomic());
	printk("\n");
	return error;
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
