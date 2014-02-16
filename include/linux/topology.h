/*
 * include/linux/topology.h
 *
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
#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H

#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/topology.h>

#ifndef node_has_online_mem
#define node_has_online_mem(nid) (1)
#endif

#ifndef nr_cpus_node
#define nr_cpus_node(node) cpumask_weight(cpumask_of_node(node))
#endif

#define for_each_node_with_cpus(node)			\
	for_each_online_node(node)			\
		if (nr_cpus_node(node))

int arch_update_cpu_topology(void);

/* Conform to ACPI 2.0 SLIT distance definitions */
#define LOCAL_DISTANCE		10
#define REMOTE_DISTANCE		20
#ifndef node_distance
#define node_distance(from,to)	((from) == (to) ? LOCAL_DISTANCE : REMOTE_DISTANCE)
#endif
#ifndef RECLAIM_DISTANCE
/*
 * If the distance between nodes in a system is larger than RECLAIM_DISTANCE
 * (in whatever arch specific measurement units returned by node_distance())
 * then switch on zone reclaim on boot.
 */
#define RECLAIM_DISTANCE 30
#endif
#ifndef PENALTY_FOR_NODE_WITH_CPUS
#define PENALTY_FOR_NODE_WITH_CPUS	(1)
#endif

/*
 * Below are the 3 major initializers used in building sched_domains:
 * SD_SIBLING_INIT, for SMT domains
 * SD_CPU_INIT, for SMP domains
 *
 * Any architecture that cares to do any tuning to these values should do so
 * by defining their own arch-specific initializer in include/asm/topology.h.
 * A definition there will automagically override these default initializers
 * and allow arch-specific performance tuning of sched_domains.
 * (Only non-zero and non-null fields need be specified.)
 */

#ifdef CONFIG_SCHED_SMT
/* MCD - Do we really need this?  It is always on if CONFIG_SCHED_SMT is,
 * so can't we drop this in favor of CONFIG_SCHED_SMT?
 */
#define ARCH_HAS_SCHED_WAKE_IDLE
/* Common values for SMT siblings */
#ifndef SD_SIBLING_INIT
#define SD_SIBLING_INIT (struct sched_domain) {				\
	.min_interval		= 1,					\
	.max_interval		= 2,					\
	.busy_factor		= 64,					\
	.imbalance_pct		= 110,					\
									\
	.flags			= 1*SD_LOAD_BALANCE			\
				| 1*SD_BALANCE_NEWIDLE			\
				| 1*SD_BALANCE_EXEC			\
				| 1*SD_BALANCE_FORK			\
				| 0*SD_BALANCE_WAKE			\
				| 1*SD_WAKE_AFFINE			\
				| 1*SD_SHARE_CPUPOWER			\
				| 1*SD_SHARE_PKG_RESOURCES		\
				| 0*SD_SERIALIZE			\
				| 0*SD_PREFER_SIBLING			\
				| arch_sd_sibling_asym_packing()	\
				,					\
	.last_balance		= jiffies,				\
	.balance_interval	= 1,					\
	.smt_gain		= 1178,	/* 15% */			\
	.max_newidle_lb_cost	= 0,					\
	.next_decay_max_lb_cost	= jiffies,				\
}
#endif
#endif /* CONFIG_SCHED_SMT */

#ifdef CONFIG_SCHED_MC
/* Common values for MC siblings. for now mostly derived from SD_CPU_INIT */
#ifndef SD_MC_INIT
#define SD_MC_INIT (struct sched_domain) {				\
	.min_interval		= 1,					\
	.max_interval		= 4,					\
	.busy_factor		= 64,					\
	.imbalance_pct		= 125,					\
	.cache_nice_tries	= 1,					\
	.busy_idx		= 2,					\
	.wake_idx		= 0,					\
	.forkexec_idx		= 0,					\
									\
	.flags			= 1*SD_LOAD_BALANCE			\
				| 1*SD_BALANCE_NEWIDLE			\
				| 1*SD_BALANCE_EXEC			\
				| 1*SD_BALANCE_FORK			\
				| 0*SD_BALANCE_WAKE			\
				| 1*SD_WAKE_AFFINE			\
				| 0*SD_SHARE_CPUPOWER			\
				| 1*SD_SHARE_PKG_RESOURCES		\
				| 0*SD_SERIALIZE			\
				,					\
	.last_balance		= jiffies,				\
	.balance_interval	= 1,					\
	.max_newidle_lb_cost	= 0,					\
	.next_decay_max_lb_cost	= jiffies,				\
}
#endif
#endif /* CONFIG_SCHED_MC */

/* Common values for CPUs */
#ifndef SD_CPU_INIT
#define SD_CPU_INIT (struct sched_domain) {				\
	.min_interval		= 1,					\
	.max_interval		= 4,					\
	.busy_factor		= 64,					\
	.imbalance_pct		= 125,					\
	.cache_nice_tries	= 1,					\
	.busy_idx		= 2,					\
	.idle_idx		= 1,					\
	.newidle_idx		= 0,					\
	.wake_idx		= 0,					\
	.forkexec_idx		= 0,					\
									\
	.flags			= 1*SD_LOAD_BALANCE			\
				| 1*SD_BALANCE_NEWIDLE			\
				| 1*SD_BALANCE_EXEC			\
				| 1*SD_BALANCE_FORK			\
				| 0*SD_BALANCE_WAKE			\
				| 1*SD_WAKE_AFFINE			\
				| 0*SD_SHARE_CPUPOWER			\
				| 0*SD_SHARE_PKG_RESOURCES		\
				| 0*SD_SERIALIZE			\
				| 1*SD_PREFER_SIBLING			\
				,					\
	.last_balance		= jiffies,				\
	.balance_interval	= 1,					\
	.max_newidle_lb_cost	= 0,					\
	.next_decay_max_lb_cost	= jiffies,				\
}
#endif

#ifdef CONFIG_SCHED_BOOK
#ifndef SD_BOOK_INIT
#error Please define an appropriate SD_BOOK_INIT in include/asm/topology.h!!!
#endif
#endif /* CONFIG_SCHED_BOOK */

#ifdef CONFIG_USE_PERCPU_NUMA_NODE_ID
DECLARE_PER_CPU(int, numa_node);

#ifndef numa_node_id
/* Returns the number of the current Node. */
static inline int numa_node_id(void)
{
	return __this_cpu_read(numa_node);
}
#endif

#ifndef cpu_to_node
static inline int cpu_to_node(int cpu)
{
	return per_cpu(numa_node, cpu);
}
#endif

#ifndef set_numa_node
static inline void set_numa_node(int node)
{
	this_cpu_write(numa_node, node);
}
#endif

#ifndef set_cpu_numa_node
static inline void set_cpu_numa_node(int cpu, int node)
{
	per_cpu(numa_node, cpu) = node;
}
#endif

#else	/* !CONFIG_USE_PERCPU_NUMA_NODE_ID */

/* Returns the number of the current Node. */
#ifndef numa_node_id
static inline int numa_node_id(void)
{
	return cpu_to_node(raw_smp_processor_id());
}
#endif

#endif	/* [!]CONFIG_USE_PERCPU_NUMA_NODE_ID */

#ifdef CONFIG_HAVE_MEMORYLESS_NODES

/*
 * N.B., Do NOT reference the '_numa_mem_' per cpu variable directly.
 * It will not be defined when CONFIG_HAVE_MEMORYLESS_NODES is not defined.
 * Use the accessor functions set_numa_mem(), numa_mem_id() and cpu_to_mem().
 */
DECLARE_PER_CPU(int, _numa_mem_);

#ifndef set_numa_mem
static inline void set_numa_mem(int node)
{
	this_cpu_write(_numa_mem_, node);
}
#endif

#ifndef numa_mem_id
/* Returns the number of the nearest Node with memory */
static inline int numa_mem_id(void)
{
	return __this_cpu_read(_numa_mem_);
}
#endif

#ifndef cpu_to_mem
static inline int cpu_to_mem(int cpu)
{
	return per_cpu(_numa_mem_, cpu);
}
#endif

#ifndef set_cpu_numa_mem
static inline void set_cpu_numa_mem(int cpu, int node)
{
	per_cpu(_numa_mem_, cpu) = node;
}
#endif

#else	/* !CONFIG_HAVE_MEMORYLESS_NODES */

#ifndef numa_mem_id
/* Returns the number of the nearest Node with memory */
static inline int numa_mem_id(void)
{
	return numa_node_id();
}
#endif

#ifndef cpu_to_mem
static inline int cpu_to_mem(int cpu)
{
	return cpu_to_node(cpu);
}
#endif

#endif	/* [!]CONFIG_HAVE_MEMORYLESS_NODES */

#ifndef topology_physical_package_id
#define topology_physical_package_id(cpu)	((void)(cpu), -1)
#endif
#ifndef topology_core_id
#define topology_core_id(cpu)			((void)(cpu), 0)
#endif
#ifndef topology_thread_cpumask
#define topology_thread_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_core_cpumask
#define topology_core_cpumask(cpu)		cpumask_of(cpu)
#endif

#endif /* _LINUX_TOPOLOGY_H */
