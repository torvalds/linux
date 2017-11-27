/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __PERF_CPUMAP_H
#define __PERF_CPUMAP_H

#include <stdio.h>
#include <stdbool.h>
#include <linux/refcount.h>

#include "perf.h"
#include "util/debug.h"

struct cpu_map {
	refcount_t refcnt;
	int nr;
	int map[];
};

struct cpu_map *cpu_map__new(const char *cpu_list);
struct cpu_map *cpu_map__empty_new(int nr);
struct cpu_map *cpu_map__dummy_new(void);
struct cpu_map *cpu_map__new_data(struct cpu_map_data *data);
struct cpu_map *cpu_map__read(FILE *file);
size_t cpu_map__snprint(struct cpu_map *map, char *buf, size_t size);
size_t cpu_map__snprint_mask(struct cpu_map *map, char *buf, size_t size);
size_t cpu_map__fprintf(struct cpu_map *map, FILE *fp);
int cpu_map__get_socket_id(int cpu);
int cpu_map__get_socket(struct cpu_map *map, int idx, void *data);
int cpu_map__get_core_id(int cpu);
int cpu_map__get_core(struct cpu_map *map, int idx, void *data);
int cpu_map__build_socket_map(struct cpu_map *cpus, struct cpu_map **sockp);
int cpu_map__build_core_map(struct cpu_map *cpus, struct cpu_map **corep);

struct cpu_map *cpu_map__get(struct cpu_map *map);
void cpu_map__put(struct cpu_map *map);

static inline int cpu_map__socket(struct cpu_map *sock, int s)
{
	if (!sock || s > sock->nr || s < 0)
		return 0;
	return sock->map[s];
}

static inline int cpu_map__id_to_socket(int id)
{
	return id >> 16;
}

static inline int cpu_map__id_to_cpu(int id)
{
	return id & 0xffff;
}

static inline int cpu_map__nr(const struct cpu_map *map)
{
	return map ? map->nr : 1;
}

static inline bool cpu_map__empty(const struct cpu_map *map)
{
	return map ? map->map[0] == -1 : true;
}

int cpu__setup_cpunode_map(void);

int cpu__max_node(void);
int cpu__max_cpu(void);
int cpu__max_present_cpu(void);
int cpu__get_node(int cpu);

int cpu_map__build_map(struct cpu_map *cpus, struct cpu_map **res,
		       int (*f)(struct cpu_map *map, int cpu, void *data),
		       void *data);

int cpu_map__cpu(struct cpu_map *cpus, int idx);
bool cpu_map__has(struct cpu_map *cpus, int cpu);
int cpu_map__idx(struct cpu_map *cpus, int cpu);
#endif /* __PERF_CPUMAP_H */
