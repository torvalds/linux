/*
 * kernel/freezer.c - Function to freeze a process
 *
 * Originally from kernel/power/process.c
 */

#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/export.h>
#include <linux/syscalls.h>
#include <linux/freezer.h>

/*
 * freezing is complete, mark current process as frozen
 */
static inline void frozen_process(void)
{
	if (!unlikely(current->flags & PF_NOFREEZE)) {
		current->flags |= PF_FROZEN;
		smp_wmb();
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

	/* prevent accounting of that task to load */
	current->flags |= PF_FREEZING;

	for (;;) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!frozen(current))
			break;
		schedule();
	}

	/* Remove the accounting blocker */
	current->flags &= ~PF_FREEZING;

	pr_debug("%s left refrigerator\n", current->comm);

	/*
	 * Restore saved task state before returning.  The mb'd version
	 * needs to be used; otherwise, it might silently break
	 * synchronization which depends on ordered task state change.
	 */
	set_current_state(save);
}
EXPORT_SYMBOL(refrigerator);

static void fake_signal_wake_up(struct task_struct *p)
{
	unsigned long flags;

	spin_lock_irqsave(&p->sighand->siglock, flags);
	signal_wake_up(p, 0);
	spin_unlock_irqrestore(&p->sighand->siglock, flags);
}

/**
 *	freeze_task - send a freeze request to given task
 *	@p: task to send the request to
 *	@sig_only: if set, the request will only be sent if the task has the
 *		PF_FREEZER_NOSIG flag unset
 *	Return value: 'false', if @sig_only is set and the task has
 *		PF_FREEZER_NOSIG set or the task is frozen, 'true', otherwise
 *
 *	The freeze request is sent by setting the tasks's TIF_FREEZE flag and
 *	either sending a fake signal to it or waking it up, depending on whether
 *	or not it has PF_FREEZER_NOSIG set.  If @sig_only is set and the task
 *	has PF_FREEZER_NOSIG set (ie. it is a typical kernel thread), its
 *	TIF_FREEZE flag will not be set.
 */
bool freeze_task(struct task_struct *p, bool sig_only)
{
	/*
	 * We first check if the task is freezing and next if it has already
	 * been frozen to avoid the race with frozen_process() which first marks
	 * the task as frozen and next clears its TIF_FREEZE.
	 */
	if (!freezing(p)) {
		smp_rmb();
		if (frozen(p))
			return false;

		if (!sig_only || should_send_signal(p))
			set_freeze_flag(p);
		else
			return false;
	}

	if (should_send_signal(p)) {
		fake_signal_wake_up(p);
		/*
		 * fake_signal_wake_up() goes through p's scheduler
		 * lock and guarantees that TASK_STOPPED/TRACED ->
		 * TASK_RUNNING transition can't race with task state
		 * testing in try_to_freeze_tasks().
		 */
	} else if (sig_only) {
		return false;
	} else {
		wake_up_state(p, TASK_INTERRUPTIBLE);
	}

	return true;
}

void cancel_freezing(struct task_struct *p)
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

static int __thaw_process(struct task_struct *p)
{
	if (frozen(p)) {
		p->flags &= ~PF_FROZEN;
		return 1;
	}
	clear_freeze_flag(p);
	return 0;
}

/*
 * Wake up a frozen process
 *
 * task_lock() is needed to prevent the race with refrigerator() which may
 * occur if the freezing of tasks fails.  Namely, without the lock, if the
 * freezing of tasks failed, thaw_tasks() might have run before a task in
 * refrigerator() could call frozen_process(), in which case the task would be
 * frozen and no one would thaw it.
 */
int thaw_process(struct task_struct *p)
{
	task_lock(p);
	if (__thaw_process(p) == 1) {
		task_unlock(p);
		wake_up_process(p);
		return 1;
	}
	task_unlock(p);
	return 0;
}
EXPORT_SYMBOL(thaw_process);
