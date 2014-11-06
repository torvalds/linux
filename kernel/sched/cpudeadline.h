#ifndef _LINUX_CPUDL_H
#define _LINUX_CPUDL_H

#include <linux/sched.h>

#define IDX_INVALID     -1

struct cpudl_item {
	u64 dl;
	int cpu;
	int idx;
};

struct cpudl {
	raw_spinlock_t lock;
	int size;
	cpumask_var_t free_cpus;
	struct cpudl_item *elements;
};


#ifdef CONFIG_SMP
int cpudl_find(struct cpudl *cp, struct task_struct *p,
	       struct cpumask *later_mask);
void cpudl_set(struct cpudl *cp, int cpu, u64 dl, int is_valid);
int cpudl_init(struct cpudl *cp);
void cpudl_cleanup(struct cpudl *cp);
#endif /* CONFIG_SMP */

#endif /* _LINUX_CPUDL_H */
