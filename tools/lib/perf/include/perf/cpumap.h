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
 * perf_cpu_map__new_any_cpu - a map with a singular "any CPU"/dummy -1 value.
 */
LIBPERF_API struct perf_cpu_map *perf_cpu_map__new_any_cpu(void);
/**
 * perf_cpu_map__new_online_cpus - a map read from
 *                                 /sys/devices/system/cpu/online if
 *                                 available. If reading wasn't possible a map
 *                                 is created using the online processors
 *                                 assuming the first 'n' processors are all
 *                                 online.
 */
LIBPERF_API struct perf_cpu_map *perf_cpu_map__new_online_cpus(void);
/**
 * perf_cpu_map__new - create a map from the given cpu_list such as "0-7". If no
 *                     cpu_list argument is provided then
 *                     perf_cpu_map__new_online_cpus is returned.
 */
LIBPERF_API struct perf_cpu_map *perf_cpu_map__new(const char *cpu_list);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__read(FILE *file);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__merge(struct perf_cpu_map *orig,
						     struct perf_cpu_map *other);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__intersect(struct perf_cpu_map *orig,
							 struct perf_cpu_map *other);
LIBPERF_API void perf_cpu_map__put(struct perf_cpu_map *map);
/**
 * perf_cpu_map__cpu - get the CPU value at the given index. Returns -1 if index
 *                     is invalid.
 */
LIBPERF_API struct perf_cpu perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx);
/**
 * perf_cpu_map__nr - for an empty map returns 1, as perf_cpu_map__cpu returns a
 *                    cpu of -1 for an invalid index, this makes an empty map
 *                    look like it contains the "any CPU"/dummy value. Otherwise
 *                    the result is the number CPUs in the map plus one if the
 *                    "any CPU"/dummy value is present.
 */
LIBPERF_API int perf_cpu_map__nr(const struct perf_cpu_map *cpus);
/**
 * perf_cpu_map__has_any_cpu_or_is_empty - is map either empty or has the "any CPU"/dummy value.
 */
LIBPERF_API bool perf_cpu_map__has_any_cpu_or_is_empty(const struct perf_cpu_map *map);
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

#define perf_cpu_map__for_each_cpu_skip_any(_cpu, idx, cpus)	\
	for ((idx) = 0, (_cpu) = perf_cpu_map__cpu(cpus, idx);	\
	     (idx) < perf_cpu_map__nr(cpus);			\
	     (idx)++, (_cpu) = perf_cpu_map__cpu(cpus, idx))	\
		if ((_cpu).cpu != -1)

#define perf_cpu_map__for_each_idx(idx, cpus)				\
	for ((idx) = 0; (idx) < perf_cpu_map__nr(cpus); (idx)++)

#endif /* __LIBPERF_CPUMAP_H */
