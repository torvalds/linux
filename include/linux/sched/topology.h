/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_TOPOLOGY_H
#define _LINUX_SCHED_TOPOLOGY_H

#include <linux/topology.h>

#include <linux/sched/idle.h>

/*
 * sched-domains (multiprocessor balancing) declarations:
 */
#ifdef CONFIG_SMP

/* Generate SD flag indexes */
#define SD_FLAG(name, mflags) __##name,
enum {
	#include <linux/sched/sd_flags.h>
	__SD_FLAG_CNT,
};
#undef SD_FLAG
/* Generate SD flag bits */
#define SD_FLAG(name, mflags) name = 1 << __##name,
enum {
	#include <linux/sched/sd_flags.h>
};
#undef SD_FLAG

#ifdef CONFIG_SCHED_DEBUG

struct sd_flag_debug {
	unsigned int meta_flags;
	char *name;
};
extern const struct sd_flag_debug sd_flag_debug[];

#endif

#ifdef CONFIG_SCHED_SMT
static inline int cpu_smt_flags(void)
{
	return SD_SHARE_CPUCAPACITY | SD_SHARE_PKG_RESOURCES;
}
#endif

#ifdef CONFIG_SCHED_MC
static inline int cpu_core_flags(void)
{
	return SD_SHARE_PKG_RESOURCES;
}
#endif

#ifdef CONFIG_NUMA
static inline int cpu_numa_flags(void)
{
	return SD_NUMA;
}
#endif

extern int arch_asym_cpu_priority(int cpu);

struct sched_domain_attr {
	int relax_domain_level;
};

#define SD_ATTR_INIT	(struct sched_domain_attr) {	\
	.relax_domain_level = -1,			\
}

extern int sched_domain_level_max;

struct sched_group;

struct sched_domain_shared {
	atomic_t	ref;
	atomic_t	nr_busy_cpus;
	int		has_idle_cores;
	int		nr_idle_scan;
};

struct sched_domain {
	/* These fields must be setup */
	struct sched_domain __rcu *parent;	/* top domain must be null terminated */
	struct sched_domain __rcu *child;	/* bottom domain must be null terminated */
	struct sched_group *groups;	/* the balancing groups of the domain */
	unsigned long min_interval;	/* Minimum balance interval ms */
	unsigned long max_interval;	/* Maximum balance interval ms */
	unsigned int busy_factor;	/* less balancing by factor if busy */
	unsigned int imbalance_pct;	/* No balance until over watermark */
	unsigned int cache_nice_tries;	/* Leave cache hot tasks for # tries */

	int nohz_idle;			/* NOHZ IDLE status */
	int flags;			/* See SD_* */
	int level;

	/* Runtime fields. */
	unsigned long last_balance;	/* init to jiffies. units in jiffies */
	unsigned int balance_interval;	/* initialise to 1. units in ms. */
	unsigned int nr_balance_failed; /* initialise to 0 */

	/* idle_balance() stats */
	u64 max_newidle_lb_cost;
	unsigned long next_decay_max_lb_cost;

	u64 avg_scan_cost;		/* select_idle_sibling */

#ifdef CONFIG_SCHEDSTATS
	/* load_balance() stats */
	unsigned int lb_count[CPU_MAX_IDLE_TYPES];
	unsigned int lb_failed[CPU_MAX_IDLE_TYPES];
	unsigned int lb_balanced[CPU_MAX_IDLE_TYPES];
	unsigned int lb_imbalance[CPU_MAX_IDLE_TYPES];
	unsigned int lb_gained[CPU_MAX_IDLE_TYPES];
	unsigned int lb_hot_gained[CPU_MAX_IDLE_TYPES];
	unsigned int lb_nobusyg[CPU_MAX_IDLE_TYPES];
	unsigned int lb_nobusyq[CPU_MAX_IDLE_TYPES];

	/* Active load balancing */
	unsigned int alb_count;
	unsigned int alb_failed;
	unsigned int alb_pushed;

	/* SD_BALANCE_EXEC stats */
	unsigned int sbe_count;
	unsigned int sbe_balanced;
	unsigned int sbe_pushed;

	/* SD_BALANCE_FORK stats */
	unsigned int sbf_count;
	unsigned int sbf_balanced;
	unsigned int sbf_pushed;

	/* try_to_wake_up() stats */
	unsigned int ttwu_wake_remote;
	unsigned int ttwu_move_affine;
	unsigned int ttwu_move_balance;
#endif
#ifdef CONFIG_SCHED_DEBUG
	char *name;
#endif
	union {
		void *private;		/* used during construction */
		struct rcu_head rcu;	/* used during destruction */
	};
	struct sched_domain_shared *shared;

	unsigned int span_weight;
	/*
	 * Span of all CPUs in this domain.
	 *
	 * NOTE: this field is variable length. (Allocated dynamically
	 * by attaching extra space to the end of the structure,
	 * depending on how many CPUs the kernel has booted up with)
	 */
	unsigned long span[];
};

static inline struct cpumask *sched_domain_span(struct sched_domain *sd)
{
	return to_cpumask(sd->span);
}

extern void partition_sched_domains_locked(int ndoms_new,
					   cpumask_var_t doms_new[],
					   struct sched_domain_attr *dattr_new);

extern void partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
				    struct sched_domain_attr *dattr_new);

/* Allocate an array of sched domains, for partition_sched_domains(). */
cpumask_var_t *alloc_sched_domains(unsigned int ndoms);
void free_sched_domains(cpumask_var_t doms[], unsigned int ndoms);

bool cpus_share_cache(int this_cpu, int that_cpu);

typedef const struct cpumask *(*sched_domain_mask_f)(int cpu);
typedef int (*sched_domain_flags_f)(void);

#define SDTL_OVERLAP	0x01

struct sd_data {
	struct sched_domain *__percpu *sd;
	struct sched_domain_shared *__percpu *sds;
	struct sched_group *__percpu *sg;
	struct sched_group_capacity *__percpu *sgc;
};

struct sched_domain_topology_level {
	sched_domain_mask_f mask;
	sched_domain_flags_f sd_flags;
	int		    flags;
	int		    numa_level;
	struct sd_data      data;
#ifdef CONFIG_SCHED_DEBUG
	char                *name;
#endif
};

extern void set_sched_topology(struct sched_domain_topology_level *tl);

#ifdef CONFIG_SCHED_DEBUG
# define SD_INIT_NAME(type)		.name = #type
#else
# define SD_INIT_NAME(type)
#endif

#else /* CONFIG_SMP */

struct sched_domain_attr;

static inline void
partition_sched_domains_locked(int ndoms_new, cpumask_var_t doms_new[],
			       struct sched_domain_attr *dattr_new)
{
}

static inline void
partition_sched_domains(int ndoms_new, cpumask_var_t doms_new[],
			struct sched_domain_attr *dattr_new)
{
}

static inline bool cpus_share_cache(int this_cpu, int that_cpu)
{
	return true;
}

#endif	/* !CONFIG_SMP */

#if defined(CONFIG_ENERGY_MODEL) && defined(CONFIG_CPU_FREQ_GOV_SCHEDUTIL)
extern void rebuild_sched_domains_energy(void);
#else
static inline void rebuild_sched_domains_energy(void)
{
}
#endif

#ifndef arch_scale_cpu_capacity
/**
 * arch_scale_cpu_capacity - get the capacity scale factor of a given CPU.
 * @cpu: the CPU in question.
 *
 * Return: the CPU scale factor normalized against SCHED_CAPACITY_SCALE, i.e.
 *
 *             max_perf(cpu)
 *      ----------------------------- * SCHED_CAPACITY_SCALE
 *      max(max_perf(c) : c \in CPUs)
 */
static __always_inline
unsigned long arch_scale_cpu_capacity(int cpu)
{
	return SCHED_CAPACITY_SCALE;
}
#endif

#ifndef arch_scale_thermal_pressure
static __always_inline
unsigned long arch_scale_thermal_pressure(int cpu)
{
	return 0;
}
#endif

#ifndef arch_set_thermal_pressure
static __always_inline
void arch_set_thermal_pressure(const struct cpumask *cpus,
			       unsigned long th_pressure)
{ }
#endif

static inline int task_node(const struct task_struct *p)
{
	return cpu_to_node(task_cpu(p));
}

#endif /* _LINUX_SCHED_TOPOLOGY_H */
