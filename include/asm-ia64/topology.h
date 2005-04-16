/*
 * linux/include/asm-ia64/topology.h
 *
 * Copyright (C) 2002, Erich Focht, NEC
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef _ASM_IA64_TOPOLOGY_H
#define _ASM_IA64_TOPOLOGY_H

#include <asm/acpi.h>
#include <asm/numa.h>
#include <asm/smp.h>

#ifdef CONFIG_NUMA
/*
 * Returns the number of the node containing CPU 'cpu'
 */
#define cpu_to_node(cpu) (int)(cpu_to_node_map[cpu])

/*
 * Returns a bitmask of CPUs on Node 'node'.
 */
#define node_to_cpumask(node) (node_to_cpu_mask[node])

/*
 * Returns the number of the node containing Node 'nid'.
 * Not implemented here. Multi-level hierarchies detected with
 * the help of node_distance().
 */
#define parent_node(nid) (nid)

/*
 * Returns the number of the first CPU on Node 'node'.
 */
#define node_to_first_cpu(node) (__ffs(node_to_cpumask(node)))

void build_cpu_to_node_map(void);

/* sched_domains SD_NODE_INIT for IA64 NUMA machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 80,			\
	.max_interval		= 320,			\
	.busy_factor		= 320,			\
	.imbalance_pct		= 125,			\
	.cache_hot_time		= (10*1000000),		\
	.cache_nice_tries	= 1,			\
	.per_cpu_gain		= 100,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_NEWIDLE	\
				| SD_WAKE_IDLE		\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

/* sched_domains SD_ALLNODES_INIT for IA64 NUMA machines */
#define SD_ALLNODES_INIT (struct sched_domain) {	\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 80,			\
	.max_interval		= 320,			\
	.busy_factor		= 320,			\
	.imbalance_pct		= 125,			\
	.cache_hot_time		= (10*1000000),		\
	.cache_nice_tries	= 1,			\
	.per_cpu_gain		= 100,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 100*(63+num_online_cpus())/64,   \
	.nr_balance_failed	= 0,			\
}

#endif /* CONFIG_NUMA */

#include <asm-generic/topology.h>

#endif /* _ASM_IA64_TOPOLOGY_H */
