// SPDX-License-Identifier: GPL-2.0
#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/resume_user_mode.h>

static struct callback_head work_exited; /* all we need is ->next == NULL */

/**
 * task_work_add - ask the @task to execute @work->func()
 * @task: the task which should run the callback
 * @work: the callback to run
 * @notify: how to notify the targeted task
 *
 * Queue @work for task_work_run() below and notify the @task if @notify
 * is @TWA_RESUME, @TWA_SIGNAL, or @TWA_SIGNAL_NO_IPI.
 *
 * @TWA_SIGNAL works like signals, in that the it will interrupt the targeted
 * task and run the task_work, regardless of whether the task is currently
 * running in the kernel or userspace.
 * @TWA_SIGNAL_NO_IPI works like @TWA_SIGNAL, except it doesn't send a
 * reschedule IPI to force the targeted task to reschedule and run task_work.
 * This can be advantageous if there's no strict requirement that the
 * task_work be run as soon as possible, just whenever the task enters the
 * kernel anyway.
 * @TWA_RESUME work is run only when the task exits the kernel and returns to
 * user mode, or before entering guest mode.
 *
 * Fails if the @task is exiting/exited and thus it can't process this @work.
 * Otherwise @work->func() will be called when the @task goes through one of
 * the aforementioned transitions, or exits.
 *
 * If the targeted task is exiting, then an error is returned and the work item
 * is not queued. It's up to the caller to arrange for an alternative mechanism
 * in that case.
 *
 * Note: there is no ordering guarantee on works queued here. The task_work
 * list is LIFO.
 *
 * RETURNS:
 * 0 if succeeds or -ESRCH.
 */
int task_work_add(struct task_struct *task, struct callback_head *work,
		  enum task_work_notify_mode notify)
{
	struct callback_head *head;

	/* record the work call stack in order to print it in KASAN reports */
	kasan_record_aux_stack(work);

	head = READ_ONCE(task->task_works);
	do {
		if (unlikely(head == &work_exited))
			return -ESRCH;
		work->next = head;
	} while (!try_cmpxchg(&task->task_works, &head, work));

	switch (notify) {
	case TWA_NONE:
		break;
	case TWA_RESUME:
		set_notify_resume(task);
		break;
	case TWA_SIGNAL:
		set_notify_signal(task);
		break;
	case TWA_SIGNAL_NO_IPI:
		__set_notify_signal(task);
		break;
	default:
		WARN_ON_ONCE(1);
		break;
	}

	return 0;
}

/**
 * task_work_cancel_match - cancel a pending work added by task_work_add()
 * @task: the task which should execute the work
 * @match: match function to call
 *
 * RETURNS:
 * The found work or NULL if not found.
 */
struct callback_head *
task_work_cancel_match(struct task_struct *task,
		       bool (*match)(struct callback_head *, void *data),
		       void *data)
{
	struct callback_head **pprev = &task->task_works;
	struct callback_head *work;
	unsigned long flags;

	if (likely(!task_work_pending(task)))
		return NULL;
	/*
	 * If cmpxchg() fails we continue without updating pprev.
	 * Either we raced with task_work_add() which added the
	 * new entry before this work, we will find it again. Or
	 * we raced with task_work_run(), *pprev == NULL/exited.
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	work = READ_ONCE(*pprev);
	while (work) {
		if (!match(work, data)) {
			pprev = &work->next;
			work = READ_ONCE(*pprev);
		} else if (try_cmpxchg(pprev, &work, work->next))
			break;
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	return work;
}

static bool task_work_func_match(struct callback_head *cb, void *data)
{
	return cb->func == data;
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
	return task_work_cancel_match(task, task_work_func_match, func);
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
		work = READ_ONCE(task->task_works);
		do {
			head = NULL;
			if (!work) {
				if (task->flags & PF_EXITING)
					head = &work_exited;
				else
					break;
			}
		} while (!try_cmpxchg(&task->task_works, &work, head));

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
