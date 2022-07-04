/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_CPUMAP_H
#define __LIBPERF_CPUMAP_H

#include <perf/core.h>
#include <perf/cpumap.h>
#include <stdio.h>
#include <stdbool.h>

/** A wrapper around a CPU to avoid confusion with the perf_cpu_map's map's indices. */
struct perf_cpu {
	int cpu;
};

LIBPERF_API struct perf_cpu_map *perf_cpu_map__dummy_new(void);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__default_new(void);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__new(const char *cpu_list);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__read(FILE *file);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__merge(struct perf_cpu_map *orig,
						     struct perf_cpu_map *other);
LIBPERF_API void perf_cpu_map__put(struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu perf_cpu_map__cpu(const struct perf_cpu_map *cpus, int idx);
LIBPERF_API int perf_cpu_map__nr(const struct perf_cpu_map *cpus);
LIBPERF_API bool perf_cpu_map__empty(const struct perf_cpu_map *map);
LIBPERF_API struct perf_cpu perf_cpu_map__max(struct perf_cpu_map *map);
LIBPERF_API bool perf_cpu_map__has(const struct perf_cpu_map *map, struct perf_cpu cpu);

#define perf_cpu_map__for_each_cpu(cpu, idx, cpus)		\
	for ((idx) = 0, (cpu) = perf_cpu_map__cpu(cpus, idx);	\
	     (idx) < perf_cpu_map__nr(cpus);			\
	     (idx)++, (cpu) = perf_cpu_map__cpu(cpus, idx))

#define perf_cpu_map__for_each_idx(idx, cpus)				\
	for ((idx) = 0; (idx) < perf_cpu_map__nr(cpus); (idx)++)

#endif /* __LIBPERF_CPUMAP_H */
