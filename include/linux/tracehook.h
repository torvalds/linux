/*
 * Tracing hooks
 *
 * Copyright (C) 2008-2009 Red Hat, Inc.  All rights reserved.
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
 * tracehook_expect_breakpoints - guess if task memory might be touched
 * @task:		current task, making a new mapping
 *
 * Return nonzero if @task is expected to want breakpoint insertion in
 * its memory at some point.  A zero return is no guarantee it won't
 * be done, but this is a hint that it's known to be likely.
 *
 * May be called with @task->mm->mmap_sem held for writing.
 */
static inline int tracehook_expect_breakpoints(struct task_struct *task)
{
	return (task_ptrace(task) & PT_PTRACED) != 0;
}

/*
 * ptrace report for syscall entry and exit looks identical.
 */
static inline void ptrace_report_syscall(struct pt_regs *regs)
{
	int ptrace = task_ptrace(current);

	if (!(ptrace & PT_PTRACED))
		return;

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
}

/**
 * tracehook_report_syscall_entry - task is about to attempt a system call
 * @regs:		user register state of current task
 *
 * This will be called if %TIF_SYSCALL_TRACE has been set, when the
 * current task has just entered the kernel for a system call.
 * Full user register state is available here.  Changing the values
 * in @regs can affect the system call number and arguments to be tried.
 * It is safe to block here, preventing the system call from beginning.
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
	ptrace_report_syscall(regs);
	return 0;
}

/**
 * tracehook_report_syscall_exit - task has just finished a system call
 * @regs:		user register state of current task
 * @step:		nonzero if simulating single-step or block-step
 *
 * This will be called if %TIF_SYSCALL_TRACE has been set, when the
 * current task has just finished an attempted system call.  Full
 * user register state is available here.  It is safe to block here,
 * preventing signals from being processed.
 *
 * If @step is nonzero, this report is also in lieu of the normal
 * trap that would follow the system call instruction because
 * user_enable_block_step() or user_enable_single_step() was used.
 * In this case, %TIF_SYSCALL_TRACE might not be set.
 *
 * Called without locks, just before checking for pending signals.
 */
static inline void tracehook_report_syscall_exit(struct pt_regs *regs, int step)
{
	if (step) {
		siginfo_t info;
		user_single_step_siginfo(current, regs, &info);
		force_sig_info(SIGTRAP, &info, current);
		return;
	}

	ptrace_report_syscall(regs);
}

/**
 * tracehook_unsafe_exec - check for exec declared unsafe due to tracing
 * @task:		current task doing exec
 *
 * Return %LSM_UNSAFE_* bits applied to an exec because of tracing.
 *
 * @task->cred_guard_mutex is held by the caller through the do_execve().
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
 * tracehook_tracer_task - return the task that is tracing the given task
 * @tsk:		task to consider
 *
 * Returns NULL if noone is tracing @task, or the &struct task_struct
 * pointer to its tracer.
 *
 * Must called under rcu_read_lock().  The pointer returned might be kept
 * live only by RCU.  During exec, this may be called with task_lock()
 * held on @task, still held from when tracehook_unsafe_exec() was called.
 */
static inline struct task_struct *tracehook_tracer_task(struct task_struct *tsk)
{
	if (task_ptrace(tsk) & PT_PTRACED)
		return rcu_dereference(tsk->parent);
	return NULL;
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
 * @trace:		return value from tracehook_prepare_clone()
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
 * @regs:		parent's user register state
 * @clone_flags:	flags from parent's system call
 * @pid:		new child's PID in the parent's namespace
 * @child:		new child task
 *
 * Called after a child is set up, but before it has been started running.
 * This is not a good place to block, because the child has not started
 * yet.  Suspend the child here if desired, and then block in
 * tracehook_report_clone_complete().  This must prevent the child from
 * self-reaping if tracehook_report_clone_complete() uses the @child
 * pointer; otherwise it might have died and been released by the time
 * tracehook_report_clone_complete() is called.
 *
 * Called with no locks held, but the child cannot run until this returns.
 */
static inline void tracehook_report_clone(struct pt_regs *regs,
					  unsigned long clone_flags,
					  pid_t pid, struct task_struct *child)
{
	if (unlikely(task_ptrace(child))) {
		/*
		 * It doesn't matter who attached/attaching to this
		 * task, the pending SIGSTOP is right in any case.
		 */
		sigaddset(&child->pending.signal, SIGSTOP);
		set_tsk_thread_flag(child, TIF_SIGPENDING);
	}
}

/**
 * tracehook_report_clone_complete - new child is running
 * @trace:		return value from tracehook_prepare_clone()
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

/**
 * tracehook_prepare_release_task - task is being reaped, clean up tracing
 * @task:		task in %EXIT_DEAD state
 *
 * This is called in release_task() just before @task gets finally reaped
 * and freed.  This would be the ideal place to remove and clean up any
 * tracing-related state for @task.
 *
 * Called with no locks held.
 */
static inline void tracehook_prepare_release_task(struct task_struct *task)
{
}

/**
 * tracehook_finish_release_task - final tracing clean-up
 * @task:		task in %EXIT_DEAD state
 *
 * This is called in release_task() when @task is being in the middle of
 * being reaped.  After this, there must be no tracing entanglements.
 *
 * Called with write_lock_irq(&tasklist_lock) held.
 */
static inline void tracehook_finish_release_task(struct task_struct *task)
{
	ptrace_release_task(task);
}

/**
 * tracehook_signal_handler - signal handler setup is complete
 * @sig:		number of signal being delivered
 * @info:		siginfo_t of signal being delivered
 * @ka:			sigaction setting that chose the handler
 * @regs:		user register state
 * @stepping:		nonzero if debugger single-step or block-step in use
 *
 * Called by the arch code after a signal handler has been set up.
 * Register and stack state reflects the user handler about to run.
 * Signal mask changes have already been made.
 *
 * Called without locks, shortly before returning to user mode
 * (or handling more signals).
 */
static inline void tracehook_signal_handler(int sig, siginfo_t *info,
					    const struct k_sigaction *ka,
					    struct pt_regs *regs, int stepping)
{
	if (stepping)
		ptrace_notify(SIGTRAP);
}

/**
 * tracehook_consider_ignored_signal - suppress short-circuit of ignored signal
 * @task:		task receiving the signal
 * @sig:		signal number being sent
 *
 * Return zero iff tracing doesn't care to examine this ignored signal,
 * so it can short-circuit normal delivery and never even get queued.
 *
 * Called with @task->sighand->siglock held.
 */
static inline int tracehook_consider_ignored_signal(struct task_struct *task,
						    int sig)
{
	return (task_ptrace(task) & PT_PTRACED) != 0;
}

/**
 * tracehook_consider_fatal_signal - suppress special handling of fatal signal
 * @task:		task receiving the signal
 * @sig:		signal number being sent
 *
 * Return nonzero to prevent special handling of this termination signal.
 * Normally handler for signal is %SIG_DFL.  It can be %SIG_IGN if @sig is
 * ignored, in which case force_sig() is about to reset it to %SIG_DFL.
 * When this returns zero, this signal might cause a quick termination
 * that does not give the debugger a chance to intercept the signal.
 *
 * Called with or without @task->sighand->siglock held.
 */
static inline int tracehook_consider_fatal_signal(struct task_struct *task,
						  int sig)
{
	return (task_ptrace(task) & PT_PTRACED) != 0;
}

/**
 * tracehook_force_sigpending - let tracing force signal_pending(current) on
 *
 * Called when recomputing our signal_pending() flag.  Return nonzero
 * to force the signal_pending() flag on, so that tracehook_get_signal()
 * will be called before the next return to user mode.
 *
 * Called with @current->sighand->siglock held.
 */
static inline int tracehook_force_sigpending(void)
{
	return 0;
}

/**
 * tracehook_get_signal - deliver synthetic signal to traced task
 * @task:		@current
 * @regs:		task_pt_regs(@current)
 * @info:		details of synthetic signal
 * @return_ka:		sigaction for synthetic signal
 *
 * Return zero to check for a real pending signal normally.
 * Return -1 after releasing the siglock to repeat the check.
 * Return a signal number to induce an artifical signal delivery,
 * setting *@info and *@return_ka to specify its details and behavior.
 *
 * The @return_ka->sa_handler value controls the disposition of the
 * signal, no matter the signal number.  For %SIG_DFL, the return value
 * is a representative signal to indicate the behavior (e.g. %SIGTERM
 * for death, %SIGQUIT for core dump, %SIGSTOP for job control stop,
 * %SIGTSTP for stop unless in an orphaned pgrp), but the signal number
 * reported will be @info->si_signo instead.
 *
 * Called with @task->sighand->siglock held, before dequeuing pending signals.
 */
static inline int tracehook_get_signal(struct task_struct *task,
				       struct pt_regs *regs,
				       siginfo_t *info,
				       struct k_sigaction *return_ka)
{
	return 0;
}

/**
 * tracehook_notify_jctl - report about job control stop/continue
 * @notify:		zero, %CLD_STOPPED or %CLD_CONTINUED
 * @why:		%CLD_STOPPED or %CLD_CONTINUED
 *
 * This is called when we might call do_notify_parent_cldstop().
 *
 * @notify is zero if we would not ordinarily send a %SIGCHLD,
 * or is the %CLD_STOPPED or %CLD_CONTINUED .si_code for %SIGCHLD.
 *
 * @why is %CLD_STOPPED when about to stop for job control;
 * we are already in %TASK_STOPPED state, about to call schedule().
 * It might also be that we have just exited (check %PF_EXITING),
 * but need to report that a group-wide stop is complete.
 *
 * @why is %CLD_CONTINUED when waking up after job control stop and
 * ready to make a delayed @notify report.
 *
 * Return the %CLD_* value for %SIGCHLD, or zero to generate no signal.
 *
 * Called with the siglock held.
 */
static inline int tracehook_notify_jctl(int notify, int why)
{
	return notify ?: (current->ptrace & PT_PTRACED) ? why : 0;
}

/**
 * tracehook_finish_jctl - report about return from job control stop
 *
 * This is called by do_signal_stop() after wakeup.
 */
static inline void tracehook_finish_jctl(void)
{
}

#define DEATH_REAP			-1
#define DEATH_DELAYED_GROUP_LEADER	-2

/**
 * tracehook_notify_death - task is dead, ready to notify parent
 * @task:		@current task now exiting
 * @death_cookie:	value to pass to tracehook_report_death()
 * @group_dead:		nonzero if this was the last thread in the group to die
 *
 * A return value >= 0 means call do_notify_parent() with that signal
 * number.  Negative return value can be %DEATH_REAP to self-reap right
 * now, or %DEATH_DELAYED_GROUP_LEADER to a zombie without notifying our
 * parent.  Note that a return value of 0 means a do_notify_parent() call
 * that sends no signal, but still wakes up a parent blocked in wait*().
 *
 * Called with write_lock_irq(&tasklist_lock) held.
 */
static inline int tracehook_notify_death(struct task_struct *task,
					 void **death_cookie, int group_dead)
{
	if (task_detached(task))
		return task->ptrace ? SIGCHLD : DEATH_REAP;

	/*
	 * If something other than our normal parent is ptracing us, then
	 * send it a SIGCHLD instead of honoring exit_signal.  exit_signal
	 * only has special meaning to our real parent.
	 */
	if (thread_group_empty(task) && !ptrace_reparented(task))
		return task->exit_signal;

	return task->ptrace ? SIGCHLD : DEATH_DELAYED_GROUP_LEADER;
}

/**
 * tracehook_report_death - task is dead and ready to be reaped
 * @task:		@current task now exiting
 * @signal:		return value from tracheook_notify_death()
 * @death_cookie:	value passed back from tracehook_notify_death()
 * @group_dead:		nonzero if this was the last thread in the group to die
 *
 * Thread has just become a zombie or is about to self-reap.  If positive,
 * @signal is the signal number just sent to the parent (usually %SIGCHLD).
 * If @signal is %DEATH_REAP, this thread will self-reap.  If @signal is
 * %DEATH_DELAYED_GROUP_LEADER, this is a delayed_group_leader() zombie.
 * The @death_cookie was passed back by tracehook_notify_death().
 *
 * If normal reaping is not inhibited, @task->exit_state might be changing
 * in parallel.
 *
 * Called without locks.
 */
static inline void tracehook_report_death(struct task_struct *task,
					  int signal, void *death_cookie,
					  int group_dead)
{
}

#ifdef TIF_NOTIFY_RESUME
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
	if (!test_and_set_tsk_thread_flag(task, TIF_NOTIFY_RESUME))
		kick_process(task);
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
}
#endif	/* TIF_NOTIFY_RESUME */

#endif	/* <linux/tracehook.h> */
