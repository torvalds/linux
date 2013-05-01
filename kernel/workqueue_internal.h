/*
 * kernel/workqueue_internal.h
 *
 * Workqueue internal header file.  Only to be included by workqueue and
 * core kernel subsystems.
 */
#ifndef _KERNEL_WORKQUEUE_INTERNAL_H
#define _KERNEL_WORKQUEUE_INTERNAL_H

#include <linux/workqueue.h>
#include <linux/kthread.h>

struct worker_pool;

/*
 * The poor guys doing the actual heavy lifting.  All on-duty workers are
 * either serving the manager role, on idle list or on busy hash.  For
 * details on the locking annotation (L, I, X...), refer to workqueue.c.
 *
 * Only to be used in workqueue and async.
 */
struct worker {
	/* on idle list while idle, on busy hash table while busy */
	union {
		struct list_head	entry;	/* L: while idle */
		struct hlist_node	hentry;	/* L: while busy */
	};

	struct work_struct	*current_work;	/* L: work being processed */
	work_func_t		current_func;	/* L: current_work's fn */
	struct pool_workqueue	*current_pwq; /* L: current_work's pwq */
	struct list_head	scheduled;	/* L: scheduled works */
	struct task_struct	*task;		/* I: worker task */
	struct worker_pool	*pool;		/* I: the associated pool */
	/* 64 bytes boundary on 64bit, 32 on 32bit */
	unsigned long		last_active;	/* L: last active timestamp */
	unsigned int		flags;		/* X: flags */
	int			id;		/* I: worker id */

	/* for rebinding worker to CPU */
	struct work_struct	rebind_work;	/* L: for busy worker */

	/* used only by rescuers to point to the target workqueue */
	struct workqueue_struct	*rescue_wq;	/* I: the workqueue to rescue */
};

/**
 * current_wq_worker - return struct worker if %current is a workqueue worker
 */
static inline struct worker *current_wq_worker(void)
{
	if (current->flags & PF_WQ_WORKER)
		return kthread_data(current);
	return NULL;
}

/*
 * Scheduler hooks for concurrency managed workqueue.  Only to be used from
 * sched.c and workqueue.c.
 */
void wq_worker_waking_up(struct task_struct *task, unsigned int cpu);
struct task_struct *wq_worker_sleeping(struct task_struct *task,
				       unsigned int cpu);

#endif /* _KERNEL_WORKQUEUE_INTERNAL_H */
