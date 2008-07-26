/*
 * Tracing hooks
 *
 * Copyright (C) 2008 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License v.2.
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
struct linux_binprm;

/**
 * tracehook_unsafe_exec - check for exec declared unsafe due to tracing
 * @task:		current task doing exec
 *
 * Return %LSM_UNSAFE_* bits applied to an exec because of tracing.
 *
 * Called with task_lock() held on @task.
 */
static inline int tracehook_unsafe_exec(struct task_struct *task)
{
	int unsafe = 0;
	int ptrace = task_ptrace(task);
	if (ptrace & PT_PTRACED) {
		if (ptrace & PT_PTRACE_CAP)
			unsafe |= LSM_UNSAFE_PTRACE_CAP;
		else
			unsafe |= LSM_UNSAFE_PTRACE;
	}
	return unsafe;
}

/**
 * tracehook_report_exec - a successful exec was completed
 * @fmt:		&struct linux_binfmt that performed the exec
 * @bprm:		&struct linux_binprm containing exec details
 * @regs:		user-mode register state
 *
 * An exec just completed, we are shortly going to return to user mode.
 * The freshly initialized register state can be seen and changed in @regs.
 * The name, file and other pointers in @bprm are still on hand to be
 * inspected, but will be freed as soon as this returns.
 *
 * Called with no locks, but with some kernel resources held live
 * and a reference on @fmt->module.
 */
static inline void tracehook_report_exec(struct linux_binfmt *fmt,
					 struct linux_binprm *bprm,
					 struct pt_regs *regs)
{
	if (!ptrace_event(PT_TRACE_EXEC, PTRACE_EVENT_EXEC, 0) &&
	    unlikely(task_ptrace(current) & PT_PTRACED))
		send_sig(SIGTRAP, current, 0);
}

/**
 * tracehook_report_exit - task has begun to exit
 * @exit_code:		pointer to value destined for @current->exit_code
 *
 * @exit_code points to the value passed to do_exit(), which tracing
 * might change here.  This is almost the first thing in do_exit(),
 * before freeing any resources or setting the %PF_EXITING flag.
 *
 * Called with no locks held.
 */
static inline void tracehook_report_exit(long *exit_code)
{
	ptrace_event(PT_TRACE_EXIT, PTRACE_EVENT_EXIT, *exit_code);
}

/**
 * tracehook_prepare_clone - prepare for new child to be cloned
 * @clone_flags:	%CLONE_* flags from clone/fork/vfork system call
 *
 * This is called before a new user task is to be cloned.
 * Its return value will be passed to tracehook_finish_clone().
 *
 * Called with no locks held.
 */
static inline int tracehook_prepare_clone(unsigned clone_flags)
{
	if (clone_flags & CLONE_UNTRACED)
		return 0;

	if (clone_flags & CLONE_VFORK) {
		if (current->ptrace & PT_TRACE_VFORK)
			return PTRACE_EVENT_VFORK;
	} else if ((clone_flags & CSIGNAL) != SIGCHLD) {
		if (current->ptrace & PT_TRACE_CLONE)
			return PTRACE_EVENT_CLONE;
	} else if (current->ptrace & PT_TRACE_FORK)
		return PTRACE_EVENT_FORK;

	return 0;
}

/**
 * tracehook_finish_clone - new child created and being attached
 * @child:		new child task
 * @clone_flags:	%CLONE_* flags from clone/fork/vfork system call
 * @trace:		return value from tracehook_clone_prepare()
 *
 * This is called immediately after adding @child to its parent's children list.
 * The @trace value is that returned by tracehook_prepare_clone().
 *
 * Called with current's siglock and write_lock_irq(&tasklist_lock) held.
 */
static inline void tracehook_finish_clone(struct task_struct *child,
					  unsigned long clone_flags, int trace)
{
	ptrace_init_task(child, (clone_flags & CLONE_PTRACE) || trace);
}

/**
 * tracehook_report_clone - in parent, new child is about to start running
 * @trace:		return value from tracehook_clone_prepare()
 * @regs:		parent's user register state
 * @clone_flags:	flags from parent's system call
 * @pid:		new child's PID in the parent's namespace
 * @child:		new child task
 *
 * Called after a child is set up, but before it has been started running.
 * The @trace value is that returned by tracehook_clone_prepare().
 * This is not a good place to block, because the child has not started yet.
 * Suspend the child here if desired, and block in tracehook_clone_complete().
 * This must prevent the child from self-reaping if tracehook_clone_complete()
 * uses the @child pointer; otherwise it might have died and been released by
 * the time tracehook_report_clone_complete() is called.
 *
 * Called with no locks held, but the child cannot run until this returns.
 */
static inline void tracehook_report_clone(int trace, struct pt_regs *regs,
					  unsigned long clone_flags,
					  pid_t pid, struct task_struct *child)
{
	if (unlikely(trace)) {
		/*
		 * The child starts up with an immediate SIGSTOP.
		 */
		sigaddset(&child->pending.signal, SIGSTOP);
		set_tsk_thread_flag(child, TIF_SIGPENDING);
	}
}

/**
 * tracehook_report_clone_complete - new child is running
 * @trace:		return value from tracehook_clone_prepare()
 * @regs:		parent's user register state
 * @clone_flags:	flags from parent's system call
 * @pid:		new child's PID in the parent's namespace
 * @child:		child task, already running
 *
 * This is called just after the child has started running.  This is
 * just before the clone/fork syscall returns, or blocks for vfork
 * child completion if @clone_flags has the %CLONE_VFORK bit set.
 * The @child pointer may be invalid if a self-reaping child died and
 * tracehook_report_clone() took no action to prevent it from self-reaping.
 *
 * Called with no locks held.
 */
static inline void tracehook_report_clone_complete(int trace,
						   struct pt_regs *regs,
						   unsigned long clone_flags,
						   pid_t pid,
						   struct task_struct *child)
{
	if (unlikely(trace))
		ptrace_event(0, trace, pid);
}

/**
 * tracehook_report_vfork_done - vfork parent's child has exited or exec'd
 * @child:		child task, already running
 * @pid:		new child's PID in the parent's namespace
 *
 * Called after a %CLONE_VFORK parent has waited for the child to complete.
 * The clone/vfork system call will return immediately after this.
 * The @child pointer may be invalid if a self-reaping child died and
 * tracehook_report_clone() took no action to prevent it from self-reaping.
 *
 * Called with no locks held.
 */
static inline void tracehook_report_vfork_done(struct task_struct *child,
					       pid_t pid)
{
	ptrace_event(PT_TRACE_VFORK_DONE, PTRACE_EVENT_VFORK_DONE, pid);
}

#endif	/* <linux/tracehook.h> */
