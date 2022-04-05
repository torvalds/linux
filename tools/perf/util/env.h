/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ENV_H
#define __PERF_ENV_H

#include <linux/types.h>
#include <linux/rbtree.h>
#include "rwsem.h"

struct perf_cpu_map;

struct cpu_topology_map {
	int	socket_id;
	int	die_id;
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
	struct perf_cpu_map	*map;
};

struct memory_node {
	u64		 node;
	u64		 size;
	unsigned long	*set;
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
	int			nr_sibling_dies;
	int			nr_sibling_threads;
	int			nr_numa_nodes;
	int			nr_memory_nodes;
	int			nr_pmu_mappings;
	int			nr_groups;
	char			*cmdline;
	const char		**cmdline_argv;
	char			*sibling_cores;
	char			*sibling_dies;
	char			*sibling_threads;
	char			*pmu_mappings;
	struct cpu_topology_map	*cpu;
	struct cpu_cache_level	*caches;
	int			 caches_cnt;
	u32			comp_ratio;
	u32			comp_ver;
	u32			comp_type;
	u32			comp_level;
	u32			comp_mmap_len;
	struct numa_node	*numa_nodes;
	struct memory_node	*memory_nodes;
	unsigned long long	 memory_bsize;
	u64                     clockid_res_ns;

	/*
	 * bpf_info_lock protects bpf rbtrees. This is needed because the
	 * trees are accessed by different threads in perf-top
	 */
	struct {
		struct rw_semaphore	lock;
		struct rb_root		infos;
		u32			infos_cnt;
		struct rb_root		btfs;
		u32			btfs_cnt;
	} bpf_progs;
};

enum perf_compress_type {
	PERF_COMP_NONE = 0,
	PERF_COMP_ZSTD,
	PERF_COMP_MAX
};

struct bpf_prog_info_node;
struct btf_node;

extern struct perf_env perf_env;

void perf_env__exit(struct perf_env *env);

int perf_env__set_cmdline(struct perf_env *env, int argc, const char *argv[]);

int perf_env__read_cpu_topology_map(struct perf_env *env);

void cpu_cache_level__free(struct cpu_cache_level *cache);

const char *perf_env__arch(struct perf_env *env);
const char *perf_env__raw_arch(struct perf_env *env);
int perf_env__nr_cpus_avail(struct perf_env *env);

void perf_env__init(struct perf_env *env);
void perf_env__insert_bpf_prog_info(struct perf_env *env,
				    struct bpf_prog_info_node *info_node);
struct bpf_prog_info_node *perf_env__find_bpf_prog_info(struct perf_env *env,
							__u32 prog_id);
void perf_env__insert_btf(struct perf_env *env, struct btf_node *btf_node);
struct btf_node *perf_env__find_btf(struct perf_env *env, __u32 btf_id);
#endif /* __PERF_ENV_H */
