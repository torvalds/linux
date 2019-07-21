/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_CPUMAP_H
#define __LIBPERF_CPUMAP_H

#include <perf/core.h>
#include <stdio.h>

struct perf_cpu_map;

LIBPERF_API struct perf_cpu_map *perf_cpu_map__dummy_new(void);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__new(const char *cpu_list);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__read(FILE *file);
LIBPERF_API struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map);
LIBPERF_API void perf_cpu_map__put(struct perf_cpu_map *map);

#endif /* __LIBPERF_CPUMAP_H */
