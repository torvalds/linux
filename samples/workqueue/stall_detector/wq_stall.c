// SPDX-License-Identifier: GPL-2.0
/*
 * wq_stall - Test module for the workqueue stall detector.
 *
 * Deliberately creates a workqueue stall so the watchdog fires and
 * prints diagnostic output.  Useful for verifying that the stall
 * detector correctly identifies stuck workers and produces useful
 * backtraces.
 *
 * The stall is triggered by clearing PF_WQ_WORKER before sleeping,
 * which hides the worker from the concurrency manager.  A second
 * work item queued on the same pool then sits in the worklist with
 * no worker available to process it.
 *
 * After ~30s the workqueue watchdog fires:
 *   BUG: workqueue lockup - pool cpus=N ...
 *
 * Build:
 *	make -C <kernel tree> M=samples/workqueue/stall_detector modules
 *
 * Copyright (c) 2026 Meta Platforms, Inc. and affiliates.
 * Copyright (c) 2026 Breno Leitao <leitao@debian.org>
 */

#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/atomic.h>
#include <linux/sched.h>

static DECLARE_WAIT_QUEUE_HEAD(stall_wq_head);
static atomic_t wake_condition = ATOMIC_INIT(0);
static struct work_struct stall_work1;
static struct work_struct stall_work2;

static void stall_work2_fn(struct work_struct *work)
{
	pr_info("wq_stall: second work item finally ran\n");
}

static void stall_work1_fn(struct work_struct *work)
{
	pr_info("wq_stall: first work item running on cpu %d\n",
		raw_smp_processor_id());

	/*
	 * Queue second item while we're still counted as running
	 * (pool->nr_running > 0).  Since schedule_work() on a per-CPU
	 * workqueue targets raw_smp_processor_id(), item 2 lands on the
	 * same pool.  __queue_work -> kick_pool -> need_more_worker()
	 * sees nr_running > 0 and does NOT wake a new worker.
	 */
	schedule_work(&stall_work2);

	/*
	 * Hide from the workqueue concurrency manager.  Without
	 * PF_WQ_WORKER, schedule() won't call wq_worker_sleeping(),
	 * so nr_running is never decremented and no replacement
	 * worker is created.  Item 2 stays stuck in pool->worklist.
	 */
	current->flags &= ~PF_WQ_WORKER;

	pr_info("wq_stall: entering wait_event_idle (PF_WQ_WORKER cleared)\n");
	pr_info("wq_stall: expect 'BUG: workqueue lockup' in ~30-60s\n");
	wait_event_idle(stall_wq_head, atomic_read(&wake_condition) != 0);

	/* Restore so process_one_work() cleanup works correctly */
	current->flags |= PF_WQ_WORKER;
	pr_info("wq_stall: woke up, PF_WQ_WORKER restored\n");
}

static int __init wq_stall_init(void)
{
	pr_info("wq_stall: loading\n");

	INIT_WORK(&stall_work1, stall_work1_fn);
	INIT_WORK(&stall_work2, stall_work2_fn);
	schedule_work(&stall_work1);

	return 0;
}

static void __exit wq_stall_exit(void)
{
	pr_info("wq_stall: unloading\n");
	atomic_set(&wake_condition, 1);
	wake_up(&stall_wq_head);
	flush_work(&stall_work1);
	flush_work(&stall_work2);
	pr_info("wq_stall: all work flushed, module unloaded\n");
}

module_init(wq_stall_init);
module_exit(wq_stall_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reproduce workqueue stall caused by PF_WQ_WORKER misuse");
MODULE_AUTHOR("Breno Leitao <leitao@debian.org>");
