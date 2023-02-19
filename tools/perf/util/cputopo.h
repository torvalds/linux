/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUTOPO_H
#define __PERF_CPUTOPO_H

#include <linux/types.h>

struct cpu_topology {
	/* The number of unique package_cpus_lists below. */
	u32	  package_cpus_lists;
	/* The number of unique die_cpu_lists below. */
	u32	  die_cpus_lists;
	/* The number of unique core_cpu_lists below. */
	u32	  core_cpus_lists;
	/*
	 * An array of strings where each string is unique and read from
	 * /sys/devices/system/cpu/cpuX/topology/package_cpus_list. From the ABI
	 * each of these is a human-readable list of CPUs sharing the same
	 * physical_package_id. The format is like 0-3, 8-11, 14,17.
	 */
	const char **package_cpus_list;
	/*
	 * An array of string where each string is unique and from
	 * /sys/devices/system/cpu/cpuX/topology/die_cpus_list. From the ABI
	 * each of these is a human-readable list of CPUs within the same die.
	 * The format is like 0-3, 8-11, 14,17.
	 */
	const char **die_cpus_list;
	/*
	 * An array of string where each string is unique and from
	 * /sys/devices/system/cpu/cpuX/topology/core_cpus_list. From the ABI
	 * each of these is a human-readable list of CPUs within the same
	 * core. The format is like 0-3, 8-11, 14,17.
	 */
	const char **core_cpus_list;
};

struct numa_topology_node {
	char		*cpus;
	u32		 node;
	u64		 mem_total;
	u64		 mem_free;
};

struct numa_topology {
	u32				nr;
	struct numa_topology_node	nodes[];
};

struct hybrid_topology_node {
	char		*pmu_name;
	char		*cpus;
};

struct hybrid_topology {
	u32				nr;
	struct hybrid_topology_node	nodes[];
};

/*
 * The topology for online CPUs, lazily created.
 */
const struct cpu_topology *online_topology(void);

struct cpu_topology *cpu_topology__new(void);
void cpu_topology__delete(struct cpu_topology *tp);
/* Determine from the core list whether SMT was enabled. */
bool cpu_topology__smt_on(const struct cpu_topology *topology);
/* Are the sets of SMT siblings all enabled or all disabled in user_requested_cpus. */
bool cpu_topology__core_wide(const struct cpu_topology *topology,
			     const char *user_requested_cpu_list);

struct numa_topology *numa_topology__new(void);
void numa_topology__delete(struct numa_topology *tp);

struct hybrid_topology *hybrid_topology__new(void);
void hybrid_topology__delete(struct hybrid_topology *tp);

#endif /* __PERF_CPUTOPO_H */
