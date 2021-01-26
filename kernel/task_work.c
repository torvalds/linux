// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/tracehook.h>

static struct callback_head work_exited; /* all we need is ->next == NULL */

/**
 * task_work_add - ask the @task to execute @work->func()
 * @task: the task which should run the callback
 * @work: the callback to run
 * @notify: send the notification if true
 *
 * Queue @work for task_work_run() below and notify the @task if @notify.
 * Fails if the @task is exiting/exited and thus it can't process this @work.
 * Otherwise @work->func() will be called when the @task returns from kernel
 * mode or exits.
 *
 * This is like the signal handler which runs in kernel mode, but it doesn't
 * try to wake up the @task.
 *
 * Note: there is no ordering guarantee on works queued here.
 *
 * RETURNS:
 * 0 if succeeds or -ESRCH.
 */
int
task_work_add(struct task_struct *task, struct callback_head *work, int notify)
{
	struct callback_head *head;
	unsigned long flags;

	do {
		head = READ_ONCE(task->task_works);
		if (unlikely(head == &work_exited))
			return -ESRCH;
		work->next = head;
	} while (cmpxchg(&task->task_works, head, work) != head);

	switch (notify) {
	case TWA_RESUME:
		set_notify_resume(task);
		break;
	case TWA_SIGNAL:
		/*
		 * Only grab the sighand lock if we don't already have some
		 * task_work pending. This pairs with the smp_store_mb()
		 * in get_signal(), see comment there.
		 */
		if (!(READ_ONCE(task->jobctl) & JOBCTL_TASK_WORK) &&
		    lock_task_sighand(task, &flags)) {
			task->jobctl |= JOBCTL_TASK_WORK;
			signal_wake_up(task, 0);
			unlock_task_sighand(task, &flags);
		}
		break;
	}

	return 0;
}

/**
 * task_work_cancel - cancel a pending work added by task_work_add()
 * @task: the task which should execute the work
 * @func: identifies the work to remove
 *
 * Find the last queued pending work with ->func == @func and remove
 * it from queue.
 *
 * RETURNS:
 * The found work or NULL if not found.
 */
struct callback_head *
task_work_cancel(struct task_struct *task, task_work_func_t func)
{
	struct callback_head **pprev = &task->task_works;
	struct callback_head *work;
	unsigned long flags;

	if (likely(!task->task_works))
		return NULL;
	/*
	 * If cmpxchg() fails we continue without updating pprev.
	 * Either we raced with task_work_add() which added the
	 * new entry before this work, we will find it again. Or
	 * we raced with task_work_run(), *pprev == NULL/exited.
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	while ((work = READ_ONCE(*pprev))) {
		if (work->func != func)
			pprev = &work->next;
		else if (cmpxchg(pprev, work, work->next) == work)
			break;
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	return work;
}

/**
 * task_work_run - execute the works added by task_work_add()
 *
 * Flush the pending works. Should be used by the core kernel code.
 * Called before the task returns to the user-mode or stops, or when
 * it exits. In the latter case task_work_add() can no longer add the
 * new work after task_work_run() returns.
 */
void task_work_run(void)
{
	struct task_struct *task = current;
	struct callback_head *work, *head, *next;

	for (;;) {
		/*
		 * work->func() can do task_work_add(), do not set
		 * work_exited unless the list is empty.
		 */
		do {
			head = NULL;
			work = READ_ONCE(task->task_works);
			if (!work) {
				if (task->flags & PF_EXITING)
					head = &work_exited;
				else
					break;
			}
		} while (cmpxchg(&task->task_works, work, head) != work);

		if (!work)
			break;
		/*
		 * Synchronize with task_work_cancel(). It can not remove
		 * the first entry == work, cmpxchg(task_works) must fail.
		 * But it can remove another entry from the ->next list.
		 */
		raw_spin_lock_irq(&task->pi_lock);
		raw_spin_unlock_irq(&task->pi_lock);

		do {
			next = work->next;
			work->func(work);
			work = next;
			cond_resched();
		} while (work);
	}
}
