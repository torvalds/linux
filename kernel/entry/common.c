// SPDX-License-Identifier: GPL-2.0

#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/livepatch.h>
#include <linux/audit.h>

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

	/* The above might have changed the syscall number */
	return ret ? : syscall_get_nr(current, regs);
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

/**
 * exit_to_user_mode - Fixup state when exiting to user mode
 *
 * Syscall/interupt exit enables interrupts, but the kernel state is
 * interrupts disabled when this is invoked. Also tell RCU about it.
 *
 * 1) Trace interrupts on state
 * 2) Invoke context tracking if enabled to adjust RCU state
 * 3) Invoke architecture specific last minute exit code, e.g. speculation
 *    mitigations, etc.
 * 4) Tell lockdep that interrupts are enabled
 */
static __always_inline void exit_to_user_mode(void)
{
	instrumentation_begin();
	trace_hardirqs_on_prepare();
	lockdep_hardirqs_on_prepare(CALLER_ADDR0);
	instrumentation_end();

	user_enter_irqoff();
	arch_exit_to_user_mode();
	lockdep_hardirqs_on(CALLER_ADDR0);
}

/* Workaround to allow gradual conversion of architecture code */
void __weak arch_do_signal(struct pt_regs *regs) { }

static unsigned long exit_to_user_mode_loop(struct pt_regs *regs,
					    unsigned long ti_work)
{
	/*
	 * Before returning to user space ensure that all pending work
	 * items have been completed.
	 */
	while (ti_work & EXIT_TO_USER_MODE_WORK) {

		local_irq_enable_exit_to_user(ti_work);

		if (ti_work & _TIF_NEED_RESCHED)
			schedule();

		if (ti_work & _TIF_UPROBE)
			uprobe_notify_resume(regs);

		if (ti_work & _TIF_PATCH_PENDING)
			klp_update_patch_state(current);

		if (ti_work & _TIF_SIGPENDING)
			arch_do_signal(regs);

		if (ti_work & _TIF_NOTIFY_RESUME) {
			clear_thread_flag(TIF_NOTIFY_RESUME);
			tracehook_notify_resume(regs);
			rseq_handle_notify_resume(NULL, regs);
		}

		/* Architecture specific TIF work */
		arch_exit_to_user_mode_work(regs, ti_work);

		/*
		 * Disable interrupts and reevaluate the work flags as they
		 * might have changed while interrupts and preemption was
		 * enabled above.
		 */
		local_irq_disable_exit_to_user();
		ti_work = READ_ONCE(current_thread_info()->flags);
	}

	/* Return the latest work state for arch_exit_to_user_mode() */
	return ti_work;
}

static void exit_to_user_mode_prepare(struct pt_regs *regs)
{
	unsigned long ti_work = READ_ONCE(current_thread_info()->flags);

	lockdep_assert_irqs_disabled();

	if (unlikely(ti_work & EXIT_TO_USER_MODE_WORK))
		ti_work = exit_to_user_mode_loop(regs, ti_work);

	arch_exit_to_user_mode_prepare(regs, ti_work);

	/* Ensure that the address limit is intact and no locks are held */
	addr_limit_user_check();
	lockdep_assert_irqs_disabled();
	lockdep_sys_exit();
}

#ifndef _TIF_SINGLESTEP
static inline bool report_single_step(unsigned long ti_work)
{
	return false;
}
#else
/*
 * If TIF_SYSCALL_EMU is set, then the only reason to report is when
 * TIF_SINGLESTEP is set (i.e. PTRACE_SYSEMU_SINGLESTEP).  This syscall
 * instruction has been already reported in syscall_enter_from_usermode().
 */
#define SYSEMU_STEP	(_TIF_SINGLESTEP | _TIF_SYSCALL_EMU)

static inline bool report_single_step(unsigned long ti_work)
{
	return (ti_work & SYSEMU_STEP) == _TIF_SINGLESTEP;
}
#endif

static void syscall_exit_work(struct pt_regs *regs, unsigned long ti_work)
{
	bool step;

	audit_syscall_exit(regs);

	if (ti_work & _TIF_SYSCALL_TRACEPOINT)
		trace_sys_exit(regs, syscall_get_return_value(current, regs));

	step = report_single_step(ti_work);
	if (step || ti_work & _TIF_SYSCALL_TRACE)
		arch_syscall_exit_tracehook(regs, step);
}

/*
 * Syscall specific exit to user mode preparation. Runs with interrupts
 * enabled.
 */
static void syscall_exit_to_user_mode_prepare(struct pt_regs *regs)
{
	u32 cached_flags = READ_ONCE(current_thread_info()->flags);
	unsigned long nr = syscall_get_nr(current, regs);

	CT_WARN_ON(ct_state() != CONTEXT_KERNEL);

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
	if (unlikely(cached_flags & SYSCALL_EXIT_WORK))
		syscall_exit_work(regs, cached_flags);
}

__visible noinstr void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	instrumentation_begin();
	syscall_exit_to_user_mode_prepare(regs);
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
	instrumentation_end();
	exit_to_user_mode();
}

noinstr void irqentry_enter_from_user_mode(struct pt_regs *regs)
{
	enter_from_user_mode(regs);
}

noinstr void irqentry_exit_to_user_mode(struct pt_regs *regs)
{
	instrumentation_begin();
	exit_to_user_mode_prepare(regs);
	instrumentation_end();
	exit_to_user_mode();
}

noinstr irqentry_state_t irqentry_enter(struct pt_regs *regs)
{
	irqentry_state_t ret = {
		.exit_rcu = false,
	};

	if (user_mode(regs)) {
		irqentry_enter_from_user_mode(regs);
		return ret;
	}

	/*
	 * If this entry hit the idle task invoke rcu_irq_enter() whether
	 * RCU is watching or not.
	 *
	 * Interupts can nest when the first interrupt invokes softirq
	 * processing on return which enables interrupts.
	 *
	 * Scheduler ticks in the idle task can mark quiescent state and
	 * terminate a grace period, if and only if the timer interrupt is
	 * not nested into another interrupt.
	 *
	 * Checking for __rcu_is_watching() here would prevent the nesting
	 * interrupt to invoke rcu_irq_enter(). If that nested interrupt is
	 * the tick then rcu_flavor_sched_clock_irq() would wrongfully
	 * assume that it is the first interupt and eventually claim
	 * quiescient state and end grace periods prematurely.
	 *
	 * Unconditionally invoke rcu_irq_enter() so RCU state stays
	 * consistent.
	 *
	 * TINY_RCU does not support EQS, so let the compiler eliminate
	 * this part when enabled.
	 */
	if (!IS_ENABLED(CONFIG_TINY_RCU) && is_idle_task(current)) {
		/*
		 * If RCU is not watching then the same careful
		 * sequence vs. lockdep and tracing is required
		 * as in irq_enter_from_user_mode().
		 */
		lockdep_hardirqs_off(CALLER_ADDR0);
		rcu_irq_enter();
		instrumentation_begin();
		trace_hardirqs_off_finish();
		instrumentation_end();

		ret.exit_rcu = true;
		return ret;
	}

	/*
	 * If RCU is watching then RCU only wants to check whether it needs
	 * to restart the tick in NOHZ mode. rcu_irq_enter_check_tick()
	 * already contains a warning when RCU is not watching, so no point
	 * in having another one here.
	 */
	instrumentation_begin();
	rcu_irq_enter_check_tick();
	/* Use the combo lockdep/tracing function */
	trace_hardirqs_off();
	instrumentation_end();

	return ret;
}

void irqentry_exit_cond_resched(void)
{
	if (!preempt_count()) {
		/* Sanity check RCU and thread stack */
		rcu_irq_exit_check_preempt();
		if (IS_ENABLED(CONFIG_DEBUG_ENTRY))
			WARN_ON_ONCE(!on_thread_stack());
		if (need_resched())
			preempt_schedule_irq();
	}
}

noinstr void irqentry_exit(struct pt_regs *regs, irqentry_state_t state)
{
	lockdep_assert_irqs_disabled();

	/* Check whether this returns to user mode */
	if (user_mode(regs)) {
		irqentry_exit_to_user_mode(regs);
	} else if (!regs_irqs_disabled(regs)) {
		/*
		 * If RCU was not watching on entry this needs to be done
		 * carefully and needs the same ordering of lockdep/tracing
		 * and RCU as the return to user mode path.
		 */
		if (state.exit_rcu) {
			instrumentation_begin();
			/* Tell the tracer that IRET will enable interrupts */
			trace_hardirqs_on_prepare();
			lockdep_hardirqs_on_prepare(CALLER_ADDR0);
			instrumentation_end();
			rcu_irq_exit();
			lockdep_hardirqs_on(CALLER_ADDR0);
			return;
		}

		instrumentation_begin();
		if (IS_ENABLED(CONFIG_PREEMPTION))
			irqentry_exit_cond_resched();
		/* Covers both tracing and lockdep */
		trace_hardirqs_on();
		instrumentation_end();
	} else {
		/*
		 * IRQ flags state is correct already. Just tell RCU if it
		 * was not watching on entry.
		 */
		if (state.exit_rcu)
			rcu_irq_exit();
	}
}
