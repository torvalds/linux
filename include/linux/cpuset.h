/* SPDX-License-Identifier: GPL-2.0 */
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
#include <linux/sched/topology.h>
#include <linux/sched/task.h>
#include <linux/cpumask.h>
#include <linux/nodemask.h>
#include <linux/mm.h>
#include <linux/mmu_context.h>
#include <linux/jump_label.h>

#ifdef CONFIG_CPUSETS

/*
 * Static branch rewrites can happen in an arbitrary order for a given
 * key. In code paths where we need to loop with read_mems_allowed_begin() and
 * read_mems_allowed_retry() to get a consistent view of mems_allowed, we need
 * to ensure that begin() always gets rewritten before retry() in the
 * disabled -> enabled transition. If not, then if local irqs are disabled
 * around the loop, we can deadlock since retry() would always be
 * comparing the latest value of the mems_allowed seqcount against 0 as
 * begin() still would see cpusets_enabled() as false. The enabled -> disabled
 * transition should happen in reverse order for the same reasons (want to stop
 * looking at real value of mems_allowed.sequence in retry() first).
 */
extern struct static_key_false cpusets_pre_enable_key;
extern struct static_key_false cpusets_enabled_key;
static inline bool cpusets_enabled(void)
{
	return static_branch_unlikely(&cpusets_enabled_key);
}

static inline void cpuset_inc(void)
{
	static_branch_inc_cpuslocked(&cpusets_pre_enable_key);
	static_branch_inc_cpuslocked(&cpusets_enabled_key);
}

static inline void cpuset_dec(void)
{
	static_branch_dec_cpuslocked(&cpusets_enabled_key);
	static_branch_dec_cpuslocked(&cpusets_pre_enable_key);
}

extern int cpuset_init(void);
extern void cpuset_init_smp(void);
extern void cpuset_force_rebuild(void);
extern void cpuset_update_active_cpus(void);
extern void cpuset_wait_for_hotplug(void);
extern void cpuset_cpus_allowed(struct task_struct *p, struct cpumask *mask);
extern bool cpuset_cpus_allowed_fallback(struct task_struct *p);
extern nodemask_t cpuset_mems_allowed(struct task_struct *p);
#define cpuset_current_mems_allowed (current->mems_allowed)
void cpuset_init_current_mems_allowed(void);
int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask);

extern bool __cpuset_node_allowed(int node, gfp_t gfp_mask);

static inline bool cpuset_node_allowed(int node, gfp_t gfp_mask)
{
	if (cpusets_enabled())
		return __cpuset_node_allowed(node, gfp_mask);
	return true;
}

static inline bool __cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return __cpuset_node_allowed(zone_to_nid(z), gfp_mask);
}

static inline bool cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	if (cpusets_enabled())
		return __cpuset_zone_allowed(z, gfp_mask);
	return true;
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

extern void cpuset_task_status_allowed(struct seq_file *m,
					struct task_struct *task);
extern int proc_cpuset_show(struct seq_file *m, struct pid_namespace *ns,
			    struct pid *pid, struct task_struct *tsk);

extern int cpuset_mem_spread_node(void);
extern int cpuset_slab_spread_node(void);

static inline int cpuset_do_page_mem_spread(void)
{
	return task_spread_page(current);
}

static inline int cpuset_do_slab_mem_spread(void)
{
	return task_spread_slab(current);
}

extern bool current_cpuset_is_being_rebound(void);

extern void rebuild_sched_domains(void);

extern void cpuset_print_current_mems_allowed(void);

/*
 * read_mems_allowed_begin is required when making decisions involving
 * mems_allowed such as during page allocation. mems_allowed can be updated in
 * parallel and depending on the new value an operation can fail potentially
 * causing process failure. A retry loop with read_mems_allowed_begin and
 * read_mems_allowed_retry prevents these artificial failures.
 */
static inline unsigned int read_mems_allowed_begin(void)
{
	if (!static_branch_unlikely(&cpusets_pre_enable_key))
		return 0;

	return read_seqcount_begin(&current->mems_allowed_seq);
}

/*
 * If this returns true, the operation that took place after
 * read_mems_allowed_begin may have failed artificially due to a concurrent
 * update of mems_allowed. It is up to the caller to retry the operation if
 * appropriate.
 */
static inline bool read_mems_allowed_retry(unsigned int seq)
{
	if (!static_branch_unlikely(&cpusets_enabled_key))
		return false;

	return read_seqcount_retry(&current->mems_allowed_seq, seq);
}

static inline void set_mems_allowed(nodemask_t nodemask)
{
	unsigned long flags;

	task_lock(current);
	local_irq_save(flags);
	write_seqcount_begin(&current->mems_allowed_seq);
	current->mems_allowed = nodemask;
	write_seqcount_end(&current->mems_allowed_seq);
	local_irq_restore(flags);
	task_unlock(current);
}

#else /* !CONFIG_CPUSETS */

static inline bool cpusets_enabled(void) { return false; }

static inline int cpuset_init(void) { return 0; }
static inline void cpuset_init_smp(void) {}

static inline void cpuset_force_rebuild(void) { }

static inline void cpuset_update_active_cpus(void)
{
	partition_sched_domains(1, NULL, NULL);
}

static inline void cpuset_wait_for_hotplug(void) { }

static inline void cpuset_cpus_allowed(struct task_struct *p,
				       struct cpumask *mask)
{
	cpumask_copy(mask, task_cpu_possible_mask(p));
}

static inline bool cpuset_cpus_allowed_fallback(struct task_struct *p)
{
	return false;
}

static inline nodemask_t cpuset_mems_allowed(struct task_struct *p)
{
	return node_possible_map;
}

#define cpuset_current_mems_allowed (node_states[N_MEMORY])
static inline void cpuset_init_current_mems_allowed(void) {}

static inline int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask)
{
	return 1;
}

static inline bool cpuset_node_allowed(int node, gfp_t gfp_mask)
{
	return true;
}

static inline bool __cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return true;
}

static inline bool cpuset_zone_allowed(struct zone *z, gfp_t gfp_mask)
{
	return true;
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

static inline int cpuset_mem_spread_node(void)
{
	return 0;
}

static inline int cpuset_slab_spread_node(void)
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

static inline bool current_cpuset_is_being_rebound(void)
{
	return false;
}

static inline void rebuild_sched_domains(void)
{
	partition_sched_domains(1, NULL, NULL);
}

static inline void cpuset_print_current_mems_allowed(void)
{
}

static inline void set_mems_allowed(nodemask_t nodemask)
{
}

static inline unsigned int read_mems_allowed_begin(void)
{
	return 0;
}

static inline bool read_mems_allowed_retry(unsigned int seq)
{
	return false;
}

#endif /* !CONFIG_CPUSETS */

#endif /* _LINUX_CPUSET_H */
