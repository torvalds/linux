// SPDX-License-Identifier: GPL-2.0

#include <linux/context_tracking.h>
#include <linux/entry-common.h>

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/**
 * enter_from_user_mode - Establish state when coming from user mode
 *
 * Syscall/interrupt entry disables interrupts, but user mode is traced as
 * interrupts enabled. Also with NO_HZ_FULL RCU might be idle.
 *
 * 1) Tell lockdep that interrupts are disabled
 * 2) Invoke context tracking if enabled to reactivate RCU
 * 3) Trace interrupts off state
 */
static __always_inline void enter_from_user_mode(struct pt_regs *regs)
{
	arch_check_user_regs(regs);
	lockdep_hardirqs_off(CALLER_ADDR0);

	CT_WARN_ON(ct_state() != CONTEXT_USER);
	user_exit_irqoff();

	instrumentation_begin();
	trace_hardirqs_off_finish();
	instrumentation_end();
}

static inline void syscall_enter_audit(struct pt_regs *regs, long syscall)
{
	if (unlikely(audit_context())) {
		unsigned long args[6];

		syscall_get_arguments(current, regs, args);
		audit_syscall_entry(syscall, args[0], args[1], args[2], args[3]);
	}
}

static long syscall_trace_enter(struct pt_regs *regs, long syscall,
				unsigned long ti_work)
{
	long ret = 0;

	/* Handle ptrace */
	if (ti_work & (_TIF_SYSCALL_TRACE | _TIF_SYSCALL_EMU)) {
		ret = arch_syscall_enter_tracehook(regs);
		if (ret || (ti_work & _TIF_SYSCALL_EMU))
			return -1L;
	}

	/* Do seccomp after ptrace, to catch any tracer changes. */
	if (ti_work & _TIF_SECCOMP) {
		ret = __secure_computing(NULL);
		if (ret == -1L)
			return ret;
	}

	if (unlikely(ti_work & _TIF_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall);

	syscall_enter_audit(regs, syscall);

	return ret ? : syscall;
}

noinstr long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	unsigned long ti_work;

	enter_from_user_mode(regs);
	instrumentation_begin();

	local_irq_enable();
	ti_work = READ_ONCE(current_thread_info()->flags);
	if (ti_work & SYSCALL_ENTER_WORK)
		syscall = syscall_trace_enter(regs, syscall, ti_work);
	instrumentation_end();

	return syscall;
}

noinstr void irqentry_enter_from_user_mode(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
}
