/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_CPUMAP_H
#define __LIBPERF_CPUMAP_H

#include <perf/core.h>
#include <stdio.h>
#include <stdbool.h>

/** A wrapper around a CPU to avoid confusion with the perf_cpu_map's map's indices. */
struct perf_cpu {
	int cpu;
};

struct perf_cache {
	int cache_lvl;
	int cache;
};

struct perf_cpu_map;

/**
 * perf_cpu_map__dummy_new - a map with a singular "any CPU"/dummy -1 value.
 */
LIBPERF_API struct perf_cpu_map *perf_cpu_map__dummy_new(void);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__default_new(void);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__new(const char *cpu_list);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__read(FILE *file);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__merge(struct perf_cpu_map *orig,
						     struct perf_cpu_map *other);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__intersect(struct perf_cpu_map *orig,
							 struct perf_cpu_map *other);
LIBPERF_API void perf_cpu_map__put(struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx);
LIBPERF_API int perf_cpu_map__nr(const struct perf_cpu_map *cpus);
/**
 * perf_cpu_map__empty - is map either empty or the "any CPU"/dummy value.
 */
LIBPERF_API bool perf_cpu_map__empty(const struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu perf_cpu_map__max(const struct perf_cpu_map *map);
LIBPERF_API bool perf_cpu_map__has(const struct perf_cpu_map *map, struct perf_cpu cpu);
LIBPERF_API bool perf_cpu_map__equal(const struct perf_cpu_map *lhs,
				     const struct perf_cpu_map *rhs);
/**
 * perf_cpu_map__any_cpu - Does the map contain the "any CPU"/dummy -1 value?
 */
LIBPERF_API bool perf_cpu_map__has_any_cpu(const struct perf_cpu_map *map);

#define perf_cpu_map__for_each_cpu(cpu, idx, cpus)		\
	for ((idx) = 0, (cpu) = perf_cpu_map__cpu(cpus, idx);	\
	     (idx) < perf_cpu_map__nr(cpus);			\
	     (idx)++, (cpu) = perf_cpu_map__cpu(cpus, idx))

#define perf_cpu_map__for_each_idx(idx, cpus)				\
	for ((idx) = 0; (idx) < perf_cpu_map__nr(cpus); (idx)++)

#endif /* __LIBPERF_CPUMAP_H */
