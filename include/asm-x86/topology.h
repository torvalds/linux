/*
 * Written by: Matthew Dobson, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#ifndef _ASM_X86_TOPOLOGY_H
#define _ASM_X86_TOPOLOGY_H

#ifdef CONFIG_NUMA
#include <linux/cpumask.h>
#include <asm/mpspec.h>

/* Mappings between logical cpu number and node number */
#ifdef CONFIG_X86_32
extern int cpu_to_node_map[];
#else
/* Returns the number of the current Node. */
#define numa_node_id()		(early_cpu_to_node(raw_smp_processor_id()))
#endif

DECLARE_PER_CPU(int, x86_cpu_to_node_map);

#ifdef CONFIG_SMP
extern int x86_cpu_to_node_map_init[];
extern void *x86_cpu_to_node_map_early_ptr;
#else
#define x86_cpu_to_node_map_early_ptr NULL
#endif

extern cpumask_t node_to_cpumask_map[];

#define NUMA_NO_NODE	(-1)

/* Returns the number of the node containing CPU 'cpu' */
#ifdef CONFIG_X86_32
#define early_cpu_to_node(cpu)	cpu_to_node(cpu)
static inline int cpu_to_node(int cpu)
{
	return cpu_to_node_map[cpu];
}

#else /* CONFIG_X86_64 */

#ifdef CONFIG_SMP
static inline int early_cpu_to_node(int cpu)
{
	int *cpu_to_node_map = x86_cpu_to_node_map_early_ptr;

	if (cpu_to_node_map)
		return cpu_to_node_map[cpu];
	else if (per_cpu_offset(cpu))
		return per_cpu(x86_cpu_to_node_map, cpu);
	else
		return NUMA_NO_NODE;
}
#else
#define	early_cpu_to_node(cpu)	cpu_to_node(cpu)
#endif

static inline int cpu_to_node(int cpu)
{
#ifdef CONFIG_DEBUG_PER_CPU_MAPS
	if (x86_cpu_to_node_map_early_ptr) {
		printk("KERN_NOTICE cpu_to_node(%d): usage too early!\n",
		       (int)cpu);
		dump_stack();
		return ((int *)x86_cpu_to_node_map_early_ptr)[cpu];
	}
#endif
	return per_cpu(x86_cpu_to_node_map, cpu);
}

#ifdef	CONFIG_NUMA

/* Returns a pointer to the cpumask of CPUs on Node 'node'. */
#define node_to_cpumask_ptr(v, node)		\
		cpumask_t *v = &(node_to_cpumask_map[node])

#define node_to_cpumask_ptr_next(v, node)	\
			   v = &(node_to_cpumask_map[node])
#endif

#endif /* CONFIG_X86_64 */

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

#ifdef CONFIG_X86_32
extern unsigned long node_start_pfn[];
extern unsigned long node_end_pfn[];
extern unsigned long node_remap_size[];
#define node_has_online_mem(nid) (node_start_pfn[nid] != node_end_pfn[nid])

# ifdef CONFIG_X86_HT
#  define ENABLE_TOPO_DEFINES
# endif

# define SD_CACHE_NICE_TRIES	1
# define SD_IDLE_IDX		1
# define SD_NEWIDLE_IDX		2
# define SD_FORKEXEC_IDX	0

#else

# ifdef CONFIG_SMP
#  define ENABLE_TOPO_DEFINES
# endif

# define SD_CACHE_NICE_TRIES	2
# define SD_IDLE_IDX		2
# define SD_NEWIDLE_IDX		2
# define SD_FORKEXEC_IDX	1

#endif

/* sched_domains SD_NODE_INIT for NUMAQ machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= SD_CACHE_NICE_TRIES,	\
	.busy_idx		= 3,			\
	.idle_idx		= SD_IDLE_IDX,		\
	.newidle_idx		= SD_NEWIDLE_IDX,	\
	.wake_idx		= 1,			\
	.forkexec_idx		= SD_FORKEXEC_IDX,	\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_FORK	\
				| SD_SERIALIZE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
}

#ifdef CONFIG_X86_64_ACPI_NUMA
extern int __node_distance(int, int);
#define node_distance(a, b) __node_distance(a, b)
#endif

#else /* CONFIG_NUMA */

#endif

#include <asm-generic/topology.h>

extern cpumask_t cpu_coregroup_map(int cpu);

#ifdef ENABLE_TOPO_DEFINES
#define topology_physical_package_id(cpu)	(cpu_data(cpu).phys_proc_id)
#define topology_core_id(cpu)			(cpu_data(cpu).cpu_core_id)
#define topology_core_siblings(cpu)		(per_cpu(cpu_core_map, cpu))
#define topology_thread_siblings(cpu)		(per_cpu(cpu_sibling_map, cpu))
#endif

static inline void arch_fix_phys_package_id(int num, u32 slot)
{
}

struct pci_bus;
void set_pci_bus_resources_arch_default(struct pci_bus *b);

#ifdef CONFIG_SMP
#define mc_capable()			(boot_cpu_data.x86_max_cores > 1)
#define smt_capable()			(smp_num_siblings > 1)
#endif

#ifdef CONFIG_NUMA
extern int get_mp_bus_to_node(int busnum);
extern void set_mp_bus_to_node(int busnum, int node);
#else
static inline int get_mp_bus_to_node(int busnum)
{
	return 0;
}
static inline void set_mp_bus_to_node(int busnum, int node)
{
}
#endif

#endif
