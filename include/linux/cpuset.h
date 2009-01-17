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
#include <linux/cgroup.h>

#ifdef CONFIG_CPUSETS

extern int number_of_cpusets;	/* How many cpusets are defined in system? */

extern int cpuset_init_early(void);
extern int cpuset_init(void);
extern void cpuset_init_smp(void);
extern void cpuset_cpus_allowed(struct task_struct *p, struct cpumask *mask);
extern void cpuset_cpus_allowed_locked(struct task_struct *p,
				       struct cpumask *mask);
extern nodemask_t cpuset_mems_allowed(struct task_struct *p);
#define cpuset_current_mems_allowed (current->mems_allowed)
void cpuset_init_current_mems_allowed(void);
void cpuset_update_task_memory_state(void);
int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask);

extern int __cpuset_zone_allowed_softwall(struct zone *z, gfp_t gfp_mask);
extern int __cpuset_zone_allowed_hardwall(struct zone *z, gfp_t gfp_mask);

static int inline cpuset_zone_allowed_softwall(struct zone *z, gfp_t gfp_mask)
{
	return number_of_cpusets <= 1 ||
		__cpuset_zone_allowed_softwall(z, gfp_mask);
}

static int inline cpuset_zone_allowed_hardwall(struct zone *z, gfp_t gfp_mask)
{
	return number_of_cpusets <= 1 ||
		__cpuset_zone_allowed_hardwall(z, gfp_mask);
}

extern int cpuset_mems_allowed_intersects(const struct task_struct *tsk1,
					  const struct task_struct *tsk2);

#define cpuset_memory_pressure_bump() 				\
	do {							\
		if (cpuset_memory_pressure_enabled)		\
			__cpuset_memory_pressure_bump();	\
	} while (0)
extern int cpuset_memory_pressure_enabled;
extern void __cpuset_memory_pressure_bump(void);

extern const struct file_operations proc_cpuset_operations;
struct seq_file;
extern void cpuset_task_status_allowed(struct seq_file *m,
					struct task_struct *task);

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

extern int current_cpuset_is_being_rebound(void);

extern void rebuild_sched_domains(void);

extern void cpuset_print_task_mems_allowed(struct task_struct *p);

#else /* !CONFIG_CPUSETS */

static inline int cpuset_init_early(void) { return 0; }
static inline int cpuset_init(void) { return 0; }
static inline void cpuset_init_smp(void) {}

static inline void cpuset_cpus_allowed(struct task_struct *p,
				       struct cpumask *mask)
{
	*mask = cpu_possible_map;
}
static inline void cpuset_cpus_allowed_locked(struct task_struct *p,
					      struct cpumask *mask)
{
	*mask = cpu_possible_map;
}

static inline nodemask_t cpuset_mems_allowed(struct task_struct *p)
{
	return node_possible_map;
}

#define cpuset_current_mems_allowed (node_states[N_HIGH_MEMORY])
static inline void cpuset_init_current_mems_allowed(void) {}
static inline void cpuset_update_task_memory_state(void) {}

static inline int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask)
{
	return 1;
}

static inline int cpuset_zone_allowed_softwall(struct zone *z, gfp_t gfp_mask)
{
	return 1;
}

static inline int cpuset_zone_allowed_hardwall(struct zone *z, gfp_t gfp_mask)
{
	return 1;
}

static inline int cpuset_mems_allowed_intersects(const struct task_struct *tsk1,
						 const struct task_struct *tsk2)
{
	return 1;
}

static inline void cpuset_memory_pressure_bump(void) {}

static inline void cpuset_task_status_allowed(struct seq_file *m,
						struct task_struct *task)
{
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

static inline int current_cpuset_is_being_rebound(void)
{
	return 0;
}

static inline void rebuild_sched_domains(void)
{
	partition_sched_domains(1, NULL, NULL);
}

static inline void cpuset_print_task_mems_allowed(struct task_struct *p)
{
}

#endif /* !CONFIG_CPUSETS */

#endif /* _LINUX_CPUSET_H */
