#ifndef _LINUX_KERNEL_VTIME_H
#define _LINUX_KERNEL_VTIME_H

#include <linux/context_tracking_state.h>
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
#include <asm/vtime.h>
#endif


struct task_struct;

/*
 * vtime_accounting_cpu_enabled() definitions/declarations
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
static inline bool vtime_accounting_cpu_enabled(void) { return true; }
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_NATIVE */

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
/*
 * Checks if vtime is enabled on some CPU. Cputime readers want to be careful
 * in that case and compute the tickless cputime.
 * For now vtime state is tied to context tracking. We might want to decouple
 * those later if necessary.
 */
static inline bool vtime_accounting_enabled(void)
{
	return context_tracking_is_enabled();
}

static inline bool vtime_accounting_cpu_enabled(void)
{
	if (vtime_accounting_enabled()) {
		if (context_tracking_cpu_is_enabled())
			return true;
	}

	return false;
}
#endif /* CONFIG_VIRT_CPU_ACCOUNTING_GEN */

#ifndef CONFIG_VIRT_CPU_ACCOUNTING
static inline bool vtime_accounting_cpu_enabled(void) { return false; }
#endif /* !CONFIG_VIRT_CPU_ACCOUNTING */


/*
 * Common vtime APIs
 */
#ifdef CONFIG_VIRT_CPU_ACCOUNTING

#ifdef __ARCH_HAS_VTIME_TASK_SWITCH
extern void vtime_task_switch(struct task_struct *prev);
#else
extern void vtime_common_task_switch(struct task_struct *prev);
static inline void vtime_task_switch(struct task_struct *prev)
{
	if (vtime_accounting_cpu_enabled())
		vtime_common_task_switch(prev);
}
#endif /* __ARCH_HAS_VTIME_TASK_SWITCH */

extern void vtime_account_system(struct task_struct *tsk);
extern void vtime_account_idle(struct task_struct *tsk);
extern void vtime_account_user(struct task_struct *tsk);

#ifdef __ARCH_HAS_VTIME_ACCOUNT
extern void vtime_account_irq_enter(struct task_struct *tsk);
#else
extern void vtime_common_account_irq_enter(struct task_struct *tsk);
static inline void vtime_account_irq_enter(struct task_struct *tsk)
{
	if (vtime_accounting_cpu_enabled())
		vtime_common_account_irq_enter(tsk);
}
#endif /* __ARCH_HAS_VTIME_ACCOUNT */

#else /* !CONFIG_VIRT_CPU_ACCOUNTING */

static inline void vtime_task_switch(struct task_struct *prev) { }
static inline void vtime_account_system(struct task_struct *tsk) { }
static inline void vtime_account_user(struct task_struct *tsk) { }
static inline void vtime_account_irq_enter(struct task_struct *tsk) { }
#endif /* !CONFIG_VIRT_CPU_ACCOUNTING */

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
extern void arch_vtime_task_switch(struct task_struct *tsk);
extern void vtime_gen_account_irq_exit(struct task_struct *tsk);

static inline void vtime_account_irq_exit(struct task_struct *tsk)
{
	if (vtime_accounting_cpu_enabled())
		vtime_gen_account_irq_exit(tsk);
}

extern void vtime_user_enter(struct task_struct *tsk);

static inline void vtime_user_exit(struct task_struct *tsk)
{
	vtime_account_user(tsk);
}
extern void vtime_guest_enter(struct task_struct *tsk);
extern void vtime_guest_exit(struct task_struct *tsk);
extern void vtime_init_idle(struct task_struct *tsk, int cpu);
#else /* !CONFIG_VIRT_CPU_ACCOUNTING_GEN  */
static inline void vtime_account_irq_exit(struct task_struct *tsk)
{
	/* On hard|softirq exit we always account to hard|softirq cputime */
	vtime_account_system(tsk);
}
static inline void vtime_user_enter(struct task_struct *tsk) { }
static inline void vtime_user_exit(struct task_struct *tsk) { }
static inline void vtime_guest_enter(struct task_struct *tsk) { }
static inline void vtime_guest_exit(struct task_struct *tsk) { }
static inline void vtime_init_idle(struct task_struct *tsk, int cpu) { }
#endif

#ifdef CONFIG_IRQ_TIME_ACCOUNTING
extern void irqtime_account_irq(struct task_struct *tsk);
#else
static inline void irqtime_account_irq(struct task_struct *tsk) { }
#endif

static inline void account_irq_enter_time(struct task_struct *tsk)
{
	vtime_account_irq_enter(tsk);
	irqtime_account_irq(tsk);
}

static inline void account_irq_exit_time(struct task_struct *tsk)
{
	vtime_account_irq_exit(tsk);
	irqtime_account_irq(tsk);
}

#endif /* _LINUX_KERNEL_VTIME_H */
