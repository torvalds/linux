/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#include <stdio.h>
#include <stdbool.h>
#include <internal/cpumap.h>
#include <perf/cpumap.h>

struct perf_record_cpu_map_data;

struct perf_cpu_map *perf_cpu_map__empty_new(int nr);
struct perf_cpu_map *cpu_map__new_data(struct perf_record_cpu_map_data *data);
size_t cpu_map__snprint(struct perf_cpu_map *map, char *buf, size_t size);
size_t cpu_map__snprint_mask(struct perf_cpu_map *map, char *buf, size_t size);
size_t cpu_map__fprintf(struct perf_cpu_map *map, FILE *fp);
int cpu_map__get_socket_id(int cpu);
int cpu_map__get_socket(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__get_die_id(int cpu);
int cpu_map__get_die(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__get_core_id(int cpu);
int cpu_map__get_core(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__get_node_id(int cpu);
int cpu_map__get_node(struct perf_cpu_map *map, int idx, void *data);
int cpu_map__build_socket_map(struct perf_cpu_map *cpus, struct perf_cpu_map **sockp);
int cpu_map__build_die_map(struct perf_cpu_map *cpus, struct perf_cpu_map **diep);
int cpu_map__build_core_map(struct perf_cpu_map *cpus, struct perf_cpu_map **corep);
int cpu_map__build_node_map(struct perf_cpu_map *cpus, struct perf_cpu_map **nodep);
const struct perf_cpu_map *cpu_map__online(void); /* thread unsafe */

static inline int cpu_map__socket(struct perf_cpu_map *sock, int s)
{
	if (!sock || s > sock->nr || s < 0)
		return 0;
	return sock->map[s];
}

static inline int cpu_map__id_to_socket(int id)
{
	return id >> 24;
}

static inline int cpu_map__id_to_die(int id)
{
	return (id >> 16) & 0xff;
}

static inline int cpu_map__id_to_cpu(int id)
{
	return id & 0xffff;
}

int cpu__setup_cpunode_map(void);

int cpu__max_node(void);
int cpu__max_cpu(void);
int cpu__max_present_cpu(void);
int cpu__get_node(int cpu);

int cpu_map__build_map(struct perf_cpu_map *cpus, struct perf_cpu_map **res,
		       int (*f)(struct perf_cpu_map *map, int cpu, void *data),
		       void *data);

int cpu_map__cpu(struct perf_cpu_map *cpus, int idx);
bool cpu_map__has(struct perf_cpu_map *cpus, int cpu);

#endif /* __PERF_CPUMAP_H */
