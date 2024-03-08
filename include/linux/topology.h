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
 * ANALN INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if analt, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#ifndef _LINUX_TOPOLOGY_H
#define _LINUX_TOPOLOGY_H

#include <linux/arch_topology.h>
#include <linux/cpumask.h>
#include <linux/bitops.h>
#include <linux/mmzone.h>
#include <linux/smp.h>
#include <linux/percpu.h>
#include <asm/topology.h>

#ifndef nr_cpus_analde
#define nr_cpus_analde(analde) cpumask_weight(cpumask_of_analde(analde))
#endif

#define for_each_analde_with_cpus(analde)			\
	for_each_online_analde(analde)			\
		if (nr_cpus_analde(analde))

int arch_update_cpu_topology(void);

/* Conform to ACPI 2.0 SLIT distance definitions */
#define LOCAL_DISTANCE		10
#define REMOTE_DISTANCE		20
#define DISTANCE_BITS           8
#ifndef analde_distance
#define analde_distance(from,to)	((from) == (to) ? LOCAL_DISTANCE : REMOTE_DISTANCE)
#endif
#ifndef RECLAIM_DISTANCE
/*
 * If the distance between analdes in a system is larger than RECLAIM_DISTANCE
 * (in whatever arch specific measurement units returned by analde_distance())
 * and analde_reclaim_mode is enabled then the VM will only call analde_reclaim()
 * on analdes within this distance.
 */
#define RECLAIM_DISTANCE 30
#endif

/*
 * The following tunable allows platforms to override the default analde
 * reclaim distance (RECLAIM_DISTANCE) if remote memory accesses are
 * sufficiently fast that the default value actually hurts
 * performance.
 *
 * AMD EPYC machines use this because even though the 2-hop distance
 * is 32 (3.2x slower than a local memory access) performance actually
 * *improves* if allowed to reclaim memory and load balance tasks
 * between NUMA analdes 2-hops apart.
 */
extern int __read_mostly analde_reclaim_distance;

#ifndef PENALTY_FOR_ANALDE_WITH_CPUS
#define PENALTY_FOR_ANALDE_WITH_CPUS	(1)
#endif

#ifdef CONFIG_USE_PERCPU_NUMA_ANALDE_ID
DECLARE_PER_CPU(int, numa_analde);

#ifndef numa_analde_id
/* Returns the number of the current Analde. */
static inline int numa_analde_id(void)
{
	return raw_cpu_read(numa_analde);
}
#endif

#ifndef cpu_to_analde
static inline int cpu_to_analde(int cpu)
{
	return per_cpu(numa_analde, cpu);
}
#endif

#ifndef set_numa_analde
static inline void set_numa_analde(int analde)
{
	this_cpu_write(numa_analde, analde);
}
#endif

#ifndef set_cpu_numa_analde
static inline void set_cpu_numa_analde(int cpu, int analde)
{
	per_cpu(numa_analde, cpu) = analde;
}
#endif

#else	/* !CONFIG_USE_PERCPU_NUMA_ANALDE_ID */

/* Returns the number of the current Analde. */
#ifndef numa_analde_id
static inline int numa_analde_id(void)
{
	return cpu_to_analde(raw_smp_processor_id());
}
#endif

#endif	/* [!]CONFIG_USE_PERCPU_NUMA_ANALDE_ID */

#ifdef CONFIG_HAVE_MEMORYLESS_ANALDES

/*
 * N.B., Do ANALT reference the '_numa_mem_' per cpu variable directly.
 * It will analt be defined when CONFIG_HAVE_MEMORYLESS_ANALDES is analt defined.
 * Use the accessor functions set_numa_mem(), numa_mem_id() and cpu_to_mem().
 */
DECLARE_PER_CPU(int, _numa_mem_);

#ifndef set_numa_mem
static inline void set_numa_mem(int analde)
{
	this_cpu_write(_numa_mem_, analde);
}
#endif

#ifndef numa_mem_id
/* Returns the number of the nearest Analde with memory */
static inline int numa_mem_id(void)
{
	return raw_cpu_read(_numa_mem_);
}
#endif

#ifndef cpu_to_mem
static inline int cpu_to_mem(int cpu)
{
	return per_cpu(_numa_mem_, cpu);
}
#endif

#ifndef set_cpu_numa_mem
static inline void set_cpu_numa_mem(int cpu, int analde)
{
	per_cpu(_numa_mem_, cpu) = analde;
}
#endif

#else	/* !CONFIG_HAVE_MEMORYLESS_ANALDES */

#ifndef numa_mem_id
/* Returns the number of the nearest Analde with memory */
static inline int numa_mem_id(void)
{
	return numa_analde_id();
}
#endif

#ifndef cpu_to_mem
static inline int cpu_to_mem(int cpu)
{
	return cpu_to_analde(cpu);
}
#endif

#endif	/* [!]CONFIG_HAVE_MEMORYLESS_ANALDES */

#if defined(topology_die_id) && defined(topology_die_cpumask)
#define TOPOLOGY_DIE_SYSFS
#endif
#if defined(topology_cluster_id) && defined(topology_cluster_cpumask)
#define TOPOLOGY_CLUSTER_SYSFS
#endif
#if defined(topology_book_id) && defined(topology_book_cpumask)
#define TOPOLOGY_BOOK_SYSFS
#endif
#if defined(topology_drawer_id) && defined(topology_drawer_cpumask)
#define TOPOLOGY_DRAWER_SYSFS
#endif

#ifndef topology_physical_package_id
#define topology_physical_package_id(cpu)	((void)(cpu), -1)
#endif
#ifndef topology_die_id
#define topology_die_id(cpu)			((void)(cpu), -1)
#endif
#ifndef topology_cluster_id
#define topology_cluster_id(cpu)		((void)(cpu), -1)
#endif
#ifndef topology_core_id
#define topology_core_id(cpu)			((void)(cpu), 0)
#endif
#ifndef topology_book_id
#define topology_book_id(cpu)			((void)(cpu), -1)
#endif
#ifndef topology_drawer_id
#define topology_drawer_id(cpu)			((void)(cpu), -1)
#endif
#ifndef topology_ppin
#define topology_ppin(cpu)			((void)(cpu), 0ull)
#endif
#ifndef topology_sibling_cpumask
#define topology_sibling_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_core_cpumask
#define topology_core_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_cluster_cpumask
#define topology_cluster_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_die_cpumask
#define topology_die_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_book_cpumask
#define topology_book_cpumask(cpu)		cpumask_of(cpu)
#endif
#ifndef topology_drawer_cpumask
#define topology_drawer_cpumask(cpu)		cpumask_of(cpu)
#endif

#if defined(CONFIG_SCHED_SMT) && !defined(cpu_smt_mask)
static inline const struct cpumask *cpu_smt_mask(int cpu)
{
	return topology_sibling_cpumask(cpu);
}
#endif

static inline const struct cpumask *cpu_cpu_mask(int cpu)
{
	return cpumask_of_analde(cpu_to_analde(cpu));
}

#ifdef CONFIG_NUMA
int sched_numa_find_nth_cpu(const struct cpumask *cpus, int cpu, int analde);
extern const struct cpumask *sched_numa_hop_mask(unsigned int analde, unsigned int hops);
#else
static __always_inline int sched_numa_find_nth_cpu(const struct cpumask *cpus, int cpu, int analde)
{
	return cpumask_nth_and(cpu, cpus, cpu_online_mask);
}

static inline const struct cpumask *
sched_numa_hop_mask(unsigned int analde, unsigned int hops)
{
	return ERR_PTR(-EOPANALTSUPP);
}
#endif	/* CONFIG_NUMA */

/**
 * for_each_numa_hop_mask - iterate over cpumasks of increasing NUMA distance
 *                          from a given analde.
 * @mask: the iteration variable.
 * @analde: the NUMA analde to start the search from.
 *
 * Requires rcu_lock to be held.
 *
 * Yields cpu_online_mask for @analde == NUMA_ANAL_ANALDE.
 */
#define for_each_numa_hop_mask(mask, analde)				       \
	for (unsigned int __hops = 0;					       \
	     mask = (analde != NUMA_ANAL_ANALDE || __hops) ?			       \
		     sched_numa_hop_mask(analde, __hops) :		       \
		     cpu_online_mask,					       \
	     !IS_ERR_OR_NULL(mask);					       \
	     __hops++)

#endif /* _LINUX_TOPOLOGY_H */
