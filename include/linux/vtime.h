#ifndef _LINUX_KERNEL_VTIME_H
#define _LINUX_KERNEL_VTIME_H

struct task_struct;

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
extern void vtime_task_switch(struct task_struct *prev);
extern void __vtime_account_system(struct task_struct *tsk);
extern void vtime_account_system(struct task_struct *tsk);
extern void __vtime_account_idle(struct task_struct *tsk);
#else
static inline void vtime_task_switch(struct task_struct *prev) { }
static inline void vtime_account_system(struct task_struct *tsk) { }
#endif

#if !defined(CONFIG_VIRT_CPU_ACCOUNTING) && !defined(CONFIG_IRQ_TIME_ACCOUNTING)
static inline void vtime_account(struct task_struct *tsk)
{
}
#else
extern void vtime_account(struct task_struct *tsk);
#endif

#endif /* _LINUX_KERNEL_VTIME_H */
