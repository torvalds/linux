/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUTOPO_H
#define __PERF_CPUTOPO_H

#include <linux/types.h>
#include "env.h"

struct cpu_topology {
	u32	  core_sib;
	u32	  die_sib;
	u32	  thread_sib;
	char	**core_siblings;
	char	**die_siblings;
	char	**thread_siblings;
};

struct numa_topology_node {
	char		*cpus;
	u32		 node;
	u64		 mem_total;
	u64		 mem_free;
};

struct numa_topology {
	u32				nr;
	struct numa_topology_node	nodes[0];
};

struct cpu_topology *cpu_topology__new(void);
void cpu_topology__delete(struct cpu_topology *tp);

struct numa_topology *numa_topology__new(void);
void numa_topology__delete(struct numa_topology *tp);

#endif /* __PERF_CPUTOPO_H */
