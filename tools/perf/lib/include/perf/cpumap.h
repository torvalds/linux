/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_CPUMAP_H
#define __LIBPERF_CPUMAP_H

#include <perf/core.h>

struct perf_cpu_map;

LIBPERF_API struct perf_cpu_map *perf_cpu_map__dummy_new(void);

#endif /* __LIBPERF_CPUMAP_H */
