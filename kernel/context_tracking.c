// SPDX-License-Identifier: GPL-2.0-only
/*
 * Context tracking: Probe on high level context boundaries such as kernel
 * and userspace. This includes syscalls and exceptions entry/exit.
 *
 * This is used by RCU to remove its dependency on the timer tick while a CPU
 * runs in userspace.
 *
 *  Started by Frederic Weisbecker:
 *
 * Copyright (C) 2012 Red Hat, Inc., Frederic Weisbecker <fweisbec@redhat.com>
 *
 * Many thanks to Gilad Ben-Yossef, Paul McKenney, Ingo Molnar, Andrew Morton,
 * Steven Rostedt, Peter Zijlstra for suggestions and improvements.
 *
 */

#include <linux/context_tracking.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/export.h>
#include <linux/kprobes.h>


DEFINE_PER_CPU(struct context_tracking, context_tracking) = {
#ifdef CONFIG_CONTEXT_TRACKING_IDLE
	.dynticks = ATOMIC_INIT(1),
#endif
};
EXPORT_SYMBOL_GPL(context_tracking);

#ifdef CONFIG_CONTEXT_TRACKING_IDLE
noinstr void ct_idle_enter(void)
{
	rcu_idle_enter();
}
EXPORT_SYMBOL_GPL(ct_idle_enter);

void ct_idle_exit(void)
{
	rcu_idle_exit();
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

noinstr void ct_nmi_enter(void)
{
	rcu_nmi_enter();
}

noinstr void ct_nmi_exit(void)
{
	rcu_nmi_exit();
}
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
	/* Kernel threads aren't supposed to go to userspace */
	WARN_ON_ONCE(!current->mm);

	if (!context_tracking_recursion_enter())
		return;

	if ( __this_cpu_read(context_tracking.state) != state) {
		if (__this_cpu_read(context_tracking.active)) {
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
			rcu_user_enter();
		}
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
		__this_cpu_write(context_tracking.state, state);
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
	 * ct_irq_enter() rcu_user_exit() rcu_user_exit() ct_irq_exit()
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
	if (!context_tracking_recursion_enter())
		return;

	if (__this_cpu_read(context_tracking.state) == state) {
		if (__this_cpu_read(context_tracking.active)) {
			/*
			 * We are going to run code that may use RCU. Inform
			 * RCU core about that (ie: we may need the tick again).
			 */
			rcu_user_exit();
			if (state == CONTEXT_USER) {
				instrumentation_begin();
				vtime_user_exit(current);
				trace_user_exit(0);
				instrumentation_end();
			}
		}
		__this_cpu_write(context_tracking.state, CONTEXT_KERNEL);
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
