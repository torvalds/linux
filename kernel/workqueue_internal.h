/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/preempt.h>

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

	struct work_struct	*current_work;	/* K: work being processed and its */
	work_func_t		current_func;	/* K: function */
	struct pool_workqueue	*current_pwq;	/* K: pwq */
	u64			current_at;	/* K: runtime at start or last wakeup */
	unsigned int		current_color;	/* K: color */

	int			sleeping;	/* S: is worker sleeping? */

	/* used by the scheduler to determine a worker's last known identity */
	work_func_t		last_func;	/* K: last work's fn */

	struct list_head	scheduled;	/* L: scheduled works */

	struct task_struct	*task;		/* I: worker task */
	struct worker_pool	*pool;		/* A: the associated pool */
						/* L: for rescuers */
	struct list_head	node;		/* A: anchored at pool->workers */
						/* A: runs through worker->node */

	unsigned long		last_active;	/* K: last active timestamp */
	unsigned int		flags;		/* L: flags */
	int			id;		/* I: worker id */

	/*
	 * Opaque string set with work_set_desc().  Printed out with task
	 * dump for debugging - WARN, BUG, panic or sysrq.
	 */
	char			desc[WORKER_DESC_LEN];

	/* used only by rescuers to point to the target workqueue */
	struct workqueue_struct	*rescue_wq;	/* I: the workqueue to rescue */
};

/**
 * current_wq_worker - return struct worker if %current is a workqueue worker
 */
static inline struct worker *current_wq_worker(void)
{
	if (in_task() && (current->flags & PF_WQ_WORKER))
		return kthread_data(current);
	return NULL;
}

/*
 * Scheduler hooks for concurrency managed workqueue.  Only to be used from
 * sched/ and workqueue.c.
 */
void wq_worker_running(struct task_struct *task);
void wq_worker_sleeping(struct task_struct *task);
void wq_worker_tick(struct task_struct *task);
work_func_t wq_worker_last_func(struct task_struct *task);

#endif /* _KERNEL_WORKQUEUE_INTERNAL_H */
