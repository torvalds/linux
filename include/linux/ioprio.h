/* SPDX-License-Identifier: GPL-2.0 */
#ifndef IOPRIO_H
#define IOPRIO_H

#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/iocontext.h>

#include <uapi/linux/ioprio.h>

/*
 * Default IO priority.
 */
#define IOPRIO_DEFAULT	IOPRIO_PRIO_VALUE(IOPRIO_CLASS_NONE, 0)

/*
 * Check that a priority value has a valid class.
 */
static inline bool ioprio_valid(unsigned short ioprio)
{
	unsigned short class = IOPRIO_PRIO_CLASS(ioprio);

	return class > IOPRIO_CLASS_NONE && class <= IOPRIO_CLASS_IDLE;
}

/*
 * if process has set io priority explicitly, use that. if not, convert
 * the cpu scheduler nice value to an io priority
 */
static inline int task_nice_ioprio(struct task_struct *task)
{
	return (task_nice(task) + 20) / 5;
}

/*
 * This is for the case where the task hasn't asked for a specific IO class.
 * Check for idle and rt task process, and return appropriate IO class.
 */
static inline int task_nice_ioclass(struct task_struct *task)
{
	if (task->policy == SCHED_IDLE)
		return IOPRIO_CLASS_IDLE;
	else if (task_is_realtime(task))
		return IOPRIO_CLASS_RT;
	else
		return IOPRIO_CLASS_BE;
}

#ifdef CONFIG_BLOCK
/*
 * If the task has set an I/O priority, use that. Otherwise, return
 * the default I/O priority.
 *
 * Expected to be called for current task or with task_lock() held to keep
 * io_context stable.
 */
static inline int __get_task_ioprio(struct task_struct *p)
{
	struct io_context *ioc = p->io_context;
	int prio;

	if (!ioc)
		return IOPRIO_DEFAULT;

	if (p != current)
		lockdep_assert_held(&p->alloc_lock);

	prio = ioc->ioprio;
	if (IOPRIO_PRIO_CLASS(prio) == IOPRIO_CLASS_NONE)
		prio = IOPRIO_PRIO_VALUE(task_nice_ioclass(p),
					 task_nice_ioprio(p));
	return prio;
}
#else
static inline int __get_task_ioprio(struct task_struct *p)
{
	return IOPRIO_DEFAULT;
}
#endif /* CONFIG_BLOCK */

static inline int get_current_ioprio(void)
{
	return __get_task_ioprio(current);
}

extern int set_task_ioprio(struct task_struct *task, int ioprio);

#ifdef CONFIG_BLOCK
extern int ioprio_check_cap(int ioprio);
#else
static inline int ioprio_check_cap(int ioprio)
{
	return -ENOTBLK;
}
#endif /* CONFIG_BLOCK */

#endif
