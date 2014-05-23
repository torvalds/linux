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

#define CREATE_TRACE_POINTS
#include <trace/events/context_tracking.h>

struct static_key context_tracking_enabled = STATIC_KEY_INIT_FALSE;
EXPORT_SYMBOL_GPL(context_tracking_enabled);

DEFINE_PER_CPU(struct context_tracking, context_tracking);
EXPORT_SYMBOL_GPL(context_tracking);

void context_tracking_cpu_set(int cpu)
{
	if (!per_cpu(context_tracking.active, cpu)) {
		per_cpu(context_tracking.active, cpu) = true;
		static_key_slow_inc(&context_tracking_enabled);
	}
}

/**
 * context_tracking_user_enter - Inform the context tracking that the CPU is going to
 *                               enter userspace mode.
 *
 * This function must be called right before we switch from the kernel
 * to userspace, when it's guaranteed the remaining kernel instructions
 * to execute won't use any RCU read side critical section because this
 * function sets RCU in extended quiescent state.
 */
void context_tracking_user_enter(void)
{
	unsigned long flags;

	/*
	 * Repeat the user_enter() check here because some archs may be calling
	 * this from asm and if no CPU needs context tracking, they shouldn't
	 * go further. Repeat the check here until they support the inline static
	 * key check.
	 */
	if (!context_tracking_is_enabled())
		return;

	/*
	 * Some contexts may involve an exception occuring in an irq,
	 * leading to that nesting:
	 * rcu_irq_enter() rcu_user_exit() rcu_user_exit() rcu_irq_exit()
	 * This would mess up the dyntick_nesting count though. And rcu_irq_*()
	 * helpers are enough to protect RCU uses inside the exception. So
	 * just return immediately if we detect we are in an IRQ.
	 */
	if (in_interrupt())
		return;

	/* Kernel threads aren't supposed to go to userspace */
	WARN_ON_ONCE(!current->mm);

	local_irq_save(flags);
	if ( __this_cpu_read(context_tracking.state) != IN_USER) {
		if (__this_cpu_read(context_tracking.active)) {
			trace_user_enter(0);
			/*
			 * At this stage, only low level arch entry code remains and
			 * then we'll run in userspace. We can assume there won't be
			 * any RCU read-side critical section until the next call to
			 * user_exit() or rcu_irq_enter(). Let's remove RCU's dependency
			 * on the tick.
			 */
			vtime_user_enter(current);
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
		__this_cpu_write(context_tracking.state, IN_USER);
	}
	local_irq_restore(flags);
}

#ifdef CONFIG_PREEMPT
/**
 * preempt_schedule_context - preempt_schedule called by tracing
 *
 * The tracing infrastructure uses preempt_enable_notrace to prevent
 * recursion and tracing preempt enabling caused by the tracing
 * infrastructure itself. But as tracing can happen in areas coming
 * from userspace or just about to enter userspace, a preempt enable
 * can occur before user_exit() is called. This will cause the scheduler
 * to be called when the system is still in usermode.
 *
 * To prevent this, the preempt_enable_notrace will use this function
 * instead of preempt_schedule() to exit user context if needed before
 * calling the scheduler.
 */
asmlinkage __visible void __sched notrace preempt_schedule_context(void)
{
	enum ctx_state prev_ctx;

	if (likely(!preemptible()))
		return;

	/*
	 * Need to disable preemption in case user_exit() is traced
	 * and the tracer calls preempt_enable_notrace() causing
	 * an infinite recursion.
	 */
	preempt_disable_notrace();
	prev_ctx = exception_enter();
	preempt_enable_no_resched_notrace();

	preempt_schedule();

	preempt_disable_notrace();
	exception_exit(prev_ctx);
	preempt_enable_notrace();
}
EXPORT_SYMBOL_GPL(preempt_schedule_context);
#endif /* CONFIG_PREEMPT */

/**
 * context_tracking_user_exit - Inform the context tracking that the CPU is
 *                              exiting userspace mode and entering the kernel.
 *
 * This function must be called after we entered the kernel from userspace
 * before any use of RCU read side critical section. This potentially include
 * any high level kernel code like syscalls, exceptions, signal handling, etc...
 *
 * This call supports re-entrancy. This way it can be called from any exception
 * handler without needing to know if we came from userspace or not.
 */
void context_tracking_user_exit(void)
{
	unsigned long flags;

	if (!context_tracking_is_enabled())
		return;

	if (in_interrupt())
		return;

	local_irq_save(flags);
	if (__this_cpu_read(context_tracking.state) == IN_USER) {
		if (__this_cpu_read(context_tracking.active)) {
			/*
			 * We are going to run code that may use RCU. Inform
			 * RCU core about that (ie: we may need the tick again).
			 */
			rcu_user_exit();
			vtime_user_exit(current);
			trace_user_exit(0);
		}
		__this_cpu_write(context_tracking.state, IN_KERNEL);
	}
	local_irq_restore(flags);
}

/**
 * __context_tracking_task_switch - context switch the syscall callbacks
 * @prev: the task that is being switched out
 * @next: the task that is being switched in
 *
 * The context tracking uses the syscall slow path to implement its user-kernel
 * boundaries probes on syscalls. This way it doesn't impact the syscall fast
 * path on CPUs that don't do context tracking.
 *
 * But we need to clear the flag on the previous task because it may later
 * migrate to some CPU that doesn't do the context tracking. As such the TIF
 * flag may not be desired there.
 */
void __context_tracking_task_switch(struct task_struct *prev,
				    struct task_struct *next)
{
	clear_tsk_thread_flag(prev, TIF_NOHZ);
	set_tsk_thread_flag(next, TIF_NOHZ);
}

#ifdef CONFIG_CONTEXT_TRACKING_FORCE
void __init context_tracking_init(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		context_tracking_cpu_set(cpu);
}
#endif
