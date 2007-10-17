#ifndef __INCLUDE_LINUX_OOM_H
#define __INCLUDE_LINUX_OOM_H

#include <linux/sched.h>

/* /proc/<pid>/oom_adj set to -17 protects from the oom-killer */
#define OOM_DISABLE (-17)
/* inclusive */
#define OOM_ADJUST_MIN (-16)
#define OOM_ADJUST_MAX 15

#ifdef __KERNEL__

/*
 * Types of limitations to the nodes from which allocations may occur
 */
enum oom_constraint {
	CONSTRAINT_NONE,
	CONSTRAINT_CPUSET,
	CONSTRAINT_MEMORY_POLICY,
};

extern void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask, int order);
extern int register_oom_notifier(struct notifier_block *nb);
extern int unregister_oom_notifier(struct notifier_block *nb);

#endif /* __KERNEL__*/
#endif /* _INCLUDE_LINUX_OOM_H */
