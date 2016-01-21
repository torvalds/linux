#ifndef __PERF_ENV_H
#define __PERF_ENV_H

struct cpu_topology_map {
	int	socket_id;
	int	core_id;
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
	char			*numa_nodes;
	char			*pmu_mappings;
	struct cpu_topology_map	*cpu;
};

extern struct perf_env perf_env;

void perf_env__exit(struct perf_env *env);

int perf_env__set_cmdline(struct perf_env *env, int argc, const char *argv[]);

int perf_env__read_cpu_topology_map(struct perf_env *env);

#endif /* __PERF_ENV_H */
