#ifndef _LINUX_CPUSET_H
#define _LINUX_CPUSET_H
/*
 *  cpuset interface
 *
 *  Copyright (C) 2003 BULL SA
 *  Copyright (C) 2004 Silicon Graphics, Inc.
 *
 */

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>

#ifdef CONFIG_CPUSETS

extern int cpuset_init(void);
extern void cpuset_init_smp(void);
extern void cpuset_fork(struct task_struct *p);
extern void cpuset_exit(struct task_struct *p);
extern cpumask_t cpuset_cpus_allowed(const struct task_struct *p);
void cpuset_init_current_mems_allowed(void);
void cpuset_update_current_mems_allowed(void);
void cpuset_restrict_to_mems_allowed(unsigned long *nodes);
int cpuset_zonelist_valid_mems_allowed(struct zonelist *zl);
extern int cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask);
extern int cpuset_excl_nodes_overlap(const struct task_struct *p);
extern struct file_operations proc_cpuset_operations;
extern char *cpuset_task_status_allowed(struct task_struct *task, char *buffer);

#else /* !CONFIG_CPUSETS */

static inline int cpuset_init(void) { return 0; }
static inline void cpuset_init_smp(void) {}
static inline void cpuset_fork(struct task_struct *p) {}
static inline void cpuset_exit(struct task_struct *p) {}

static inline cpumask_t cpuset_cpus_allowed(struct task_struct *p)
{
	return cpu_possible_map;
}

static inline void cpuset_init_current_mems_allowed(void) {}
static inline void cpuset_update_current_mems_allowed(void) {}
static inline void cpuset_restrict_to_mems_allowed(unsigned long *nodes) {}

static inline int cpuset_zonelist_valid_mems_allowed(struct zonelist *zl)
{
	return 1;
}

static inline int cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return 1;
}

static inline int cpuset_excl_nodes_overlap(const struct task_struct *p)
{
	return 1;
}

static inline char *cpuset_task_status_allowed(struct task_struct *task,
							char *buffer)
{
	return buffer;
}

#endif /* !CONFIG_CPUSETS */

#endif /* _LINUX_CPUSET_H */
