/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tracing hooks
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
 *
 * This file defines hook entry points called by core code where
 * user tracing/debugging support might need to do something.  These
 * entry points are called tracehook_*().  Each hook declared below
 * has a detailed kerneldoc comment giving the context (locking et
 * al) from which it is called, and the meaning of its return value.
 *
 * Each function here typically has only one call site, so it is ok
 * to have some nontrivial tracehook_*() inlines.  In all cases, the
 * fast path when no tracing is enabled should be very short.
 *
 * The purpose of this file and the tracehook_* layer is to consolidate
 * the interface that the kernel core and arch code uses to enable any
 * user debugging or tracing facility (such as ptrace).  The interfaces
 * here are carefully documented so that maintainers of core and arch
 * code do not need to think about the implementation details of the
 * tracing facilities.  Likewise, maintainers of the tracing code do not
 * need to understand all the calling core or arch code in detail, just
 * documented circumstances of each call, such as locking conditions.
 *
 * If the calling core code changes so that locking is different, then
 * it is ok to change the interface documented here.  The maintainer of
 * core code changing should notify the maintainers of the tracing code
 * that they need to work out the change.
 *
 * Some tracehook_*() inlines take arguments that the current tracing
 * implementations might not necessarily use.  These function signatures
 * are chosen to pass in all the information that is on hand in the
 * caller and might conceivably be relevant to a tracer, so that the
 * core code won't have to be updated when tracing adds more features.
 * If a call site changes so that some of those parameters are no longer
 * already on hand without extra work, then the tracehook_* interface
 * can change so there is no make-work burden on the core code.  The
 * maintainer of core code changing should notify the maintainers of the
 * tracing code that they need to work out the change.
 */

#ifndef _LINUX_TRACEHOOK_H
#define _LINUX_TRACEHOOK_H	1

#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/security.h>
#include <linux/task_work.h>
#include <linux/memcontrol.h>
#include <linux/blk-cgroup.h>
struct linux_binprm;

/*
 * ptrace report for syscall entry and exit looks identical.
 */
static inline int ptrace_report_syscall(struct pt_regs *regs,
					unsigned long message)
{
	int ptrace = current->ptrace;

	if (!(ptrace & PT_PTRACED))
		return 0;

	current->ptrace_message = message;
	ptrace_notify(SIGTRAP | ((ptrace & PT_TRACESYSGOOD) ? 0x80 : 0));

	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}

	current->ptrace_message = 0;
	return fatal_signal_pending(current);
}

/**
 * tracehook_report_syscall_entry - task is about to attempt a system call
 * @regs:		user register state of current task
 *
 * This will be called if %SYSCALL_WORK_SYSCALL_TRACE or
 * %SYSCALL_WORK_SYSCALL_EMU have been set, when the current task has just
 * entered the kernel for a system call.  Full user register state is
 * available here.  Changing the values in @regs can affect the system
 * call number and arguments to be tried.  It is safe to block here,
 * preventing the system call from beginning.
 *
 * Returns zero normally, or nonzero if the calling arch code should abort
 * the system call.  That must prevent normal entry so no system call is
 * made.  If @task ever returns to user mode after this, its register state
 * is unspecified, but should be something harmless like an %ENOSYS error
 * return.  It should preserve enough information so that syscall_rollback()
 * can work (see asm-generic/syscall.h).
 *
 * Called without locks, just after entering kernel mode.
 */
static inline __must_check int tracehook_report_syscall_entry(
	struct pt_regs *regs)
{
	return ptrace_report_syscall(regs, PTRACE_EVENTMSG_SYSCALL_ENTRY);
}

/**
 * tracehook_report_syscall_exit - task has just finished a system call
 * @regs:		user register state of current task
 * @step:		nonzero if simulating single-step or block-step
 *
 * This will be called if %SYSCALL_WORK_SYSCALL_TRACE has been set, when
 * the current task has just finished an attempted system call.  Full
 * user register state is available here.  It is safe to block here,
 * preventing signals from being processed.
 *
 * If @step is nonzero, this report is also in lieu of the normal
 * trap that would follow the system call instruction because
 * user_enable_block_step() or user_enable_single_step() was used.
 * In this case, %SYSCALL_WORK_SYSCALL_TRACE might not be set.
 *
 * Called without locks, just before checking for pending signals.
 */
static inline void tracehook_report_syscall_exit(struct pt_regs *regs, int step)
{
	if (step)
		user_single_step_report(regs);
	else
		ptrace_report_syscall(regs, PTRACE_EVENTMSG_SYSCALL_EXIT);
}

/**
 * tracehook_signal_handler - signal handler setup is complete
 * @stepping:		nonzero if debugger single-step or block-step in use
 *
 * Called by the arch code after a signal handler has been set up.
 * Register and stack state reflects the user handler about to run.
 * Signal mask changes have already been made.
 *
 * Called without locks, shortly before returning to user mode
 * (or handling more signals).
 */
static inline void tracehook_signal_handler(int stepping)
{
	if (stepping)
		ptrace_notify(SIGTRAP);
}

/**
 * set_notify_resume - cause tracehook_notify_resume() to be called
 * @task:		task that will call tracehook_notify_resume()
 *
 * Calling this arranges that @task will call tracehook_notify_resume()
 * before returning to user mode.  If it's already running in user mode,
 * it will enter the kernel and call tracehook_notify_resume() soon.
 * If it's blocked, it will not be woken.
 */
static inline void set_notify_resume(struct task_struct *task)
{
#ifdef TIF_NOTIFY_RESUME
	if (!test_and_set_tsk_thread_flag(task, TIF_NOTIFY_RESUME))
		kick_process(task);
#endif
}

/**
 * tracehook_notify_resume - report when about to return to user mode
 * @regs:		user-mode registers of @current task
 *
 * This is called when %TIF_NOTIFY_RESUME has been set.  Now we are
 * about to return to user mode, and the user state in @regs can be
 * inspected or adjusted.  The caller in arch code has cleared
 * %TIF_NOTIFY_RESUME before the call.  If the flag gets set again
 * asynchronously, this will be called again before we return to
 * user mode.
 *
 * Called without locks.
 */
static inline void tracehook_notify_resume(struct pt_regs *regs)
{
	clear_thread_flag(TIF_NOTIFY_RESUME);
	/*
	 * This barrier pairs with task_work_add()->set_notify_resume() after
	 * hlist_add_head(task->task_works);
	 */
	smp_mb__after_atomic();
	if (unlikely(current->task_works))
		task_work_run();

#ifdef CONFIG_KEYS_REQUEST_CACHE
	if (unlikely(current->cached_requested_key)) {
		key_put(current->cached_requested_key);
		current->cached_requested_key = NULL;
	}
#endif

	mem_cgroup_handle_over_high();
	blkcg_maybe_throttle_current();
}

/*
 * called by exit_to_user_mode_loop() if ti_work & _TIF_NOTIFY_SIGNAL. This
 * is currently used by TWA_SIGNAL based task_work, which requires breaking
 * wait loops to ensure that task_work is noticed and run.
 */
static inline void tracehook_notify_signal(void)
{
	clear_thread_flag(TIF_NOTIFY_SIGNAL);
	smp_mb__after_atomic();
	if (current->task_works)
		task_work_run();
}

/*
 * Called when we have work to process from exit_to_user_mode_loop()
 */
static inline void set_notify_signal(struct task_struct *task)
{
	if (!test_and_set_tsk_thread_flag(task, TIF_NOTIFY_SIGNAL) &&
	    !wake_up_state(task, TASK_INTERRUPTIBLE))
		kick_process(task);
}

#endif	/* <linux/tracehook.h> */
