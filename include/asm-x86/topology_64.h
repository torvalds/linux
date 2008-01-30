#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

#ifdef CONFIG_NUMA
#include <linux/cpumask.h>
#include <asm/mpspec.h>

/* Mappings between logical cpu number and node number */
extern int cpu_to_node_map[];
extern cpumask_t node_to_cpumask_map[];

#ifdef CONFIG_ACPI_NUMA
extern int __node_distance(int, int);
#define node_distance(a,b) __node_distance(a,b)
#endif

/* Returns the number of the node containing CPU 'cpu' */
static inline int cpu_to_node(int cpu)
{
	return cpu_to_node_map[cpu];
}

/*
 * Returns the number of the node containing Node 'node'. This
 * architecture is flat, so it is a pretty simple function!
 */
#define parent_node(node) (node)

/* Returns a bitmask of CPUs on Node 'node'. */
static inline cpumask_t node_to_cpumask(int node)
{
	return node_to_cpumask_map[node];
}

/* Returns the number of the first CPU on Node 'node'. */
static inline int node_to_first_cpu(int node)
{
	cpumask_t mask = node_to_cpumask(node);

	return first_cpu(mask);
}

#define pcibus_to_node(bus) __pcibus_to_node(bus)
#define pcibus_to_cpumask(bus) __pcibus_to_cpumask(bus)

#define numa_node_id()			read_pda(nodenumber)

/* sched_domains SD_NODE_INIT for x86_64 machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.child			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 2,			\
	.busy_idx		= 3,			\
	.idle_idx		= 2,			\
	.newidle_idx		= 0,			\
	.wake_idx		= 1,			\
	.forkexec_idx		= 1,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_FORK	\
				| SD_SERIALIZE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

#else /* CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif

extern cpumask_t cpu_coregroup_map(int cpu);

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)	(cpu_data(cpu).phys_proc_id)
#define topology_core_id(cpu)			(cpu_data(cpu).cpu_core_id)
#define topology_core_siblings(cpu)		(per_cpu(cpu_core_map, cpu))
#define topology_thread_siblings(cpu)		(per_cpu(cpu_sibling_map, cpu))
#define mc_capable()			(boot_cpu_data.x86_max_cores > 1)
#define smt_capable()			(smp_num_siblings > 1)
#endif

#endif
