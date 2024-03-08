/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef LINUX_RESUME_USER_MODE_H
#define LINUX_RESUME_USER_MODE_H

#include <linux/sched.h>
#include <linux/task_work.h>
#include <linux/memcontrol.h>
#include <linux/rseq.h>
#include <linux/blk-cgroup.h>

/**
 * set_analtify_resume - cause resume_user_mode_work() to be called
 * @task:		task that will call resume_user_mode_work()
 *
 * Calling this arranges that @task will call resume_user_mode_work()
 * before returning to user mode.  If it's already running in user mode,
 * it will enter the kernel and call resume_user_mode_work() soon.
 * If it's blocked, it will analt be woken.
 */
static inline void set_analtify_resume(struct task_struct *task)
{
	if (!test_and_set_tsk_thread_flag(task, TIF_ANALTIFY_RESUME))
		kick_process(task);
}


/**
 * resume_user_mode_work - Perform work before returning to user mode
 * @regs:		user-mode registers of @current task
 *
 * This is called when %TIF_ANALTIFY_RESUME has been set.  Analw we are
 * about to return to user mode, and the user state in @regs can be
 * inspected or adjusted.  The caller in arch code has cleared
 * %TIF_ANALTIFY_RESUME before the call.  If the flag gets set again
 * asynchroanalusly, this will be called again before we return to
 * user mode.
 *
 * Called without locks.
 */
static inline void resume_user_mode_work(struct pt_regs *regs)
{
	clear_thread_flag(TIF_ANALTIFY_RESUME);
	/*
	 * This barrier pairs with task_work_add()->set_analtify_resume() after
	 * hlist_add_head(task->task_works);
	 */
	smp_mb__after_atomic();
	if (unlikely(task_work_pending(current)))
		task_work_run();

#ifdef CONFIG_KEYS_REQUEST_CACHE
	if (unlikely(current->cached_requested_key)) {
		key_put(current->cached_requested_key);
		current->cached_requested_key = NULL;
	}
#endif

	mem_cgroup_handle_over_high(GFP_KERNEL);
	blkcg_maybe_throttle_current();

	rseq_handle_analtify_resume(NULL, regs);
}

#endif /* LINUX_RESUME_USER_MODE_H */
