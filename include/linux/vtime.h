#ifndef _LINUX_KERNEL_VTIME_H
#define _LINUX_KERNEL_VTIME_H

struct task_struct;

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
extern void vtime_task_switch(struct task_struct *prev);
extern void vtime_account_system(struct task_struct *tsk);
extern void vtime_account_idle(struct task_struct *tsk);
extern void vtime_account_user(struct task_struct *tsk);
extern void vtime_account_irq_enter(struct task_struct *tsk);

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_NATIVE
static inline bool vtime_accounting_enabled(void) { return true; }
#endif

#else /* !CONFIG_VIRT_CPU_ACCOUNTING */

static inline void vtime_task_switch(struct task_struct *prev) { }
static inline void vtime_account_system(struct task_struct *tsk) { }
static inline void vtime_account_user(struct task_struct *tsk) { }
static inline void vtime_account_irq_enter(struct task_struct *tsk) { }
static inline bool vtime_accounting_enabled(void) { return false; }
#endif

#ifdef CONFIG_VIRT_CPU_ACCOUNTING_GEN
extern void arch_vtime_task_switch(struct task_struct *tsk);
extern void vtime_account_irq_exit(struct task_struct *tsk);
extern bool vtime_accounting_enabled(void);
extern void vtime_user_enter(struct task_struct *tsk);
static inline void vtime_user_exit(struct task_struct *tsk)
{
	vtime_account_user(tsk);
}
extern void vtime_guest_enter(struct task_struct *tsk);
extern void vtime_guest_exit(struct task_struct *tsk);
extern void vtime_init_idle(struct task_struct *tsk);
#else
static inline void vtime_account_irq_exit(struct task_struct *tsk)
{
	/* On hard|softirq exit we always account to hard|softirq cputime */
	vtime_account_system(tsk);
}
static inline void vtime_user_enter(struct task_struct *tsk) { }
static inline void vtime_user_exit(struct task_struct *tsk) { }
static inline void vtime_guest_enter(struct task_struct *tsk) { }
static inline void vtime_guest_exit(struct task_struct *tsk) { }
static inline void vtime_init_idle(struct task_struct *tsk) { }
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
