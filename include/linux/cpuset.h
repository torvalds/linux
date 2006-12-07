#ifndef _LINUX_CPUSET_H
#define _LINUX_CPUSET_H
/*
 *  cpuset interface
 *
 *  Copyright (C) 2003 BULL SA
 *  Copyright (C) 2004-2006 Silicon Graphics, Inc.
 *
 */

#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>

#ifdef CONFIG_CPUSETS

extern int number_of_cpusets;	/* How many cpusets are defined in system? */

extern int cpuset_init_early(void);
extern int cpuset_init(void);
extern void cpuset_init_smp(void);
extern void cpuset_fork(struct task_struct *p);
extern void cpuset_exit(struct task_struct *p);
extern cpumask_t cpuset_cpus_allowed(struct task_struct *p);
extern nodemask_t cpuset_mems_allowed(struct task_struct *p);
#define cpuset_current_mems_allowed (current->mems_allowed)
void cpuset_init_current_mems_allowed(void);
void cpuset_update_task_memory_state(void);
#define cpuset_nodes_subset_current_mems_allowed(nodes) \
		nodes_subset((nodes), current->mems_allowed)
int cpuset_zonelist_valid_mems_allowed(struct zonelist *zl);

extern int __cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask);
static int inline cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return number_of_cpusets <= 1 || __cpuset_zone_allowed(z, gfp_mask);
}

extern int cpuset_excl_nodes_overlap(const struct task_struct *p);

#define cpuset_memory_pressure_bump() 				\
	do {							\
		if (cpuset_memory_pressure_enabled)		\
			__cpuset_memory_pressure_bump();	\
	} while (0)
extern int cpuset_memory_pressure_enabled;
extern void __cpuset_memory_pressure_bump(void);

extern const struct file_operations proc_cpuset_operations;
extern char *cpuset_task_status_allowed(struct task_struct *task, char *buffer);

extern void cpuset_lock(void);
extern void cpuset_unlock(void);

extern int cpuset_mem_spread_node(void);

static inline int cpuset_do_page_mem_spread(void)
{
	return current->flags & PF_SPREAD_PAGE;
}

static inline int cpuset_do_slab_mem_spread(void)
{
	return current->flags & PF_SPREAD_SLAB;
}

extern void cpuset_track_online_nodes(void);

#else /* !CONFIG_CPUSETS */

static inline int cpuset_init_early(void) { return 0; }
static inline int cpuset_init(void) { return 0; }
static inline void cpuset_init_smp(void) {}
static inline void cpuset_fork(struct task_struct *p) {}
static inline void cpuset_exit(struct task_struct *p) {}

static inline cpumask_t cpuset_cpus_allowed(struct task_struct *p)
{
	return cpu_possible_map;
}

static inline nodemask_t cpuset_mems_allowed(struct task_struct *p)
{
	return node_possible_map;
}

#define cpuset_current_mems_allowed (node_online_map)
static inline void cpuset_init_current_mems_allowed(void) {}
static inline void cpuset_update_task_memory_state(void) {}
#define cpuset_nodes_subset_current_mems_allowed(nodes) (1)

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

static inline void cpuset_memory_pressure_bump(void) {}

static inline char *cpuset_task_status_allowed(struct task_struct *task,
							char *buffer)
{
	return buffer;
}

static inline void cpuset_lock(void) {}
static inline void cpuset_unlock(void) {}

static inline int cpuset_mem_spread_node(void)
{
	return 0;
}

static inline int cpuset_do_page_mem_spread(void)
{
	return 0;
}

static inline int cpuset_do_slab_mem_spread(void)
{
	return 0;
}

static inline void cpuset_track_online_nodes(void) {}

#endif /* !CONFIG_CPUSETS */

#endif /* _LINUX_CPUSET_H */
