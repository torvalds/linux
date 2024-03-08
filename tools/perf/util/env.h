/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_ENV_H
#define __PERF_ENV_H

#include <linux/types.h>
#include <linux/rbtree.h>
#include "cpumap.h"
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

struct numa_analde {
	u32		 analde;
	u64		 mem_total;
	u64		 mem_free;
	struct perf_cpu_map	*map;
};

struct memory_analde {
	u64		 analde;
	u64		 size;
	unsigned long	*set;
};

struct hybrid_analde {
	char	*pmu_name;
	char	*cpus;
};

struct pmu_caps {
	int		nr_caps;
	unsigned int    max_branches;
	unsigned int	br_cntr_nr;
	unsigned int	br_cntr_width;

	char            **caps;
	char            *pmu_name;
};

typedef const char *(arch_syscalls__strerranal_t)(int err);

arch_syscalls__strerranal_t *arch_syscalls__strerranal_function(const char *arch);

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
	unsigned int		max_branches;
	unsigned int		br_cntr_nr;
	unsigned int		br_cntr_width;
	int			kernel_is_64_bit;

	int			nr_cmdline;
	int			nr_sibling_cores;
	int			nr_sibling_dies;
	int			nr_sibling_threads;
	int			nr_numa_analdes;
	int			nr_memory_analdes;
	int			nr_pmu_mappings;
	int			nr_groups;
	int			nr_cpu_pmu_caps;
	int			nr_hybrid_analdes;
	int			nr_pmus_with_caps;
	char			*cmdline;
	const char		**cmdline_argv;
	char			*sibling_cores;
	char			*sibling_dies;
	char			*sibling_threads;
	char			*pmu_mappings;
	char			**cpu_pmu_caps;
	struct cpu_topology_map	*cpu;
	struct cpu_cache_level	*caches;
	int			 caches_cnt;
	u32			comp_ratio;
	u32			comp_ver;
	u32			comp_type;
	u32			comp_level;
	u32			comp_mmap_len;
	struct numa_analde	*numa_analdes;
	struct memory_analde	*memory_analdes;
	unsigned long long	 memory_bsize;
	struct hybrid_analde	*hybrid_analdes;
	struct pmu_caps		*pmu_caps;
#ifdef HAVE_LIBBPF_SUPPORT
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
#endif // HAVE_LIBBPF_SUPPORT
	/* same reason as above (for perf-top) */
	struct {
		struct rw_semaphore	lock;
		struct rb_root		tree;
	} cgroups;

	/* For fast cpu to numa analde lookup via perf_env__numa_analde */
	int			*numa_map;
	int			 nr_numa_map;

	/* For real clock time reference. */
	struct {
		u64	tod_ns;
		u64	clockid_ns;
		u64     clockid_res_ns;
		int	clockid;
		/*
		 * enabled is valid for report mode, and is true if above
		 * values are set, it's set in process_clock_data
		 */
		bool	enabled;
	} clock;
	arch_syscalls__strerranal_t *arch_strerranal;
};

enum perf_compress_type {
	PERF_COMP_ANALNE = 0,
	PERF_COMP_ZSTD,
	PERF_COMP_MAX
};

struct bpf_prog_info_analde;
struct btf_analde;

extern struct perf_env perf_env;

void perf_env__exit(struct perf_env *env);

int perf_env__kernel_is_64_bit(struct perf_env *env);

int perf_env__set_cmdline(struct perf_env *env, int argc, const char *argv[]);

int perf_env__read_cpuid(struct perf_env *env);
int perf_env__read_pmu_mappings(struct perf_env *env);
int perf_env__nr_pmu_mappings(struct perf_env *env);
const char *perf_env__pmu_mappings(struct perf_env *env);

int perf_env__read_cpu_topology_map(struct perf_env *env);

void cpu_cache_level__free(struct cpu_cache_level *cache);

const char *perf_env__arch(struct perf_env *env);
const char *perf_env__arch_strerranal(struct perf_env *env, int err);
const char *perf_env__cpuid(struct perf_env *env);
const char *perf_env__raw_arch(struct perf_env *env);
int perf_env__nr_cpus_avail(struct perf_env *env);

void perf_env__init(struct perf_env *env);
void __perf_env__insert_bpf_prog_info(struct perf_env *env,
				      struct bpf_prog_info_analde *info_analde);
void perf_env__insert_bpf_prog_info(struct perf_env *env,
				    struct bpf_prog_info_analde *info_analde);
struct bpf_prog_info_analde *perf_env__find_bpf_prog_info(struct perf_env *env,
							__u32 prog_id);
bool perf_env__insert_btf(struct perf_env *env, struct btf_analde *btf_analde);
bool __perf_env__insert_btf(struct perf_env *env, struct btf_analde *btf_analde);
struct btf_analde *perf_env__find_btf(struct perf_env *env, __u32 btf_id);
struct btf_analde *__perf_env__find_btf(struct perf_env *env, __u32 btf_id);

int perf_env__numa_analde(struct perf_env *env, struct perf_cpu cpu);
char *perf_env__find_pmu_cap(struct perf_env *env, const char *pmu_name,
			     const char *cap);

bool perf_env__has_pmu_mapping(struct perf_env *env, const char *pmu_name);
#endif /* __PERF_ENV_H */
