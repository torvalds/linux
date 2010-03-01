#ifndef __INCLUDE_LINUX_OOM_H
#define __INCLUDE_LINUX_OOM_H

/* /proc/<pid>/oom_adj set to -17 protects from the oom-killer */
#define OOM_DISABLE (-17)
/* inclusive */
#define OOM_ADJUST_MIN (-16)
#define OOM_ADJUST_MAX 15

#ifdef __KERNEL__

#include <linux/types.h>
#include <linux/nodemask.h>

struct zonelist;
struct notifier_block;

/*
 * Types of limitations to the nodes from which allocations may occur
 */
enum oom_constraint {
	CONSTRAINT_NONE,
	CONSTRAINT_CPUSET,
	CONSTRAINT_MEMORY_POLICY,
};

extern int try_set_zone_oom(struct zonelist *zonelist, gfp_t gfp_flags);
extern void clear_zonelist_oom(struct zonelist *zonelist, gfp_t gfp_flags);

extern void out_of_memory(struct zonelist *zonelist, gfp_t gfp_mask,
		int order, nodemask_t *mask);
extern int register_oom_notifier(struct notifier_block *nb);
extern int unregister_oom_notifier(struct notifier_block *nb);

extern bool oom_killer_disabled;

static inline void oom_killer_disable(void)
{
	oom_killer_disabled = true;
}

static inline void oom_killer_enable(void)
{
	oom_killer_disabled = false;
}
#endif /* __KERNEL__*/
#endif /* _INCLUDE_LINUX_OOM_H */
