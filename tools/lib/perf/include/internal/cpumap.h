/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_CPUMAP_H
#define __LIBPERF_INTERNAL_CPUMAP_H

#include <linux/refcount.h>
#include <perf/cpumap.h>

/**
 * A sized, reference counted, sorted array of integers representing CPU
 * numbers. This is commonly used to capture which CPUs a PMU is associated
 * with. The indices into the cpumap are frequently used as they avoid having
 * gaps if CPU numbers were used. For events associated with a pid, rather than
 * a CPU, a single dummy map with an entry of -1 is used.
 */
struct perf_cpu_map {
	refcount_t	refcnt;
	/** Length of the map array. */
	int		nr;
	/** The CPU values. */
	struct perf_cpu	map[];
};

#ifndef MAX_NR_CPUS
#define MAX_NR_CPUS	2048
#endif

int perf_cpu_map__idx(const struct perf_cpu_map *cpus, struct perf_cpu cpu);

#endif /* __LIBPERF_INTERNAL_CPUMAP_H */
