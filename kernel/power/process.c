// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/power/process.c - Functions for starting/stopping processes on
 *                           suspend transitions.
 *
 * Originally from swsusp.
 */

#include <linux/interrupt.h>
#include <linux/oom.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/sched/debug.h>
#include <linux/sched/task.h>
#include <linux/syscalls.h>
#include <linux/freezer.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/kmod.h>
#include <trace/events/power.h>
#include <linux/cpuset.h>

/*
 * Timeout for stopping processes
 */
unsigned int __read_mostly freeze_timeout_msecs = 20 * MSEC_PER_SEC;

static int try_to_freeze_tasks(bool user_only)
{
	const char *what = user_only ? "user space processes" :
					"remaining freezable tasks";
	struct task_struct *g, *p;
	unsigned long end_time;
	unsigned int todo;
	bool wq_busy = false;
	ktime_t start, end, elapsed;
	unsigned int elapsed_msecs;
	bool wakeup = false;
	int sleep_usecs = USEC_PER_MSEC;

	pr_info("Freezing %s\n", what);

	start = ktime_get_boottime();

	end_time = jiffies + msecs_to_jiffies(freeze_timeout_msecs);

	if (!user_only)
		freeze_workqueues_begin();

	while (true) {
		todo = 0;
		read_lock(&tasklist_lock);
		for_each_process_thread(g, p) {
			if (p == current || !freeze_task(p))
				continue;

			todo++;
		}
		read_unlock(&tasklist_lock);

		if (!user_only) {
			wq_busy = freeze_workqueues_busy();
			todo += wq_busy;
		}

		if (!todo || time_after(jiffies, end_time))
			break;

		if (pm_wakeup_pending()) {
			wakeup = true;
			break;
		}

		/*
		 * We need to retry, but first give the freezing tasks some
		 * time to enter the refrigerator.  Start with an initial
		 * 1 ms sleep followed by exponential backoff until 8 ms.
		 */
		usleep_range(sleep_usecs / 2, sleep_usecs);
		if (sleep_usecs < 8 * USEC_PER_MSEC)
			sleep_usecs *= 2;
	}

	end = ktime_get_boottime();
	elapsed = ktime_sub(end, start);
	elapsed_msecs = ktime_to_ms(elapsed);

	if (todo) {
		pr_err("Freezing %s %s after %d.%03d seconds "
		       "(%d tasks refusing to freeze, wq_busy=%d):\n", what,
		       wakeup ? "aborted" : "failed",
		       elapsed_msecs / 1000, elapsed_msecs % 1000,
		       todo - wq_busy, wq_busy);

		if (wq_busy)
			show_freezable_workqueues();

		if (!wakeup || pm_debug_messages_on) {
			read_lock(&tasklist_lock);
			for_each_process_thread(g, p) {
				if (p != current && freezing(p) && !frozen(p))
					sched_show_task(p);
			}
			read_unlock(&tasklist_lock);
		}
	} else {
		pr_info("Freezing %s completed (elapsed %d.%03d seconds)\n",
			what, elapsed_msecs / 1000, elapsed_msecs % 1000);
	}

	return todo ? -EBUSY : 0;
}

/**
 * freeze_processes - Signal user space processes to enter the refrigerator.
 * The current thread will not be frozen.  The same process that calls
 * freeze_processes must later call thaw_processes.
 *
 * On success, returns 0.  On failure, -errno and system is fully thawed.
 */
int freeze_processes(void)
{
	int error;

	error = __usermodehelper_disable(UMH_FREEZING);
	if (error)
		return error;

	/* Make sure this task doesn't get frozen */
	current->flags |= PF_SUSPEND_TASK;

	if (!pm_freezing)
		static_branch_inc(&freezer_active);

	pm_wakeup_clear(0);
	pm_freezing = true;
	error = try_to_freeze_tasks(true);
	if (!error)
		__usermodehelper_set_disable_depth(UMH_DISABLED);

	BUG_ON(in_atomic());

	/*
	 * Now that the whole userspace is frozen we need to disable
	 * the OOM killer to disallow any further interference with
	 * killable tasks. There is no guarantee oom victims will
	 * ever reach a point they go away we have to wait with a timeout.
	 */
	if (!error && !oom_killer_disable(msecs_to_jiffies(freeze_timeout_msecs)))
		error = -EBUSY;

	if (error)
		thaw_processes();
	return error;
}

/**
 * freeze_kernel_threads - Make freezable kernel threads go to the refrigerator.
 *
 * On success, returns 0.  On failure, -errno and only the kernel threads are
 * thawed, so as to give a chance to the caller to do additional cleanups
 * (if any) before thawing the userspace tasks. So, it is the responsibility
 * of the caller to thaw the userspace tasks, when the time is right.
 */
int freeze_kernel_threads(void)
{
	int error;

	pm_nosig_freezing = true;
	error = try_to_freeze_tasks(false);

	BUG_ON(in_atomic());

	if (error)
		thaw_kernel_threads();
	return error;
}

void thaw_processes(void)
{
	struct task_struct *g, *p;
	struct task_struct *curr = current;

	trace_suspend_resume(TPS("thaw_processes"), 0, true);
	if (pm_freezing)
		static_branch_dec(&freezer_active);
	pm_freezing = false;
	pm_nosig_freezing = false;

	oom_killer_enable();

	pr_info("Restarting tasks ... ");

	__usermodehelper_set_disable_depth(UMH_FREEZING);
	thaw_workqueues();

	cpuset_wait_for_hotplug();

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		/* No other threads should have PF_SUSPEND_TASK set */
		WARN_ON((p != curr) && (p->flags & PF_SUSPEND_TASK));
		__thaw_task(p);
	}
	read_unlock(&tasklist_lock);

	WARN_ON(!(curr->flags & PF_SUSPEND_TASK));
	curr->flags &= ~PF_SUSPEND_TASK;

	usermodehelper_enable();

	schedule();
	pr_cont("done.\n");
	trace_suspend_resume(TPS("thaw_processes"), 0, false);
}

void thaw_kernel_threads(void)
{
	struct task_struct *g, *p;

	pm_nosig_freezing = false;
	pr_info("Restarting kernel threads ... ");

	thaw_workqueues();

	read_lock(&tasklist_lock);
	for_each_process_thread(g, p) {
		if (p->flags & PF_KTHREAD)
			__thaw_task(p);
	}
	read_unlock(&tasklist_lock);

	schedule();
	pr_cont("done.\n");
}
