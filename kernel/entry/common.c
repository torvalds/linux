// SPDX-License-Identifier: GPL-2.0

#include <linux/irq-entry-common.h>
#include <linux/resume_user_mode.h>
#include <linux/highmem.h>
#include <linux/jump_label.h>
#include <linux/kmsan.h>
#include <linux/livepatch.h>
#include <linux/tick.h>

/* Workaround to allow gradual conversion of architecture code */
void __weak arch_do_signal_or_restart(struct pt_regs *regs) { }

#ifdef CONFIG_HAVE_GENERIC_TIF_BITS
#define EXIT_TO_USER_MODE_WORK_LOOP	(EXIT_TO_USER_MODE_WORK & ~_TIF_RSEQ)
#else
#define EXIT_TO_USER_MODE_WORK_LOOP	(EXIT_TO_USER_MODE_WORK)
#endif

/* TIF bits, which prevent a time slice extension. */
#ifdef CONFIG_PREEMPT_RT
/*
 * Since rseq slice ext has a direct correlation to the worst case
 * scheduling latency (schedule is delayed after all), only have it affect
 * LAZY reschedules on PREEMPT_RT for now.
 *
 * However, since this delay is only applicable to userspace, a value
 * for rseq_slice_extension_nsec that is strictly less than the worst case
 * kernel space preempt_disable() region, should mean the scheduling latency
 * is not affected, even for !LAZY.
 *
 * However, since this value depends on the hardware at hand, it cannot be
 * pre-determined in any sensible way. Hence punt on this problem for now.
 */
# define TIF_SLICE_EXT_SCHED	(_TIF_NEED_RESCHED_LAZY)
#else
# define TIF_SLICE_EXT_SCHED	(_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY)
#endif
#define TIF_SLICE_EXT_DENY	(EXIT_TO_USER_MODE_WORK & ~TIF_SLICE_EXT_SCHED)

static __always_inline unsigned long __exit_to_user_mode_loop(struct pt_regs *regs,
							      unsigned long ti_work)
{
	/*
	 * Before returning to user space ensure that all pending work
	 * items have been completed.
	 */
	while (ti_work & EXIT_TO_USER_MODE_WORK_LOOP) {

		local_irq_enable();

		if (ti_work & (_TIF_NEED_RESCHED | _TIF_NEED_RESCHED_LAZY)) {
			if (!rseq_grant_slice_extension(ti_work, TIF_SLICE_EXT_DENY))
				schedule();
		}

		if (ti_work & _TIF_UPROBE)
			uprobe_notify_resume(regs);

		if (ti_work & _TIF_PATCH_PENDING)
			klp_update_patch_state(current);

		if (ti_work & (_TIF_SIGPENDING | _TIF_NOTIFY_SIGNAL))
			arch_do_signal_or_restart(regs);

		if (ti_work & _TIF_NOTIFY_RESUME)
			resume_user_mode_work(regs);

		/* Architecture specific TIF work */
		arch_exit_to_user_mode_work(regs, ti_work);

		/*
		 * Disable interrupts and reevaluate the work flags as they
		 * might have changed while interrupts and preemption was
		 * enabled above.
		 */
		local_irq_disable();

		/* Check if any of the above work has queued a deferred wakeup */
		tick_nohz_user_enter_prepare();

		ti_work = read_thread_flags();
	}

	/* Return the latest work state for arch_exit_to_user_mode() */
	return ti_work;
}

/**
 * exit_to_user_mode_loop - do any pending work before leaving to user space
 * @regs:	Pointer to pt_regs on entry stack
 * @ti_work:	TIF work flags as read by the caller
 */
__always_inline unsigned long exit_to_user_mode_loop(struct pt_regs *regs,
						     unsigned long ti_work)
{
	for (;;) {
		ti_work = __exit_to_user_mode_loop(regs, ti_work);

		if (likely(!rseq_exit_to_user_mode_restart(regs, ti_work)))
			return ti_work;
		ti_work = read_thread_flags();
	}
}

noinstr irqentry_state_t irqentry_enter(struct pt_regs *regs)
{
	if (user_mode(regs)) {
		irqentry_state_t ret = {
			.exit_rcu = false,
		};

		irqentry_enter_from_user_mode(regs);
		return ret;
	}

	return irqentry_enter_from_kernel_mode(regs);
}

/**
 * arch_irqentry_exit_need_resched - Architecture specific need resched function
 *
 * Invoked from raw_irqentry_exit_cond_resched() to check if resched is needed.
 * Defaults return true.
 *
 * The main purpose is to permit arch to avoid preemption of a task from an IRQ.
 */
static inline bool arch_irqentry_exit_need_resched(void);

#ifndef arch_irqentry_exit_need_resched
static inline bool arch_irqentry_exit_need_resched(void) { return true; }
#endif

void raw_irqentry_exit_cond_resched(void)
{
	if (!preempt_count()) {
		/* Sanity check RCU and thread stack */
		rcu_irq_exit_check_preempt();
		if (IS_ENABLED(CONFIG_DEBUG_ENTRY))
			WARN_ON_ONCE(!on_thread_stack());
		if (need_resched() && arch_irqentry_exit_need_resched())
			preempt_schedule_irq();
	}
}
#ifdef CONFIG_PREEMPT_DYNAMIC
#if defined(CONFIG_HAVE_PREEMPT_DYNAMIC_CALL)
DEFINE_STATIC_CALL(irqentry_exit_cond_resched, raw_irqentry_exit_cond_resched);
#elif defined(CONFIG_HAVE_PREEMPT_DYNAMIC_KEY)
DEFINE_STATIC_KEY_TRUE(sk_dynamic_irqentry_exit_cond_resched);
void dynamic_irqentry_exit_cond_resched(void)
{
	if (!static_branch_unlikely(&sk_dynamic_irqentry_exit_cond_resched))
		return;
	raw_irqentry_exit_cond_resched();
}
#endif
#endif

noinstr void irqentry_exit(struct pt_regs *regs, irqentry_state_t state)
{
	if (user_mode(regs))
		irqentry_exit_to_user_mode(regs);
	else
		irqentry_exit_to_kernel_mode(regs, state);
}

irqentry_state_t noinstr irqentry_nmi_enter(struct pt_regs *regs)
{
	irqentry_state_t irq_state;

	irq_state.lockdep = lockdep_hardirqs_enabled();

	__nmi_enter();
	lockdep_hardirqs_off(CALLER_ADDR0);
	lockdep_hardirq_enter();
	ct_nmi_enter();

	instrumentation_begin();
	kmsan_unpoison_entry_regs(regs);
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
		lockdep_hardirqs_on_prepare();
	}
	instrumentation_end();

	ct_nmi_exit();
	lockdep_hardirq_exit();
	if (irq_state.lockdep)
		lockdep_hardirqs_on(CALLER_ADDR0);
	__nmi_exit();
}
