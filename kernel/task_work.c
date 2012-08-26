#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/tracehook.h>

int
task_work_add(struct task_struct *task, struct callback_head *work, bool notify)
{
	struct callback_head *head;
	/*
	 * Not inserting the new work if the task has already passed
	 * exit_task_work() is the responisbility of callers.
	 */
	do {
		head = ACCESS_ONCE(task->task_works);
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
	 * we raced with task_work_run(), *pprev == NULL.
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
		work = xchg(&task->task_works, NULL);
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
