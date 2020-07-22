/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_ENTRYCOMMON_H
#define __LINUX_ENTRYCOMMON_H

#include <linux/tracehook.h>
#include <linux/syscalls.h>
#include <linux/seccomp.h>
#include <linux/sched.h>

#include <asm/entry-common.h>

/*
 * Define dummy _TIF work flags if not defined by the architecture or for
 * disabled functionality.
 */
#ifndef _TIF_SYSCALL_EMU
# define _TIF_SYSCALL_EMU		(0)
#endif

#ifndef _TIF_SYSCALL_TRACEPOINT
# define _TIF_SYSCALL_TRACEPOINT	(0)
#endif

#ifndef _TIF_SECCOMP
# define _TIF_SECCOMP			(0)
#endif

#ifndef _TIF_SYSCALL_AUDIT
# define _TIF_SYSCALL_AUDIT		(0)
#endif

/*
 * TIF flags handled in syscall_enter_from_usermode()
 */
#ifndef ARCH_SYSCALL_ENTER_WORK
# define ARCH_SYSCALL_ENTER_WORK	(0)
#endif

#define SYSCALL_ENTER_WORK						\
	(_TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT | _TIF_SECCOMP |	\
	 _TIF_SYSCALL_TRACEPOINT | _TIF_SYSCALL_EMU |			\
	 ARCH_SYSCALL_ENTER_WORK)

/**
 * arch_check_user_regs - Architecture specific sanity check for user mode regs
 * @regs:	Pointer to currents pt_regs
 *
 * Defaults to an empty implementation. Can be replaced by architecture
 * specific code.
 *
 * Invoked from syscall_enter_from_user_mode() in the non-instrumentable
 * section. Use __always_inline so the compiler cannot push it out of line
 * and make it instrumentable.
 */
static __always_inline void arch_check_user_regs(struct pt_regs *regs);

#ifndef arch_check_user_regs
static __always_inline void arch_check_user_regs(struct pt_regs *regs) {}
#endif

/**
 * arch_syscall_enter_tracehook - Wrapper around tracehook_report_syscall_entry()
 * @regs:	Pointer to currents pt_regs
 *
 * Returns: 0 on success or an error code to skip the syscall.
 *
 * Defaults to tracehook_report_syscall_entry(). Can be replaced by
 * architecture specific code.
 *
 * Invoked from syscall_enter_from_user_mode()
 */
static inline __must_check int arch_syscall_enter_tracehook(struct pt_regs *regs);

#ifndef arch_syscall_enter_tracehook
static inline __must_check int arch_syscall_enter_tracehook(struct pt_regs *regs)
{
	return tracehook_report_syscall_entry(regs);
}
#endif

/**
 * syscall_enter_from_user_mode - Check and handle work before invoking
 *				 a syscall
 * @regs:	Pointer to currents pt_regs
 * @syscall:	The syscall number
 *
 * Invoked from architecture specific syscall entry code with interrupts
 * disabled. The calling code has to be non-instrumentable. When the
 * function returns all state is correct and the subsequent functions can be
 * instrumented.
 *
 * Returns: The original or a modified syscall number
 *
 * If the returned syscall number is -1 then the syscall should be
 * skipped. In this case the caller may invoke syscall_set_error() or
 * syscall_set_return_value() first.  If neither of those are called and -1
 * is returned, then the syscall will fail with ENOSYS.
 *
 * The following functionality is handled here:
 *
 *  1) Establish state (lockdep, RCU (context tracking), tracing)
 *  2) TIF flag dependent invocations of arch_syscall_enter_tracehook(),
 *     __secure_computing(), trace_sys_enter()
 *  3) Invocation of audit_syscall_entry()
 */
long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall);

/**
 * irqentry_enter_from_user_mode - Establish state before invoking the irq handler
 * @regs:	Pointer to currents pt_regs
 *
 * Invoked from architecture specific entry code with interrupts disabled.
 * Can only be called when the interrupt entry came from user mode. The
 * calling code must be non-instrumentable.  When the function returns all
 * state is correct and the subsequent functions can be instrumented.
 *
 * The function establishes state (lockdep, RCU (context tracking), tracing)
 */
void irqentry_enter_from_user_mode(struct pt_regs *regs);

#endif
