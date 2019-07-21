// SPDX-License-Identifier: GPL-2.0-only
#include <perf/cpumap.h>
#include <stdlib.h>
#include <linux/refcount.h>
#include <internal/cpumap.h>
#include <asm/bug.h>
#include <stdio.h>

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

static void cpu_map__delete(struct perf_cpu_map *map)
{
	if (map) {
		WARN_ONCE(refcount_read(&map->refcnt) != 0,
			  "cpu_map refcnt unbalanced\n");
		free(map);
	}
}

struct perf_cpu_map *perf_cpu_map__get(struct perf_cpu_map *map)
{
	if (map)
		refcount_inc(&map->refcnt);
	return map;
}

void perf_cpu_map__put(struct perf_cpu_map *map)
{
	if (map && refcount_dec_and_test(&map->refcnt))
		cpu_map__delete(map);
}
