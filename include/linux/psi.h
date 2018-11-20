#ifndef _LINUX_PSI_H
#define _LINUX_PSI_H

#include <linux/psi_types.h>
#include <linux/sched.h>

struct seq_file;
struct css_set;

#ifdef CONFIG_PSI

extern bool psi_disabled;

void psi_init(void);

void psi_task_change(struct task_struct *task, int clear, int set);

void psi_memstall_tick(struct task_struct *task, int cpu);
void psi_memstall_enter(unsigned long *flags);
void psi_memstall_leave(unsigned long *flags);

int psi_show(struct seq_file *s, struct psi_group *group, enum psi_res res);

#ifdef CONFIG_CGROUPS
int psi_cgroup_alloc(struct cgroup *cgrp);
void psi_cgroup_free(struct cgroup *cgrp);
void cgroup_move_task(struct task_struct *p, struct css_set *to);
#endif

#else /* CONFIG_PSI */

static inline void psi_init(void) {}

static inline void psi_memstall_enter(unsigned long *flags) {}
static inline void psi_memstall_leave(unsigned long *flags) {}

#ifdef CONFIG_CGROUPS
static inline int psi_cgroup_alloc(struct cgroup *cgrp)
{
	return 0;
}
static inline void psi_cgroup_free(struct cgroup *cgrp)
{
}
static inline void cgroup_move_task(struct task_struct *p, struct css_set *to)
{
	rcu_assign_pointer(p->cgroups, to);
}
#endif

#endif /* CONFIG_PSI */

#endif /* _LINUX_PSI_H */
