/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TASK_WORK_H
#define _LINUX_TASK_WORK_H

#include <linux/list.h>
#include <linux/sched.h>

typedef void (*task_work_func_t)(struct callback_head *);

static inline void
init_task_work(struct callback_head *twork, task_work_func_t func)
{
	twork->func = func;
}

enum task_work_notify_mode {
	TWA_NONE,
	TWA_RESUME,
	TWA_SIGNAL,
	TWA_SIGNAL_NO_IPI,
};

static inline bool task_work_pending(struct task_struct *task)
{
	return READ_ONCE(task->task_works);
}

int task_work_add(struct task_struct *task, struct callback_head *twork,
			enum task_work_notify_mode mode);

struct callback_head *task_work_cancel_match(struct task_struct *task,
	bool (*match)(struct callback_head *, void *data), void *data);
struct callback_head *task_work_cancel(struct task_struct *, task_work_func_t);
void task_work_run(void);

static inline void exit_task_work(struct task_struct *task)
{
	task_work_run();
}

#endif	/* _LINUX_TASK_WORK_H */
