#ifndef _ASM_X86_64_TOPOLOGY_H
#define _ASM_X86_64_TOPOLOGY_H

#include <linux/config.h>

#ifdef CONFIG_NUMA

#include <asm/mpspec.h>
#include <asm/bitops.h>

/* Map the K8 CPU local memory controllers to a simple 1:1 CPU:NODE topology */

extern cpumask_t cpu_online_map;

extern unsigned char cpu_to_node[];
extern cpumask_t     node_to_cpumask[];

#ifdef CONFIG_ACPI_NUMA
extern int __node_distance(int, int);
#define node_distance(a,b) __node_distance(a,b)
/* #else fallback version */
#endif

#define cpu_to_node(cpu)		(cpu_to_node[cpu])
#define parent_node(node)		(node)
#define node_to_first_cpu(node) 	(first_cpu(node_to_cpumask[node]))
#define node_to_cpumask(node)		(node_to_cpumask[node])
#define pcibus_to_node(bus)		((long)(bus->sysdata))	
#define pcibus_to_cpumask(bus)		node_to_cpumask(pcibus_to_node(bus));

#define numa_node_id()			read_pda(nodenumber)

/* sched_domains SD_NODE_INIT for x86_64 machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 2,			\
	.busy_idx		= 3,			\
	.idle_idx		= 2,			\
	.newidle_idx		= 0, 			\
	.wake_idx		= 1,			\
	.forkexec_idx		= 1,			\
	.per_cpu_gain		= 100,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_FORK	\
				| SD_BALANCE_EXEC	\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

#endif

#ifdef CONFIG_SMP
#define topology_physical_package_id(cpu)				\
	(phys_proc_id[cpu] == BAD_APICID ? -1 : phys_proc_id[cpu])
#define topology_core_id(cpu)						\
	(cpu_core_id[cpu] == BAD_APICID ? 0 : cpu_core_id[cpu])
#define topology_core_siblings(cpu)		(cpu_core_map[cpu])
#define topology_thread_siblings(cpu)		(cpu_sibling_map[cpu])
#endif

#include <asm-generic/topology.h>

extern cpumask_t cpu_coregroup_map(int cpu);

#endif
