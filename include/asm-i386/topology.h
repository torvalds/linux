/*
 * linux/include/asm-i386/topology.h
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
#ifndef _ASM_I386_TOPOLOGY_H
#define _ASM_I386_TOPOLOGY_H

#ifdef CONFIG_NUMA

#include <asm/mpspec.h>

#include <linux/cpumask.h>

/* Mappings between logical cpu number and node number */
extern cpumask_t node_2_cpu_mask[];
extern int cpu_2_node[];

/* Returns the number of the node containing CPU 'cpu' */
static inline int cpu_to_node(int cpu)
{ 
	return cpu_2_node[cpu];
}

/* Returns the number of the node containing Node 'node'.  This architecture is flat, 
   so it is a pretty simple function! */
#define parent_node(node) (node)

/* Returns a bitmask of CPUs on Node 'node'. */
static inline cpumask_t node_to_cpumask(int node)
{
	return node_2_cpu_mask[node];
}

/* Returns the number of the first CPU on Node 'node'. */
static inline int node_to_first_cpu(int node)
{ 
	cpumask_t mask = node_to_cpumask(node);
	return first_cpu(mask);
}

#define pcibus_to_node(bus) ((long) (bus)->sysdata)
#define pcibus_to_cpumask(bus) node_to_cpumask(pcibus_to_node(bus))

/* sched_domains SD_NODE_INIT for NUMAQ machines */
#define SD_NODE_INIT (struct sched_domain) {		\
	.span			= CPU_MASK_NONE,	\
	.parent			= NULL,			\
	.groups			= NULL,			\
	.min_interval		= 8,			\
	.max_interval		= 32,			\
	.busy_factor		= 32,			\
	.imbalance_pct		= 125,			\
	.cache_nice_tries	= 1,			\
	.busy_idx		= 3,			\
	.idle_idx		= 1,			\
	.newidle_idx		= 2,			\
	.wake_idx		= 1,			\
	.per_cpu_gain		= 100,			\
	.flags			= SD_LOAD_BALANCE	\
				| SD_BALANCE_EXEC	\
				| SD_BALANCE_FORK	\
				| SD_WAKE_BALANCE,	\
	.last_balance		= jiffies,		\
	.balance_interval	= 1,			\
	.nr_balance_failed	= 0,			\
}

extern unsigned long node_start_pfn[];
extern unsigned long node_end_pfn[];
extern unsigned long node_remap_size[];

#define node_has_online_mem(nid) (node_start_pfn[nid] != node_end_pfn[nid])

#else /* !CONFIG_NUMA */
/*
 * Other i386 platforms should define their own version of the 
 * above macros here.
 */

#include <asm-generic/topology.h>

#endif /* CONFIG_NUMA */

#endif /* _ASM_I386_TOPOLOGY_H */
