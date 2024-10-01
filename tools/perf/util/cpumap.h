/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#include <stdbool.h>
#include <stdio.h>
#include <perf/cpumap.h>

/** Identify where counts are aggregated, -1 implies not to aggregate. */
struct aggr_cpu_id {
	/** A value in the range 0 to number of threads. */
	int thread_idx;
	/** The numa node X as read from /sys/devices/system/node/nodeX. */
	int node;
	/**
	 * The socket number as read from
	 * /sys/devices/system/cpu/cpuX/topology/physical_package_id.
	 */
	int socket;
	/** The die id as read from /sys/devices/system/cpu/cpuX/topology/die_id. */
	int die;
	/** The cluster id as read from /sys/devices/system/cpu/cpuX/topology/cluster_id */
	int cluster;
	/** The cache level as read from /sys/devices/system/cpu/cpuX/cache/indexY/level */
	int cache_lvl;
	/**
	 * The cache instance ID, which is the first CPU in the
	 * /sys/devices/system/cpu/cpuX/cache/indexY/shared_cpu_list
	 */
	int cache;
	/** The core id as read from /sys/devices/system/cpu/cpuX/topology/core_id. */
	int core;
	/** CPU aggregation, note there is one CPU for each SMT thread. */
	struct perf_cpu cpu;
};

/** A collection of aggr_cpu_id values, the "built" version is sorted and uniqued. */
struct cpu_aggr_map {
	/** Number of valid entries. */
	int nr;
	/** The entries. */
	struct aggr_cpu_id map[];
};

#define cpu_aggr_map__for_each_idx(idx, aggr_map)				\
	for ((idx) = 0; (idx) < aggr_map->nr; (idx)++)

struct perf_record_cpu_map_data;

bool perf_record_cpu_map_data__test_bit(int i, const struct perf_record_cpu_map_data *data);

struct perf_cpu_map *perf_cpu_map__empty_new(int nr);

struct perf_cpu_map *cpu_map__new_data(const struct perf_record_cpu_map_data *data);
size_t cpu_map__snprint(struct perf_cpu_map *map, char *buf, size_t size);
size_t cpu_map__snprint_mask(struct perf_cpu_map *map, char *buf, size_t size);
size_t cpu_map__fprintf(struct perf_cpu_map *map, FILE *fp);
struct perf_cpu_map *cpu_map__online(void); /* thread unsafe */

int cpu__setup_cpunode_map(void);

int cpu__max_node(void);
struct perf_cpu cpu__max_cpu(void);
struct perf_cpu cpu__max_present_cpu(void);

/**
 * cpu_map__is_dummy - Events associated with a pid, rather than a CPU, use a single dummy map with an entry of -1.
 */
static inline bool cpu_map__is_dummy(const struct perf_cpu_map *cpus)
{
	return perf_cpu_map__nr(cpus) == 1 && perf_cpu_map__cpu(cpus, 0).cpu == -1;
}

/**
 * cpu__get_node - Returns the numa node X as read from
 * /sys/devices/system/node/nodeX for the given CPU.
 */
int cpu__get_node(struct perf_cpu cpu);
/**
 * cpu__get_socket_id - Returns the socket number as read from
 * /sys/devices/system/cpu/cpuX/topology/physical_package_id for the given CPU.
 */
int cpu__get_socket_id(struct perf_cpu cpu);
/**
 * cpu__get_die_id - Returns the die id as read from
 * /sys/devices/system/cpu/cpuX/topology/die_id for the given CPU.
 */
int cpu__get_die_id(struct perf_cpu cpu);
/**
 * cpu__get_cluster_id - Returns the cluster id as read from
 * /sys/devices/system/cpu/cpuX/topology/cluster_id for the given CPU
 */
int cpu__get_cluster_id(struct perf_cpu cpu);
/**
 * cpu__get_core_id - Returns the core id as read from
 * /sys/devices/system/cpu/cpuX/topology/core_id for the given CPU.
 */
int cpu__get_core_id(struct perf_cpu cpu);

/**
 * cpu_aggr_map__empty_new - Create a cpu_aggr_map of size nr with every entry
 * being empty.
 */
struct cpu_aggr_map *cpu_aggr_map__empty_new(int nr);

typedef struct aggr_cpu_id (*aggr_cpu_id_get_t)(struct perf_cpu cpu, void *data);

/**
 * cpu_aggr_map__new - Create a cpu_aggr_map with an aggr_cpu_id for each cpu in
 * cpus. The aggr_cpu_id is created with 'get_id' that may have a data value
 * passed to it. The cpu_aggr_map is sorted with duplicate values removed.
 */
struct cpu_aggr_map *cpu_aggr_map__new(const struct perf_cpu_map *cpus,
				       aggr_cpu_id_get_t get_id,
				       void *data, bool needs_sort);

bool aggr_cpu_id__equal(const struct aggr_cpu_id *a, const struct aggr_cpu_id *b);
bool aggr_cpu_id__is_empty(const struct aggr_cpu_id *a);
struct aggr_cpu_id aggr_cpu_id__empty(void);


/**
 * aggr_cpu_id__socket - Create an aggr_cpu_id with the socket populated with
 * the socket for cpu. The function signature is compatible with
 * aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__socket(struct perf_cpu cpu, void *data);
/**
 * aggr_cpu_id__die - Create an aggr_cpu_id with the die and socket populated
 * with the die and socket for cpu. The function signature is compatible with
 * aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__die(struct perf_cpu cpu, void *data);
/**
 * aggr_cpu_id__cluster - Create an aggr_cpu_id with cluster, die and socket
 * populated with the cluster, die and socket for cpu. The function signature
 * is compatible with aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__cluster(struct perf_cpu cpu, void *data);
/**
 * aggr_cpu_id__core - Create an aggr_cpu_id with the core, cluster, die and
 * socket populated with the core, die and socket for cpu. The function
 * signature is compatible with aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__core(struct perf_cpu cpu, void *data);
/**
 * aggr_cpu_id__core - Create an aggr_cpu_id with the cpu, core, die and socket
 * populated with the cpu, core, die and socket for cpu. The function signature
 * is compatible with aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__cpu(struct perf_cpu cpu, void *data);
/**
 * aggr_cpu_id__node - Create an aggr_cpu_id with the numa node populated for
 * cpu. The function signature is compatible with aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__node(struct perf_cpu cpu, void *data);
/**
 * aggr_cpu_id__global - Create an aggr_cpu_id for global aggregation.
 * The function signature is compatible with aggr_cpu_id_get_t.
 */
struct aggr_cpu_id aggr_cpu_id__global(struct perf_cpu cpu, void *data);
#endif /* __PERF_CPUMAP_H */
