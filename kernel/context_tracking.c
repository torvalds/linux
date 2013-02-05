#include <linux/context_tracking.h>
#include <linux/kvm_host.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/hardirq.h>
#include <linux/export.h>

DEFINE_PER_CPU(struct context_tracking, context_tracking) = {
#ifdef CONFIG_CONTEXT_TRACKING_FORCE
	.active = true,
#endif
};

void user_enter(void)
{
	unsigned long flags;

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

	WARN_ON_ONCE(!current->mm);

	local_irq_save(flags);
	if (__this_cpu_read(context_tracking.active) &&
	    __this_cpu_read(context_tracking.state) != IN_USER) {
		vtime_user_enter(current);
		rcu_user_enter();
		__this_cpu_write(context_tracking.state, IN_USER);
	}
	local_irq_restore(flags);
}

void user_exit(void)
{
	unsigned long flags;

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

	local_irq_save(flags);
	if (__this_cpu_read(context_tracking.state) == IN_USER) {
		rcu_user_exit();
		vtime_user_exit(current);
		__this_cpu_write(context_tracking.state, IN_KERNEL);
	}
	local_irq_restore(flags);
}

void guest_enter(void)
{
	if (vtime_accounting_enabled())
		vtime_guest_enter(current);
	else
		__guest_enter();
}
EXPORT_SYMBOL_GPL(guest_enter);

void guest_exit(void)
{
	if (vtime_accounting_enabled())
		vtime_guest_exit(current);
	else
		__guest_exit();
}
EXPORT_SYMBOL_GPL(guest_exit);

void context_tracking_task_switch(struct task_struct *prev,
			     struct task_struct *next)
{
	if (__this_cpu_read(context_tracking.active)) {
		clear_tsk_thread_flag(prev, TIF_NOHZ);
		set_tsk_thread_flag(next, TIF_NOHZ);
	}
}
