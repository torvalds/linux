// SPDX-License-Identifier: GPL-2.0-only
/*
 * Context tracking: Probe on high level context boundaries such as kernel,
 * userspace, guest or idle.
 *
 * This is used by RCU to remove its dependency on the timer tick while a CPU
 * runs in idle, userspace or guest mode.
 *
 * User/guest tracking started by Frederic Weisbecker:
 *
 * Copyright (C) 2012 Red Hat, Inc., Frederic Weisbecker
 *
 * Many thanks to Gilad Ben-Yossef, Paul McKenney, Ingo Molnar, Andrew Morton,
 * Steven Rostedt, Peter Zijlstra for suggestions and improvements.
 *
 * RCU extended quiescent state bits imported from kernel/rcu/tree.c
 * where the relevant authorship may be found.
 */

#include <linux/context_tracking.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/export.h>
#include <linux/kprobes.h>
#include <trace/events/rcu.h>


DEFINE_PER_CPU(struct context_tracking, context_tracking) = {
#ifdef CONFIG_CONTEXT_TRACKING_IDLE
	.dynticks_nesting = 1,
	.dynticks_nmi_nesting = DYNTICK_IRQ_NONIDLE,
#endif
	.state = ATOMIC_INIT(RCU_DYNTICKS_IDX),
};
EXPORT_SYMBOL_GPL(context_tracking);

#ifdef CONFIG_CONTEXT_TRACKING_IDLE
#define TPS(x)  tracepoint_string(x)

/* Record the current task on dyntick-idle entry. */
static __always_inline void rcu_dynticks_task_enter(void)
{
#if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL)
	WRITE_ONCE(current->rcu_tasks_idle_cpu, smp_processor_id());
#endif /* #if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL) */
}

/* Record no current task on dyntick-idle exit. */
static __always_inline void rcu_dynticks_task_exit(void)
{
#if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL)
	WRITE_ONCE(current->rcu_tasks_idle_cpu, -1);
#endif /* #if defined(CONFIG_TASKS_RCU) && defined(CONFIG_NO_HZ_FULL) */
}

/* Turn on heavyweight RCU tasks trace readers on idle/user entry. */
static __always_inline void rcu_dynticks_task_trace_enter(void)
{
#ifdef CONFIG_TASKS_TRACE_RCU
	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB))
		current->trc_reader_special.b.need_mb = true;
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */
}

/* Turn off heavyweight RCU tasks trace readers on idle/user exit. */
static __always_inline void rcu_dynticks_task_trace_exit(void)
{
#ifdef CONFIG_TASKS_TRACE_RCU
	if (IS_ENABLED(CONFIG_TASKS_TRACE_RCU_READ_MB))
		current->trc_reader_special.b.need_mb = false;
#endif /* #ifdef CONFIG_TASKS_TRACE_RCU */
}

/*
 * Record entry into an extended quiescent state.  This is only to be
 * called when not already in an extended quiescent state, that is,
 * RCU is watching prior to the call to this function and is no longer
 * watching upon return.
 */
static noinstr void ct_kernel_exit_state(int offset)
{
	int seq;

	/*
	 * CPUs seeing atomic_add_return() must see prior RCU read-side
	 * critical sections, and we also must force ordering with the
	 * next idle sojourn.
	 */
	rcu_dynticks_task_trace_enter();  // Before ->dynticks update!
	seq = ct_state_inc(offset);
	// RCU is no longer watching.  Better be in extended quiescent state!
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && (seq & RCU_DYNTICKS_IDX));
}

/*
 * Record exit from an extended quiescent state.  This is only to be
 * called from an extended quiescent state, that is, RCU is not watching
 * prior to the call to this function and is watching upon return.
 */
static noinstr void ct_kernel_enter_state(int offset)
{
	int seq;

	/*
	 * CPUs seeing atomic_add_return() must see prior idle sojourns,
	 * and we also must force ordering with the next RCU read-side
	 * critical section.
	 */
	seq = ct_state_inc(offset);
	// RCU is now watching.  Better not be in an extended quiescent state!
	rcu_dynticks_task_trace_exit();  // After ->dynticks update!
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && !(seq & RCU_DYNTICKS_IDX));
}

/*
 * Enter an RCU extended quiescent state, which can be either the
 * idle loop or adaptive-tickless usermode execution.
 *
 * We crowbar the ->dynticks_nmi_nesting field to zero to allow for
 * the possibility of usermode upcalls having messed up our count
 * of interrupt nesting level during the prior busy period.
 */
static void noinstr ct_kernel_exit(bool user, int offset)
{
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);

	WARN_ON_ONCE(ct_dynticks_nmi_nesting() != DYNTICK_IRQ_NONIDLE);
	WRITE_ONCE(ct->dynticks_nmi_nesting, 0);
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) &&
		     ct_dynticks_nesting() == 0);
	if (ct_dynticks_nesting() != 1) {
		// RCU will still be watching, so just do accounting and leave.
		ct->dynticks_nesting--;
		return;
	}

	instrumentation_begin();
	lockdep_assert_irqs_disabled();
	trace_rcu_dyntick(TPS("Start"), ct_dynticks_nesting(), 0, ct_dynticks());
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && !user && !is_idle_task(current));
	rcu_preempt_deferred_qs(current);

	// instrumentation for the noinstr ct_kernel_exit_state()
	instrument_atomic_write(&ct->state, sizeof(ct->state));

	instrumentation_end();
	WRITE_ONCE(ct->dynticks_nesting, 0); /* Avoid irq-access tearing. */
	// RCU is watching here ...
	ct_kernel_exit_state(offset);
	// ... but is no longer watching here.
	rcu_dynticks_task_enter();
}

/*
 * Exit an RCU extended quiescent state, which can be either the
 * idle loop or adaptive-tickless usermode execution.
 *
 * We crowbar the ->dynticks_nmi_nesting field to DYNTICK_IRQ_NONIDLE to
 * allow for the possibility of usermode upcalls messing up our count of
 * interrupt nesting level during the busy period that is just now starting.
 */
static void noinstr ct_kernel_enter(bool user, int offset)
{
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);
	long oldval;

	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && !raw_irqs_disabled());
	oldval = ct_dynticks_nesting();
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && oldval < 0);
	if (oldval) {
		// RCU was already watching, so just do accounting and leave.
		ct->dynticks_nesting++;
		return;
	}
	rcu_dynticks_task_exit();
	// RCU is not watching here ...
	ct_kernel_enter_state(offset);
	// ... but is watching here.
	instrumentation_begin();

	// instrumentation for the noinstr ct_kernel_enter_state()
	instrument_atomic_write(&ct->state, sizeof(ct->state));

	trace_rcu_dyntick(TPS("End"), ct_dynticks_nesting(), 1, ct_dynticks());
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && !user && !is_idle_task(current));
	WRITE_ONCE(ct->dynticks_nesting, 1);
	WARN_ON_ONCE(ct_dynticks_nmi_nesting());
	WRITE_ONCE(ct->dynticks_nmi_nesting, DYNTICK_IRQ_NONIDLE);
	instrumentation_end();
}

/**
 * ct_nmi_exit - inform RCU of exit from NMI context
 *
 * If we are returning from the outermost NMI handler that interrupted an
 * RCU-idle period, update ct->state and ct->dynticks_nmi_nesting
 * to let the RCU grace-period handling know that the CPU is back to
 * being RCU-idle.
 *
 * If you add or remove a call to ct_nmi_exit(), be sure to test
 * with CONFIG_RCU_EQS_DEBUG=y.
 */
void noinstr ct_nmi_exit(void)
{
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);

	instrumentation_begin();
	/*
	 * Check for ->dynticks_nmi_nesting underflow and bad ->dynticks.
	 * (We are exiting an NMI handler, so RCU better be paying attention
	 * to us!)
	 */
	WARN_ON_ONCE(ct_dynticks_nmi_nesting() <= 0);
	WARN_ON_ONCE(rcu_dynticks_curr_cpu_in_eqs());

	/*
	 * If the nesting level is not 1, the CPU wasn't RCU-idle, so
	 * leave it in non-RCU-idle state.
	 */
	if (ct_dynticks_nmi_nesting() != 1) {
		trace_rcu_dyntick(TPS("--="), ct_dynticks_nmi_nesting(), ct_dynticks_nmi_nesting() - 2,
				  ct_dynticks());
		WRITE_ONCE(ct->dynticks_nmi_nesting, /* No store tearing. */
			   ct_dynticks_nmi_nesting() - 2);
		instrumentation_end();
		return;
	}

	/* This NMI interrupted an RCU-idle CPU, restore RCU-idleness. */
	trace_rcu_dyntick(TPS("Startirq"), ct_dynticks_nmi_nesting(), 0, ct_dynticks());
	WRITE_ONCE(ct->dynticks_nmi_nesting, 0); /* Avoid store tearing. */

	// instrumentation for the noinstr ct_kernel_exit_state()
	instrument_atomic_write(&ct->state, sizeof(ct->state));
	instrumentation_end();

	// RCU is watching here ...
	ct_kernel_exit_state(RCU_DYNTICKS_IDX);
	// ... but is no longer watching here.

	if (!in_nmi())
		rcu_dynticks_task_enter();
}

/**
 * ct_nmi_enter - inform RCU of entry to NMI context
 *
 * If the CPU was idle from RCU's viewpoint, update ct->state and
 * ct->dynticks_nmi_nesting to let the RCU grace-period handling know
 * that the CPU is active.  This implementation permits nested NMIs, as
 * long as the nesting level does not overflow an int.  (You will probably
 * run out of stack space first.)
 *
 * If you add or remove a call to ct_nmi_enter(), be sure to test
 * with CONFIG_RCU_EQS_DEBUG=y.
 */
void noinstr ct_nmi_enter(void)
{
	long incby = 2;
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);

	/* Complain about underflow. */
	WARN_ON_ONCE(ct_dynticks_nmi_nesting() < 0);

	/*
	 * If idle from RCU viewpoint, atomically increment ->dynticks
	 * to mark non-idle and increment ->dynticks_nmi_nesting by one.
	 * Otherwise, increment ->dynticks_nmi_nesting by two.  This means
	 * if ->dynticks_nmi_nesting is equal to one, we are guaranteed
	 * to be in the outermost NMI handler that interrupted an RCU-idle
	 * period (observation due to Andy Lutomirski).
	 */
	if (rcu_dynticks_curr_cpu_in_eqs()) {

		if (!in_nmi())
			rcu_dynticks_task_exit();

		// RCU is not watching here ...
		ct_kernel_enter_state(RCU_DYNTICKS_IDX);
		// ... but is watching here.

		instrumentation_begin();
		// instrumentation for the noinstr rcu_dynticks_curr_cpu_in_eqs()
		instrument_atomic_read(&ct->state, sizeof(ct->state));
		// instrumentation for the noinstr ct_kernel_enter_state()
		instrument_atomic_write(&ct->state, sizeof(ct->state));

		incby = 1;
	} else if (!in_nmi()) {
		instrumentation_begin();
		rcu_irq_enter_check_tick();
	} else  {
		instrumentation_begin();
	}

	trace_rcu_dyntick(incby == 1 ? TPS("Endirq") : TPS("++="),
			  ct_dynticks_nmi_nesting(),
			  ct_dynticks_nmi_nesting() + incby, ct_dynticks());
	instrumentation_end();
	WRITE_ONCE(ct->dynticks_nmi_nesting, /* Prevent store tearing. */
		   ct_dynticks_nmi_nesting() + incby);
	barrier();
}

/**
 * ct_idle_enter - inform RCU that current CPU is entering idle
 *
 * Enter idle mode, in other words, -leave- the mode in which RCU
 * read-side critical sections can occur.  (Though RCU read-side
 * critical sections can occur in irq handlers in idle, a possibility
 * handled by irq_enter() and irq_exit().)
 *
 * If you add or remove a call to ct_idle_enter(), be sure to test with
 * CONFIG_RCU_EQS_DEBUG=y.
 */
void noinstr ct_idle_enter(void)
{
	WARN_ON_ONCE(IS_ENABLED(CONFIG_RCU_EQS_DEBUG) && !raw_irqs_disabled());
	ct_kernel_exit(false, RCU_DYNTICKS_IDX + CONTEXT_IDLE);
}
EXPORT_SYMBOL_GPL(ct_idle_enter);

/**
 * ct_idle_exit - inform RCU that current CPU is leaving idle
 *
 * Exit idle mode, in other words, -enter- the mode in which RCU
 * read-side critical sections can occur.
 *
 * If you add or remove a call to ct_idle_exit(), be sure to test with
 * CONFIG_RCU_EQS_DEBUG=y.
 */
void noinstr ct_idle_exit(void)
{
	unsigned long flags;

	raw_local_irq_save(flags);
	ct_kernel_enter(false, RCU_DYNTICKS_IDX - CONTEXT_IDLE);
	raw_local_irq_restore(flags);
}
EXPORT_SYMBOL_GPL(ct_idle_exit);

/**
 * ct_irq_enter - inform RCU that current CPU is entering irq away from idle
 *
 * Enter an interrupt handler, which might possibly result in exiting
 * idle mode, in other words, entering the mode in which read-side critical
 * sections can occur.  The caller must have disabled interrupts.
 *
 * Note that the Linux kernel is fully capable of entering an interrupt
 * handler that it never exits, for example when doing upcalls to user mode!
 * This code assumes that the idle loop never does upcalls to user mode.
 * If your architecture's idle loop does do upcalls to user mode (or does
 * anything else that results in unbalanced calls to the irq_enter() and
 * irq_exit() functions), RCU will give you what you deserve, good and hard.
 * But very infrequently and irreproducibly.
 *
 * Use things like work queues to work around this limitation.
 *
 * You have been warned.
 *
 * If you add or remove a call to ct_irq_enter(), be sure to test with
 * CONFIG_RCU_EQS_DEBUG=y.
 */
noinstr void ct_irq_enter(void)
{
	lockdep_assert_irqs_disabled();
	ct_nmi_enter();
}

/**
 * ct_irq_exit - inform RCU that current CPU is exiting irq towards idle
 *
 * Exit from an interrupt handler, which might possibly result in entering
 * idle mode, in other words, leaving the mode in which read-side critical
 * sections can occur.  The caller must have disabled interrupts.
 *
 * This code assumes that the idle loop never does anything that might
 * result in unbalanced calls to irq_enter() and irq_exit().  If your
 * architecture's idle loop violates this assumption, RCU will give you what
 * you deserve, good and hard.  But very infrequently and irreproducibly.
 *
 * Use things like work queues to work around this limitation.
 *
 * You have been warned.
 *
 * If you add or remove a call to ct_irq_exit(), be sure to test with
 * CONFIG_RCU_EQS_DEBUG=y.
 */
noinstr void ct_irq_exit(void)
{
	lockdep_assert_irqs_disabled();
	ct_nmi_exit();
}

/*
 * Wrapper for ct_irq_enter() where interrupts are enabled.
 *
 * If you add or remove a call to ct_irq_enter_irqson(), be sure to test
 * with CONFIG_RCU_EQS_DEBUG=y.
 */
void ct_irq_enter_irqson(void)
{
	unsigned long flags;

	local_irq_save(flags);
	ct_irq_enter();
	local_irq_restore(flags);
}

/*
 * Wrapper for ct_irq_exit() where interrupts are enabled.
 *
 * If you add or remove a call to ct_irq_exit_irqson(), be sure to test
 * with CONFIG_RCU_EQS_DEBUG=y.
 */
void ct_irq_exit_irqson(void)
{
	unsigned long flags;

	local_irq_save(flags);
	ct_irq_exit();
	local_irq_restore(flags);
}
#else
static __always_inline void ct_kernel_exit(bool user, int offset) { }
static __always_inline void ct_kernel_enter(bool user, int offset) { }
#endif /* #ifdef CONFIG_CONTEXT_TRACKING_IDLE */

#ifdef CONFIG_CONTEXT_TRACKING_USER

#define CREATE_TRACE_POINTS
#include <trace/events/context_tracking.h>

DEFINE_STATIC_KEY_FALSE(context_tracking_key);
EXPORT_SYMBOL_GPL(context_tracking_key);

static noinstr bool context_tracking_recursion_enter(void)
{
	int recursion;

	recursion = __this_cpu_inc_return(context_tracking.recursion);
	if (recursion == 1)
		return true;

	WARN_ONCE((recursion < 1), "Invalid context tracking recursion value %d\n", recursion);
	__this_cpu_dec(context_tracking.recursion);

	return false;
}

static __always_inline void context_tracking_recursion_exit(void)
{
	__this_cpu_dec(context_tracking.recursion);
}

/**
 * __ct_user_enter - Inform the context tracking that the CPU is going
 *		     to enter user or guest space mode.
 *
 * This function must be called right before we switch from the kernel
 * to user or guest space, when it's guaranteed the remaining kernel
 * instructions to execute won't use any RCU read side critical section
 * because this function sets RCU in extended quiescent state.
 */
void noinstr __ct_user_enter(enum ctx_state state)
{
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);
	lockdep_assert_irqs_disabled();

	/* Kernel threads aren't supposed to go to userspace */
	WARN_ON_ONCE(!current->mm);

	if (!context_tracking_recursion_enter())
		return;

	if (__ct_state() != state) {
		if (ct->active) {
			/*
			 * At this stage, only low level arch entry code remains and
			 * then we'll run in userspace. We can assume there won't be
			 * any RCU read-side critical section until the next call to
			 * user_exit() or ct_irq_enter(). Let's remove RCU's dependency
			 * on the tick.
			 */
			if (state == CONTEXT_USER) {
				instrumentation_begin();
				trace_user_enter(0);
				vtime_user_enter(current);
				instrumentation_end();
			}
			/*
			 * Other than generic entry implementation, we may be past the last
			 * rescheduling opportunity in the entry code. Trigger a self IPI
			 * that will fire and reschedule once we resume in user/guest mode.
			 */
			rcu_irq_work_resched();

			/*
			 * Enter RCU idle mode right before resuming userspace.  No use of RCU
			 * is permitted between this call and rcu_eqs_exit(). This way the
			 * CPU doesn't need to maintain the tick for RCU maintenance purposes
			 * when the CPU runs in userspace.
			 */
			ct_kernel_exit(true, RCU_DYNTICKS_IDX + state);

			/*
			 * Special case if we only track user <-> kernel transitions for tickless
			 * cputime accounting but we don't support RCU extended quiescent state.
			 * In this we case we don't care about any concurrency/ordering.
			 */
			if (!IS_ENABLED(CONFIG_CONTEXT_TRACKING_IDLE))
				raw_atomic_set(&ct->state, state);
		} else {
			/*
			 * Even if context tracking is disabled on this CPU, because it's outside
			 * the full dynticks mask for example, we still have to keep track of the
			 * context transitions and states to prevent inconsistency on those of
			 * other CPUs.
			 * If a task triggers an exception in userspace, sleep on the exception
			 * handler and then migrate to another CPU, that new CPU must know where
			 * the exception returns by the time we call exception_exit().
			 * This information can only be provided by the previous CPU when it called
			 * exception_enter().
			 * OTOH we can spare the calls to vtime and RCU when context_tracking.active
			 * is false because we know that CPU is not tickless.
			 */
			if (!IS_ENABLED(CONFIG_CONTEXT_TRACKING_IDLE)) {
				/* Tracking for vtime only, no concurrent RCU EQS accounting */
				raw_atomic_set(&ct->state, state);
			} else {
				/*
				 * Tracking for vtime and RCU EQS. Make sure we don't race
				 * with NMIs. OTOH we don't care about ordering here since
				 * RCU only requires RCU_DYNTICKS_IDX increments to be fully
				 * ordered.
				 */
				raw_atomic_add(state, &ct->state);
			}
		}
	}
	context_tracking_recursion_exit();
}
EXPORT_SYMBOL_GPL(__ct_user_enter);

/*
 * OBSOLETE:
 * This function should be noinstr but the below local_irq_restore() is
 * unsafe because it involves illegal RCU uses through tracing and lockdep.
 * This is unlikely to be fixed as this function is obsolete. The preferred
 * way is to call __context_tracking_enter() through user_enter_irqoff()
 * or context_tracking_guest_enter(). It should be the arch entry code
 * responsibility to call into context tracking with IRQs disabled.
 */
void ct_user_enter(enum ctx_state state)
{
	unsigned long flags;

	/*
	 * Some contexts may involve an exception occuring in an irq,
	 * leading to that nesting:
	 * ct_irq_enter() rcu_eqs_exit(true) rcu_eqs_enter(true) ct_irq_exit()
	 * This would mess up the dyntick_nesting count though. And rcu_irq_*()
	 * helpers are enough to protect RCU uses inside the exception. So
	 * just return immediately if we detect we are in an IRQ.
	 */
	if (in_interrupt())
		return;

	local_irq_save(flags);
	__ct_user_enter(state);
	local_irq_restore(flags);
}
NOKPROBE_SYMBOL(ct_user_enter);
EXPORT_SYMBOL_GPL(ct_user_enter);

/**
 * user_enter_callable() - Unfortunate ASM callable version of user_enter() for
 *			   archs that didn't manage to check the context tracking
 *			   static key from low level code.
 *
 * This OBSOLETE function should be noinstr but it unsafely calls
 * local_irq_restore(), involving illegal RCU uses through tracing and lockdep.
 * This is unlikely to be fixed as this function is obsolete. The preferred
 * way is to call user_enter_irqoff(). It should be the arch entry code
 * responsibility to call into context tracking with IRQs disabled.
 */
void user_enter_callable(void)
{
	user_enter();
}
NOKPROBE_SYMBOL(user_enter_callable);

/**
 * __ct_user_exit - Inform the context tracking that the CPU is
 *		    exiting user or guest mode and entering the kernel.
 *
 * This function must be called after we entered the kernel from user or
 * guest space before any use of RCU read side critical section. This
 * potentially include any high level kernel code like syscalls, exceptions,
 * signal handling, etc...
 *
 * This call supports re-entrancy. This way it can be called from any exception
 * handler without needing to know if we came from userspace or not.
 */
void noinstr __ct_user_exit(enum ctx_state state)
{
	struct context_tracking *ct = this_cpu_ptr(&context_tracking);

	if (!context_tracking_recursion_enter())
		return;

	if (__ct_state() == state) {
		if (ct->active) {
			/*
			 * Exit RCU idle mode while entering the kernel because it can
			 * run a RCU read side critical section anytime.
			 */
			ct_kernel_enter(true, RCU_DYNTICKS_IDX - state);
			if (state == CONTEXT_USER) {
				instrumentation_begin();
				vtime_user_exit(current);
				trace_user_exit(0);
				instrumentation_end();
			}

			/*
			 * Special case if we only track user <-> kernel transitions for tickless
			 * cputime accounting but we don't support RCU extended quiescent state.
			 * In this we case we don't care about any concurrency/ordering.
			 */
			if (!IS_ENABLED(CONFIG_CONTEXT_TRACKING_IDLE))
				raw_atomic_set(&ct->state, CONTEXT_KERNEL);

		} else {
			if (!IS_ENABLED(CONFIG_CONTEXT_TRACKING_IDLE)) {
				/* Tracking for vtime only, no concurrent RCU EQS accounting */
				raw_atomic_set(&ct->state, CONTEXT_KERNEL);
			} else {
				/*
				 * Tracking for vtime and RCU EQS. Make sure we don't race
				 * with NMIs. OTOH we don't care about ordering here since
				 * RCU only requires RCU_DYNTICKS_IDX increments to be fully
				 * ordered.
				 */
				raw_atomic_sub(state, &ct->state);
			}
		}
	}
	context_tracking_recursion_exit();
}
EXPORT_SYMBOL_GPL(__ct_user_exit);

/*
 * OBSOLETE:
 * This function should be noinstr but the below local_irq_save() is
 * unsafe because it involves illegal RCU uses through tracing and lockdep.
 * This is unlikely to be fixed as this function is obsolete. The preferred
 * way is to call __context_tracking_exit() through user_exit_irqoff()
 * or context_tracking_guest_exit(). It should be the arch entry code
 * responsibility to call into context tracking with IRQs disabled.
 */
void ct_user_exit(enum ctx_state state)
{
	unsigned long flags;

	if (in_interrupt())
		return;

	local_irq_save(flags);
	__ct_user_exit(state);
	local_irq_restore(flags);
}
NOKPROBE_SYMBOL(ct_user_exit);
EXPORT_SYMBOL_GPL(ct_user_exit);

/**
 * user_exit_callable() - Unfortunate ASM callable version of user_exit() for
 *			  archs that didn't manage to check the context tracking
 *			  static key from low level code.
 *
 * This OBSOLETE function should be noinstr but it unsafely calls local_irq_save(),
 * involving illegal RCU uses through tracing and lockdep. This is unlikely
 * to be fixed as this function is obsolete. The preferred way is to call
 * user_exit_irqoff(). It should be the arch entry code responsibility to
 * call into context tracking with IRQs disabled.
 */
void user_exit_callable(void)
{
	user_exit();
}
NOKPROBE_SYMBOL(user_exit_callable);

void __init ct_cpu_track_user(int cpu)
{
	static __initdata bool initialized = false;

	if (!per_cpu(context_tracking.active, cpu)) {
		per_cpu(context_tracking.active, cpu) = true;
		static_branch_inc(&context_tracking_key);
	}

	if (initialized)
		return;

#ifdef CONFIG_HAVE_TIF_NOHZ
	/*
	 * Set TIF_NOHZ to init/0 and let it propagate to all tasks through fork
	 * This assumes that init is the only task at this early boot stage.
	 */
	set_tsk_thread_flag(&init_task, TIF_NOHZ);
#endif
	WARN_ON_ONCE(!tasklist_empty());

	initialized = true;
}

#ifdef CONFIG_CONTEXT_TRACKING_USER_FORCE
void __init context_tracking_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		ct_cpu_track_user(cpu);
}
#endif

#endif /* #ifdef CONFIG_CONTEXT_TRACKING_USER */
