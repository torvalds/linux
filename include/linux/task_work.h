#ifndef _LINUX_TASK_WORK_H
#define _LINUX_TASK_WORK_H

#include <linux/list.h>
#include <linux/sched.h>

struct task_work;
typedef void (*task_work_func_t)(struct task_work *);

struct task_work {
	struct hlist_node hlist;
	task_work_func_t func;
	void *data;
};

static inline void
init_task_work(struct task_work *twork, task_work_func_t func, void *data)
{
	twork->func = func;
	twork->data = data;
}

int task_work_add(struct task_struct *task, struct task_work *twork, bool);
struct task_work *task_work_cancel(struct task_struct *, task_work_func_t);
void task_work_run(void);

static inline void exit_task_work(struct task_struct *task)
{
	if (unlikely(!hlist_empty(&task->task_works)))
		task_work_run();
}

#endif	/* _LINUX_TASK_WORK_H */
