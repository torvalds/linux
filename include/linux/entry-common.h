/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/audit.h>
#include <linux/irq-entry-common.h>
#include <linux/livepatch.h>
#include <linux/ptrace.h>
#include <linux/resume_user_mode.h>
#include <linux/seccomp.h>
#include <linux/sched.h>

#include <asm/entry-common.h>
#include <asm/syscall.h>

#ifndef _TIF_UPROBE
# define _TIF_UPROBE			(0)
#endif

/*
 * SYSCALL_WORK flags handled in syscall_enter_from_user_mode()
 */
#ifndef ARCH_SYSCALL_WORK_ENTER
# define ARCH_SYSCALL_WORK_ENTER	(0)
#endif

/*
 * SYSCALL_WORK flags handled in syscall_exit_to_user_mode()
 */
#ifndef ARCH_SYSCALL_WORK_EXIT
# define ARCH_SYSCALL_WORK_EXIT		(0)
#endif

#define SYSCALL_WORK_ENTER	(SYSCALL_WORK_SECCOMP |			\
				 SYSCALL_WORK_SYSCALL_TRACEPOINT |	\
				 SYSCALL_WORK_SYSCALL_TRACE |		\
				 SYSCALL_WORK_SYSCALL_EMU |		\
				 SYSCALL_WORK_SYSCALL_AUDIT |		\
				 SYSCALL_WORK_SYSCALL_USER_DISPATCH |	\
				 SYSCALL_WORK_SYSCALL_RSEQ_SLICE |	\
				 ARCH_SYSCALL_WORK_ENTER)
#define SYSCALL_WORK_EXIT	(SYSCALL_WORK_SYSCALL_TRACEPOINT |	\
				 SYSCALL_WORK_SYSCALL_TRACE |		\
				 SYSCALL_WORK_SYSCALL_AUDIT |		\
				 SYSCALL_WORK_SYSCALL_USER_DISPATCH |	\
				 SYSCALL_WORK_SYSCALL_EXIT_TRAP	|	\
				 ARCH_SYSCALL_WORK_EXIT)

/**
 * arch_ptrace_report_syscall_entry - Architecture specific ptrace_report_syscall_entry() wrapper
 *
 * Invoked from syscall_trace_enter() to wrap ptrace_report_syscall_entry().
 *
 * This allows architecture specific ptrace_report_syscall_entry()
 * implementations. If not defined by the architecture this falls back to
 * to ptrace_report_syscall_entry().
 */
static __always_inline int arch_ptrace_report_syscall_entry(struct pt_regs *regs);

#ifndef arch_ptrace_report_syscall_entry
static __always_inline int arch_ptrace_report_syscall_entry(struct pt_regs *regs)
{
	return ptrace_report_syscall_entry(regs);
}
#endif

bool syscall_user_dispatch(struct pt_regs *regs);
long trace_syscall_enter(struct pt_regs *regs, long syscall);
void trace_syscall_exit(struct pt_regs *regs, long ret);

static inline void syscall_enter_audit(struct pt_regs *regs, long syscall)
{
	if (unlikely(audit_context())) {
		unsigned long args[6];

		syscall_get_arguments(current, regs, args);
		audit_syscall_entry(syscall, args[0], args[1], args[2], args[3]);
	}
}

static __always_inline long syscall_trace_enter(struct pt_regs *regs, unsigned long work)
{
	long syscall, ret = 0;

	/*
	 * Handle Syscall User Dispatch.  This must comes first, since
	 * the ABI here can be something that doesn't make sense for
	 * other syscall_work features.
	 */
	if (work & SYSCALL_WORK_SYSCALL_USER_DISPATCH) {
		if (syscall_user_dispatch(regs))
			return -1L;
	}

	/*
	 * User space got a time slice extension granted and relinquishes
	 * the CPU. The work stops the slice timer to avoid an extra round
	 * through hrtimer_interrupt().
	 */
	if (work & SYSCALL_WORK_SYSCALL_RSEQ_SLICE)
		rseq_syscall_enter_work(syscall_get_nr(current, regs));

	/* Handle ptrace */
	if (work & (SYSCALL_WORK_SYSCALL_TRACE | SYSCALL_WORK_SYSCALL_EMU)) {
		ret = arch_ptrace_report_syscall_entry(regs);
		if (ret || (work & SYSCALL_WORK_SYSCALL_EMU))
			return -1L;
	}

	/* Do seccomp after ptrace, to catch any tracer changes. */
	if (work & SYSCALL_WORK_SECCOMP) {
		ret = __secure_computing();
		if (ret == -1L)
			return ret;
	}

	/* Either of the above might have changed the syscall number */
	syscall = syscall_get_nr(current, regs);

	if (unlikely(work & SYSCALL_WORK_SYSCALL_TRACEPOINT))
		syscall = trace_syscall_enter(regs, syscall);

	syscall_enter_audit(regs, syscall);

	return ret ? : syscall;
}

/**
 * syscall_enter_from_user_mode_work - Check and handle work before invoking
 *				       a syscall
 * @regs:	Pointer to currents pt_regs
 * @syscall:	The syscall number
 *
 * Invoked from architecture specific syscall entry code with interrupts
 * enabled after invoking enter_from_user_mode(), enabling interrupts and
 * extra architecture specific work.
 *
 * Returns: The original or a modified syscall number
 *
 * If the returned syscall number is -1 then the syscall should be
 * skipped. In this case the caller may invoke syscall_set_error() or
 * syscall_set_return_value() first.  If neither of those are called and -1
 * is returned, then the syscall will fail with ENOSYS.
 *
 * It handles the following work items:
 *
 *  1) syscall_work flag dependent invocations of
 *     ptrace_report_syscall_entry(), __secure_computing(), trace_sys_enter()
 *  2) Invocation of audit_syscall_entry()
 */
static __always_inline long syscall_enter_from_user_mode_work(struct pt_regs *regs, long syscall)
{
	unsigned long work = READ_ONCE(current_thread_info()->syscall_work);

	if (work & SYSCALL_WORK_ENTER)
		syscall = syscall_trace_enter(regs, work);

	return syscall;
}

/**
 * syscall_enter_from_user_mode - Establish state and check and handle work
 *				  before invoking a syscall
 * @regs:	Pointer to currents pt_regs
 * @syscall:	The syscall number
 *
 * Invoked from architecture specific syscall entry code with interrupts
 * disabled. The calling code has to be non-instrumentable. When the
 * function returns all state is correct, interrupts are enabled and the
 * subsequent functions can be instrumented.
 *
 * This is the combination of enter_from_user_mode() and
 * syscall_enter_from_user_mode_work() to be used when there is no
 * architecture specific work to be done between the two.
 *
 * Returns: The original or a modified syscall number. See
 * syscall_enter_from_user_mode_work() for further explanation.
 */
static __always_inline long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	long ret;

	enter_from_user_mode(regs);

	instrumentation_begin();
	local_irq_enable();
	ret = syscall_enter_from_user_mode_work(regs, syscall);
	instrumentation_end();

	return ret;
}

/*
 * If SYSCALL_EMU is set, then the only reason to report is when
 * SINGLESTEP is set (i.e. PTRACE_SYSEMU_SINGLESTEP).  This syscall
 * instruction has been already reported in syscall_enter_from_user_mode().
 */
static __always_inline bool report_single_step(unsigned long work)
{
	if (work & SYSCALL_WORK_SYSCALL_EMU)
		return false;

	return work & SYSCALL_WORK_SYSCALL_EXIT_TRAP;
}

/**
 * arch_ptrace_report_syscall_exit - Architecture specific ptrace_report_syscall_exit()
 *
 * This allows architecture specific ptrace_report_syscall_exit()
 * implementations. If not defined by the architecture this falls back to
 * to ptrace_report_syscall_exit().
 */
static __always_inline void arch_ptrace_report_syscall_exit(struct pt_regs *regs,
							    int step);

#ifndef arch_ptrace_report_syscall_exit
static __always_inline void arch_ptrace_report_syscall_exit(struct pt_regs *regs,
							    int step)
{
	ptrace_report_syscall_exit(regs, step);
}
#endif

/**
 * syscall_exit_work - Handle work before returning to user mode
 * @regs:	Pointer to current pt_regs
 * @work:	Current thread syscall work
 *
 * Do one-time syscall specific work.
 */
static __always_inline void syscall_exit_work(struct pt_regs *regs, unsigned long work)
{
	bool step;

	/*
	 * If the syscall was rolled back due to syscall user dispatching,
	 * then the tracers below are not invoked for the same reason as
	 * the entry side was not invoked in syscall_trace_enter(): The ABI
	 * of these syscalls is unknown.
	 */
	if (work & SYSCALL_WORK_SYSCALL_USER_DISPATCH) {
		if (unlikely(current->syscall_dispatch.on_dispatch)) {
			current->syscall_dispatch.on_dispatch = false;
			return;
		}
	}

	audit_syscall_exit(regs);

	if (work & SYSCALL_WORK_SYSCALL_TRACEPOINT)
		trace_syscall_exit(regs, syscall_get_return_value(current, regs));

	step = report_single_step(work);
	if (step || work & SYSCALL_WORK_SYSCALL_TRACE)
		arch_ptrace_report_syscall_exit(regs, step);
}

/**
 * syscall_exit_to_user_mode_work - Handle one time work before returning to user mode
 * @regs:	Pointer to currents pt_regs
 *
 * Step 1 of syscall_exit_to_user_mode() with the same calling convention.
 *
 * The caller must invoke steps 2-3 of syscall_exit_to_user_mode() afterwards.
 */
static __always_inline void syscall_exit_to_user_mode_work(struct pt_regs *regs)
{
	unsigned long work = READ_ONCE(current_thread_info()->syscall_work);
	unsigned long nr = syscall_get_nr(current, regs);

	CT_WARN_ON(ct_state() != CT_STATE_KERNEL);

	if (IS_ENABLED(CONFIG_PROVE_LOCKING)) {
		if (WARN(irqs_disabled(), "syscall %lu left IRQs disabled", nr))
			local_irq_enable();
	}

	rseq_debug_syscall_return(regs);

	/*
	 * Do one-time syscall specific work. If these work items are
	 * enabled, we want to run them exactly once per syscall exit with
	 * interrupts enabled.
	 */
	if (unlikely(work & SYSCALL_WORK_EXIT))
		syscall_exit_work(regs, work);
}

/**
 * syscall_exit_to_user_mode - Handle work before returning to user mode
 * @regs:	Pointer to currents pt_regs
 *
 * Invoked with interrupts enabled and fully valid @regs. Returns with all
 * work handled, interrupts disabled such that the caller can immediately
 * switch to user mode. Called from architecture specific syscall and ret
 * from fork code.
 *
 * The call order is:
 *  1) One-time syscall exit work:
 *	- rseq syscall exit
 *      - audit
 *	- syscall tracing
 *	- ptrace (single stepping)
 *
 *  2) Preparatory work
 *	- Disable interrupts
 *	- Exit to user mode loop (common TIF handling). Invokes
 *	  arch_exit_to_user_mode_work() for architecture specific TIF work
 *	- Architecture specific one time work arch_exit_to_user_mode_prepare()
 *	- Address limit and lockdep checks
 *
 *  3) Final transition (lockdep, tracing, context tracking, RCU), i.e. the
 *     functionality in exit_to_user_mode().
 *
 * This is a combination of syscall_exit_to_user_mode_work() (1), disabling
 * interrupts followed by syscall_exit_to_user_mode_prepare() (2) and
 * exit_to_user_mode() (3). This function is preferred unless there is a
 * compelling architectural reason to invoke the functions separately.
 */
static __always_inline void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	instrumentation_begin();
	syscall_exit_to_user_mode_work(regs);
	local_irq_disable_exit_to_user();
	syscall_exit_to_user_mode_prepare(regs);
	instrumentation_end();
	exit_to_user_mode();
}

#endif
