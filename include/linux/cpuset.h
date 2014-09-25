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
#include <linux/mm.h>
#include <linux/jump_label.h>

#ifdef CONFIG_CPUSETS

extern struct static_key cpusets_enabled_key;
static inline bool cpusets_enabled(void)
{
	return static_key_false(&cpusets_enabled_key);
}

static inline int nr_cpusets(void)
{
	/* jump label reference count + the top-level cpuset */
	return static_key_count(&cpusets_enabled_key) + 1;
}

static inline void cpuset_inc(void)
{
	static_key_slow_inc(&cpusets_enabled_key);
}

static inline void cpuset_dec(void)
{
	static_key_slow_dec(&cpusets_enabled_key);
}

extern int cpuset_init(void);
extern void cpuset_init_smp(void);
extern void cpuset_update_active_cpus(bool cpu_online);
extern void cpuset_cpus_allowed(struct task_struct *p, struct cpumask *mask);
extern void cpuset_cpus_allowed_fallback(struct task_struct *p);
extern nodemask_t cpuset_mems_allowed(struct task_struct *p);
#define cpuset_current_mems_allowed (current->mems_allowed)
void cpuset_init_current_mems_allowed(void);
int cpuset_nodemask_valid_mems_allowed(nodemask_t *nodemask);

extern int __cpuset_node_allowed_softwall(int node, gfp_t gfp_mask);
extern int __cpuset_node_allowed_hardwall(int node, gfp_t gfp_mask);

static inline int cpuset_node_allowed_softwall(int node, gfp_t gfp_mask)
{
	return nr_cpusets() <= 1 ||
		__cpuset_node_allowed_softwall(node, gfp_mask);
}

static inline int cpuset_node_allowed_hardwall(int node, gfp_t gfp_mask)
{
	return nr_cpusets() <= 1 ||
		__cpuset_node_allowed_hardwall(node, gfp_mask);
}

static inline int cpuset_zone_allowed_softwall(struct zone *z, gfp_t gfp_mask)
{
	return cpuset_node_allowed_softwall(zone_to_nid(z), gfp_mask);
}

static inline int cpuset_zone_allowed_hardwall(struct zone *z, gfp_t gfp_mask)
{
	return cpuset_node_allowed_hardwall(zone_to_nid(z), gfp_mask);
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
extern int proc_cpuset_show(struct seq_file *, void *);

extern int cpuset_mem_spread_node(void);
extern int cpuset_slab_spread_node(void);

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

/*
 * read_mems_allowed_begin is required when making decisions involving
 * mems_allowed such as during page allocation. mems_allowed can be updated in
 * parallel and depending on the new value an operation can fail potentially
 * causing process failure. A retry loop with read_mems_allowed_begin and
 * read_mems_allowed_retry prevents these artificial failures.
 */
static inline unsigned int read_mems_allowed_begin(void)
{
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

static inline void cpuset_update_active_cpus(bool cpu_online)
{
	partition_sched_domains(1, NULL, NULL);
}

static inline void cpuset_cpus_allowed(struct task_struct *p,
				       struct cpumask *mask)
{
	cpumask_copy(mask, cpu_possible_mask);
}

static inline void cpuset_cpus_allowed_fallback(struct task_struct *p)
{
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

static inline int cpuset_node_allowed_softwall(int node, gfp_t gfp_mask)
{
	return 1;
}

static inline int cpuset_node_allowed_hardwall(int node, gfp_t gfp_mask)
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
