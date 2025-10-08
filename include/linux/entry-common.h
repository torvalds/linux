/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/irq-entry-common.h>
#include <linux/ptrace.h>
#include <linux/seccomp.h>
#include <linux/sched.h>
#include <linux/livepatch.h>
#include <linux/resume_user_mode.h>

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
				 ARCH_SYSCALL_WORK_ENTER)
#define SYSCALL_WORK_EXIT	(SYSCALL_WORK_SYSCALL_TRACEPOINT |	\
				 SYSCALL_WORK_SYSCALL_TRACE |		\
				 SYSCALL_WORK_SYSCALL_AUDIT |		\
				 SYSCALL_WORK_SYSCALL_USER_DISPATCH |	\
				 SYSCALL_WORK_SYSCALL_EXIT_TRAP	|	\
				 ARCH_SYSCALL_WORK_EXIT)

/**
 * syscall_enter_from_user_mode_prepare - Establish state and enable interrupts
 * @regs:	Pointer to currents pt_regs
 *
 * Invoked from architecture specific syscall entry code with interrupts
 * disabled. The calling code has to be non-instrumentable. When the
 * function returns all state is correct, interrupts are enabled and the
 * subsequent functions can be instrumented.
 *
 * This handles lockdep, RCU (context tracking) and tracing state, i.e.
 * the functionality provided by enter_from_user_mode().
 *
 * This is invoked when there is extra architecture specific functionality
 * to be done between establishing state and handling user mode entry work.
 */
void syscall_enter_from_user_mode_prepare(struct pt_regs *regs);

long syscall_trace_enter(struct pt_regs *regs, long syscall,
			 unsigned long work);

/**
 * syscall_enter_from_user_mode_work - Check and handle work before invoking
 *				       a syscall
 * @regs:	Pointer to currents pt_regs
 * @syscall:	The syscall number
 *
 * Invoked from architecture specific syscall entry code with interrupts
 * enabled after invoking syscall_enter_from_user_mode_prepare() and extra
 * architecture specific work.
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
		syscall = syscall_trace_enter(regs, syscall, work);

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
 * This is combination of syscall_enter_from_user_mode_prepare() and
 * syscall_enter_from_user_mode_work().
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

/**
 * syscall_exit_work - Handle work before returning to user mode
 * @regs:	Pointer to current pt_regs
 * @work:	Current thread syscall work
 *
 * Do one-time syscall specific work.
 */
void syscall_exit_work(struct pt_regs *regs, unsigned long work);

/**
 * syscall_exit_to_user_mode_work - Handle work before returning to user mode
 * @regs:	Pointer to currents pt_regs
 *
 * Same as step 1 and 2 of syscall_exit_to_user_mode() but without calling
 * exit_to_user_mode() to perform the final transition to user mode.
 *
 * Calling convention is the same as for syscall_exit_to_user_mode() and it
 * returns with all work handled and interrupts disabled. The caller must
 * invoke exit_to_user_mode() before actually switching to user mode to
 * make the final state transitions. Interrupts must stay disabled between
 * return from this function and the invocation of exit_to_user_mode().
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

	rseq_syscall(regs);

	/*
	 * Do one-time syscall specific work. If these work items are
	 * enabled, we want to run them exactly once per syscall exit with
	 * interrupts enabled.
	 */
	if (unlikely(work & SYSCALL_WORK_EXIT))
		syscall_exit_work(regs, work);
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
}

/**
 * syscall_exit_to_user_mode - Handle work before returning to user mode
 * @regs:	Pointer to currents pt_regs
 *
 * Invoked with interrupts enabled and fully valid regs. Returns with all
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
 *	- Exit to user mode loop (common TIF handling). Invokes
 *	  arch_exit_to_user_mode_work() for architecture specific TIF work
 *	- Architecture specific one time work arch_exit_to_user_mode_prepare()
 *	- Address limit and lockdep checks
 *
 *  3) Final transition (lockdep, tracing, context tracking, RCU), i.e. the
 *     functionality in exit_to_user_mode().
 *
 * This is a combination of syscall_exit_to_user_mode_work() (1,2) and
 * exit_to_user_mode(). This function is preferred unless there is a
 * compelling architectural reason to use the separate functions.
 */
static __always_inline void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	instrumentation_begin();
	syscall_exit_to_user_mode_work(regs);
	instrumentation_end();
	exit_to_user_mode();
}

#endif
