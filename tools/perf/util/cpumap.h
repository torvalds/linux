/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#include <stdio.h>
#include <stdbool.h>
#include <internal/cpumap.h>
#include <perf/cpumap.h>

/** Identify where counts are aggregated, -1 implies not to aggregate. */
struct aggr_cpu_id {
	/** A value in the range 0 to number of threads. */
	int thread;
	/** The numa node X as read from /sys/devices/system/node/nodeX. */
	int node;
	/**
	 * The socket number as read from
	 * /sys/devices/system/cpu/cpuX/topology/physical_package_id.
	 */
	int socket;
	/** The die id as read from /sys/devices/system/cpu/cpuX/topology/die_id. */
	int die;
	/** The core id as read from /sys/devices/system/cpu/cpuX/topology/core_id. */
	int core;
};

struct cpu_aggr_map {
	refcount_t refcnt;
	int nr;
	struct aggr_cpu_id map[];
};

struct perf_record_cpu_map_data;

struct perf_cpu_map *perf_cpu_map__empty_new(int nr);
struct cpu_aggr_map *cpu_aggr_map__empty_new(int nr);

struct perf_cpu_map *cpu_map__new_data(struct perf_record_cpu_map_data *data);
size_t cpu_map__snprint(struct perf_cpu_map *map, char *buf, size_t size);
size_t cpu_map__snprint_mask(struct perf_cpu_map *map, char *buf, size_t size);
size_t cpu_map__fprintf(struct perf_cpu_map *map, FILE *fp);
struct aggr_cpu_id cpu_map__get_socket_aggr_by_cpu(int cpu, void *data);
struct aggr_cpu_id cpu_map__get_die_aggr_by_cpu(int cpu, void *data);
struct aggr_cpu_id cpu_map__get_core_aggr_by_cpu(int cpu, void *data);
struct aggr_cpu_id cpu_map__get_node_aggr_by_cpu(int cpu, void *data);
int cpu_map__build_socket_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **sockp);
int cpu_map__build_die_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **diep);
int cpu_map__build_core_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **corep);
int cpu_map__build_node_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **nodep);
const struct perf_cpu_map *cpu_map__online(void); /* thread unsafe */

int cpu__setup_cpunode_map(void);

int cpu__max_node(void);
int cpu__max_cpu(void);
int cpu__max_present_cpu(void);
/**
 * cpu__get_node - Returns the numa node X as read from
 * /sys/devices/system/node/nodeX for the given CPU.
 */
int cpu__get_node(int cpu);
/**
 * cpu__get_socket_id - Returns the socket number as read from
 * /sys/devices/system/cpu/cpuX/topology/physical_package_id for the given CPU.
 */
int cpu__get_socket_id(int cpu);
/**
 * cpu__get_die_id - Returns the die id as read from
 * /sys/devices/system/cpu/cpuX/topology/die_id for the given CPU.
 */
int cpu__get_die_id(int cpu);
/**
 * cpu__get_core_id - Returns the core id as read from
 * /sys/devices/system/cpu/cpuX/topology/core_id for the given CPU.
 */
int cpu__get_core_id(int cpu);


int cpu_map__build_map(struct perf_cpu_map *cpus, struct cpu_aggr_map **res,
		       struct aggr_cpu_id (*f)(int cpu, void *data),
		       void *data);

bool cpu_map__has(struct perf_cpu_map *cpus, int cpu);

bool aggr_cpu_id__equal(const struct aggr_cpu_id *a, const struct aggr_cpu_id *b);
bool aggr_cpu_id__is_empty(const struct aggr_cpu_id *a);
struct aggr_cpu_id aggr_cpu_id__empty(void);

#endif /* __PERF_CPUMAP_H */
