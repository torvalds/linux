// SPDX-License-Identifier: GPL-2.0

#include <linux/context_tracking.h>
#include <linux/entry-common.h>
#include <linux/highmem.h>
#include <linux/livepatch.h>
#include <linux/audit.h>
#include <linux/tick.h>

#include "common.h"

#define CREATE_TRACE_POINTS
#include <trace/events/syscalls.h>

/* See comment for enter_from_user_mode() in entry-common.h */
static __always_inline void __enter_from_user_mode(struct pt_regs *regs)
{
	arch_check_user_regs(regs);
	lockdep_hardirqs_off(CALLER_ADDR0);

	CT_WARN_ON(ct_state() != CONTEXT_USER);
	user_exit_irqoff();

	instrumentation_begin();
	trace_hardirqs_off_finish();
	instrumentation_end();
}

void noinstr enter_from_user_mode(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
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
				unsigned long work)
{
	long ret = 0;

	/*
	 * Handle Syscall User Dispatch.  This must comes first, since
	 * the ABI here can be something that doesn't make sense for
	 * other syscall_work features.
	 */
	if (work & SYSCALL_WORK_SYSCALL_USER_DISPATCH) {
		if (syscall_user_dispatch(regs))
			return -1L;
	}

	/* Handle ptrace */
	if (work & (SYSCALL_WORK_SYSCALL_TRACE | SYSCALL_WORK_SYSCALL_EMU)) {
		ret = arch_syscall_enter_tracehook(regs);
		if (ret || (work & SYSCALL_WORK_SYSCALL_EMU))
			return -1L;
	}

	/* Do seccomp after ptrace, to catch any tracer changes. */
	if (work & SYSCALL_WORK_SECCOMP) {
		ret = __secure_computing(NULL);
		if (ret == -1L)
			return ret;
	}

	/* Either of the above might have changed the syscall number */
	syscall = syscall_get_nr(current, regs);

	if (unlikely(work & SYSCALL_WORK_SYSCALL_TRACEPOINT))
		trace_sys_enter(regs, syscall);

	syscall_enter_audit(regs, syscall);

	return ret ? : syscall;
}

static __always_inline long
__syscall_enter_from_user_work(struct pt_regs *regs, long syscall)
{
	unsigned long work = READ_ONCE(current_thread_info()->syscall_work);

	if (work & SYSCALL_WORK_ENTER)
		syscall = syscall_trace_enter(regs, syscall, work);

	return syscall;
}

long syscall_enter_from_user_mode_work(struct pt_regs *regs, long syscall)
{
	return __syscall_enter_from_user_work(regs, syscall);
}

noinstr long syscall_enter_from_user_mode(struct pt_regs *regs, long syscall)
{
	long ret;

	__enter_from_user_mode(regs);

	instrumentation_begin();
	local_irq_enable();
	ret = __syscall_enter_from_user_work(regs, syscall);
	instrumentation_end();

	return ret;
}

noinstr void syscall_enter_from_user_mode_prepare(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
	instrumentation_begin();
	local_irq_enable();
	instrumentation_end();
}

/* See comment for exit_to_user_mode() in entry-common.h */
static __always_inline void __exit_to_user_mode(void)
{
	instrumentation_begin();
	trace_hardirqs_on_prepare();
	lockdep_hardirqs_on_prepare(CALLER_ADDR0);
	instrumentation_end();

	user_enter_irqoff();
	arch_exit_to_user_mode();
	lockdep_hardirqs_on(CALLER_ADDR0);
}

void noinstr exit_to_user_mode(void)
{
	__exit_to_user_mode();
}

/* Workaround to allow gradual conversion of architecture code */
void __weak arch_do_signal_or_restart(struct pt_regs *regs, bool has_signal) { }

static void handle_signal_work(struct pt_regs *regs, unsigned long ti_work)
{
	if (ti_work & _TIF_NOTIFY_SIGNAL)
		tracehook_notify_signal();

	arch_do_signal_or_restart(regs, ti_work & _TIF_SIGPENDING);
}

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

		if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			handle_signal_work(regs, ti_work);

		if (ti_work & _TIF_NOTIFY_RESUME) {
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

		/* Check if any of the above work has queued a deferred wakeup */
		tick_nohz_user_enter_prepare();

		ti_work = READ_ONCE(current_thread_info()->flags);
	}

	/* Return the latest work state for arch_exit_to_user_mode() */
	return ti_work;
}

static void exit_to_user_mode_prepare(struct pt_regs *regs)
{
	unsigned long ti_work = READ_ONCE(current_thread_info()->flags);

	lockdep_assert_irqs_disabled();

	/* Flush pending rcuog wakeup before the last need_resched() check */
	tick_nohz_user_enter_prepare();

	if (unlikely(ti_work & EXIT_TO_USER_MODE_WORK))
		ti_work = exit_to_user_mode_loop(regs, ti_work);

	arch_exit_to_user_mode_prepare(regs, ti_work);

	/* Ensure that the address limit is intact and no locks are held */
	addr_limit_user_check();
	kmap_assert_nomap();
	lockdep_assert_irqs_disabled();
	lockdep_sys_exit();
}

/*
 * If SYSCALL_EMU is set, then the only reason to report is when
 * SINGLESTEP is set (i.e. PTRACE_SYSEMU_SINGLESTEP).  This syscall
 * instruction has been already reported in syscall_enter_from_user_mode().
 */
static inline bool report_single_step(unsigned long work)
{
	if (work & SYSCALL_WORK_SYSCALL_EMU)
		return false;

	return work & SYSCALL_WORK_SYSCALL_EXIT_TRAP;
}

static void syscall_exit_work(struct pt_regs *regs, unsigned long work)
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
		trace_sys_exit(regs, syscall_get_return_value(current, regs));

	step = report_single_step(work);
	if (step || work & SYSCALL_WORK_SYSCALL_TRACE)
		arch_syscall_exit_tracehook(regs, step);
}

/*
 * Syscall specific exit to user mode preparation. Runs with interrupts
 * enabled.
 */
static void syscall_exit_to_user_mode_prepare(struct pt_regs *regs)
{
	unsigned long work = READ_ONCE(current_thread_info()->syscall_work);
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
	if (unlikely(work & SYSCALL_WORK_EXIT))
		syscall_exit_work(regs, work);
}

static __always_inline void __syscall_exit_to_user_mode_work(struct pt_regs *regs)
{
	syscall_exit_to_user_mode_prepare(regs);
	local_irq_disable_exit_to_user();
	exit_to_user_mode_prepare(regs);
}

void syscall_exit_to_user_mode_work(struct pt_regs *regs)
{
	__syscall_exit_to_user_mode_work(regs);
}

__visible noinstr void syscall_exit_to_user_mode(struct pt_regs *regs)
{
	instrumentation_begin();
	__syscall_exit_to_user_mode_work(regs);
	instrumentation_end();
	__exit_to_user_mode();
}

noinstr void irqentry_enter_from_user_mode(struct pt_regs *regs)
{
	__enter_from_user_mode(regs);
}

noinstr void irqentry_exit_to_user_mode(struct pt_regs *regs)
{
	instrumentation_begin();
	exit_to_user_mode_prepare(regs);
	instrumentation_end();
	__exit_to_user_mode();
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
	 * Interrupts can nest when the first interrupt invokes softirq
	 * processing on return which enables interrupts.
	 *
	 * Scheduler ticks in the idle task can mark quiescent state and
	 * terminate a grace period, if and only if the timer interrupt is
	 * not nested into another interrupt.
	 *
	 * Checking for rcu_is_watching() here would prevent the nesting
	 * interrupt to invoke rcu_irq_enter(). If that nested interrupt is
	 * the tick then rcu_flavor_sched_clock_irq() would wrongfully
	 * assume that it is the first interrupt and eventually claim
	 * quiescent state and end grace periods prematurely.
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
		 * as in irqentry_enter_from_user_mode().
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
	lockdep_hardirqs_off(CALLER_ADDR0);
	instrumentation_begin();
	rcu_irq_enter_check_tick();
	trace_hardirqs_off_finish();
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
#ifdef CONFIG_PREEMPT_DYNAMIC
DEFINE_STATIC_CALL(irqentry_exit_cond_resched, irqentry_exit_cond_resched);
#endif

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
		if (IS_ENABLED(CONFIG_PREEMPTION)) {
#ifdef CONFIG_PREEMPT_DYNAMIC
			static_call(irqentry_exit_cond_resched)();
#else
			irqentry_exit_cond_resched();
#endif
		}
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

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs)
{
	irqentry_state_t irq_state;

	irq_state.lockdep = lockdep_hardirqs_enabled();

	__nmi_enter();
	lockdep_hardirqs_off(CALLER_ADDR0);
	lockdep_hardirq_enter();
	rcu_nmi_enter();

	instrumentation_begin();
	trace_hardirqs_off_finish();
	ftrace_nmi_enter();
	instrumentation_end();

	return irq_state;
}

void noinstr irqentry_nmi_exit(struct pt_regs *regs, irqentry_state_t irq_state)
{
	instrumentation_begin();
	ftrace_nmi_exit();
	if (irq_state.lockdep) {
		trace_hardirqs_on_prepare();
		lockdep_hardirqs_on_prepare(CALLER_ADDR0);
	}
	instrumentation_end();

	rcu_nmi_exit();
	lockdep_hardirq_exit();
	if (irq_state.lockdep)
		lockdep_hardirqs_on(CALLER_ADDR0);
	__nmi_exit();
}
