#ifndef _LINUX_PSI_H
#define _LINUX_PSI_H

#include <linux/psi_types.h>
#include <linux/sched.h>

#ifdef CONFIG_PSI

extern bool psi_disabled;

void psi_init(void);

void psi_task_change(struct task_struct *task, int clear, int set);

void psi_memstall_tick(struct task_struct *task, int cpu);
void psi_memstall_enter(unsigned long *flags);
void psi_memstall_leave(unsigned long *flags);

#else /* CONFIG_PSI */

static inline void psi_init(void) {}

static inline void psi_memstall_enter(unsigned long *flags) {}
static inline void psi_memstall_leave(unsigned long *flags) {}

#endif /* CONFIG_PSI */

#endif /* _LINUX_PSI_H */
