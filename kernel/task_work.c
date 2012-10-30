#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/tracehook.h>

static struct callback_head work_exited; /* all we need is ->next == NULL */

int
task_work_add(struct task_struct *task, struct callback_head *work, bool notify)
{
	struct callback_head *head;

	do {
		head = ACCESS_ONCE(task->task_works);
		if (unlikely(head == &work_exited))
			return -ESRCH;
		work->next = head;
	} while (cmpxchg(&task->task_works, head, work) != head);

	if (notify)
		set_notify_resume(task);
	return 0;
}

struct callback_head *
task_work_cancel(struct task_struct *task, task_work_func_t func)
{
	struct callback_head **pprev = &task->task_works;
	struct callback_head *work = NULL;
	unsigned long flags;
	/*
	 * If cmpxchg() fails we continue without updating pprev.
	 * Either we raced with task_work_add() which added the
	 * new entry before this work, we will find it again. Or
	 * we raced with task_work_run(), *pprev == NULL/exited.
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	while ((work = ACCESS_ONCE(*pprev))) {
		read_barrier_depends();
		if (work->func != func)
			pprev = &work->next;
		else if (cmpxchg(pprev, work, work->next) == work)
			break;
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	return work;
}

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
			work = ACCESS_ONCE(task->task_works);
			head = !work && (task->flags & PF_EXITING) ?
				&work_exited : NULL;
		} while (cmpxchg(&task->task_works, work, head) != work);

		if (!work)
			break;
		/*
		 * Synchronize with task_work_cancel(). It can't remove
		 * the first entry == work, cmpxchg(task_works) should
		 * fail, but it can play with *work and other entries.
		 */
		raw_spin_unlock_wait(&task->pi_lock);
		smp_mb();

		/* Reverse the list to run the works in fifo order */
		head = NULL;
		do {
			next = work->next;
			work->next = head;
			head = work;
			work = next;
		} while (work);

		work = head;
		do {
			next = work->next;
			work->func(work);
			work = next;
			cond_resched();
		} while (work);
	}
}
