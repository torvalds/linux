/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ENV_H
#define __PERF_ENV_H

#include <linux/types.h>
#include "cpumap.h"

struct cpu_topology_map {
	int	socket_id;
	int	core_id;
};

struct cpu_cache_level {
	u32	level;
	u32	line_size;
	u32	sets;
	u32	ways;
	char	*type;
	char	*size;
	char	*map;
};

struct numa_node {
	u32		 node;
	u64		 mem_total;
	u64		 mem_free;
	struct cpu_map	*map;
};

struct perf_env {
	char			*hostname;
	char			*os_release;
	char			*version;
	char			*arch;
	int			nr_cpus_online;
	int			nr_cpus_avail;
	char			*cpu_desc;
	char			*cpuid;
	unsigned long long	total_mem;
	unsigned int		msr_pmu_type;

	int			nr_cmdline;
	int			nr_sibling_cores;
	int			nr_sibling_threads;
	int			nr_numa_nodes;
	int			nr_pmu_mappings;
	int			nr_groups;
	char			*cmdline;
	const char		**cmdline_argv;
	char			*sibling_cores;
	char			*sibling_threads;
	char			*pmu_mappings;
	struct cpu_topology_map	*cpu;
	struct cpu_cache_level	*caches;
	int			 caches_cnt;
	struct numa_node	*numa_nodes;
};

extern struct perf_env perf_env;

void perf_env__exit(struct perf_env *env);

int perf_env__set_cmdline(struct perf_env *env, int argc, const char *argv[]);

int perf_env__read_cpu_topology_map(struct perf_env *env);

void cpu_cache_level__free(struct cpu_cache_level *cache);

const char *perf_env__arch(struct perf_env *env);
#endif /* __PERF_ENV_H */
