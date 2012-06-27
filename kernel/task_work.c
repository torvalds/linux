#include <linux/spinlock.h>
#include <linux/task_work.h>
#include <linux/tracehook.h>

int
task_work_add(struct task_struct *task, struct callback_head *twork, bool notify)
{
	unsigned long flags;
	int err = -ESRCH;

#ifndef TIF_NOTIFY_RESUME
	if (notify)
		return -ENOTSUPP;
#endif
	/*
	 * We must not insert the new work if the task has already passed
	 * exit_task_work(). We rely on do_exit()->raw_spin_unlock_wait()
	 * and check PF_EXITING under pi_lock.
	 */
	raw_spin_lock_irqsave(&task->pi_lock, flags);
	if (likely(!(task->flags & PF_EXITING))) {
		struct callback_head *last = task->task_works;
		struct callback_head *first = last ? last->next : twork;
		twork->next = first;
		if (last)
			last->next = twork;
		task->task_works = twork;
		err = 0;
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);

	/* test_and_set_bit() implies mb(), see tracehook_notify_resume(). */
	if (likely(!err) && notify)
		set_notify_resume(task);
	return err;
}

struct callback_head *
task_work_cancel(struct task_struct *task, task_work_func_t func)
{
	unsigned long flags;
	struct callback_head *last, *res = NULL;

	raw_spin_lock_irqsave(&task->pi_lock, flags);
	last = task->task_works;
	if (last) {
		struct callback_head *q = last, *p = q->next;
		while (1) {
			if (p->func == func) {
				q->next = p->next;
				if (p == last)
					task->task_works = q == p ? NULL : q;
				res = p;
				break;
			}
			if (p == last)
				break;
			q = p;
			p = q->next;
		}
	}
	raw_spin_unlock_irqrestore(&task->pi_lock, flags);
	return res;
}

void task_work_run(void)
{
	struct task_struct *task = current;
	struct callback_head *p, *q;

	raw_spin_lock_irq(&task->pi_lock);
	p = task->task_works;
	task->task_works = NULL;
	raw_spin_unlock_irq(&task->pi_lock);

	if (unlikely(!p))
		return;

	q = p->next; /* head */
	p->next = NULL; /* cut it */
	while (q) {
		p = q->next;
		q->func(q);
		q = p;
	}
}
