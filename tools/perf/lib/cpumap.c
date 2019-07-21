// SPDX-License-Identifier: GPL-2.0-only
#include <perf/cpumap.h>
#include <stdlib.h>
#include <linux/refcount.h>
#include <internal/cpumap.h>

struct perf_cpu_map *perf_cpu_map__dummy_new(void)
{
	struct perf_cpu_map *cpus = malloc(sizeof(*cpus) + sizeof(int));

	if (cpus != NULL) {
		cpus->nr = 1;
		cpus->map[0] = -1;
		refcount_set(&cpus->refcnt, 1);
	}

	return cpus;
}
