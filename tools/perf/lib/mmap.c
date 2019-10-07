// SPDX-License-Identifier: GPL-2.0
#include <sys/mman.h>
#include <internal/mmap.h>
#include <internal/lib.h>

void perf_mmap__init(struct perf_mmap *map, bool overwrite)
{
	map->fd = -1;
	map->overwrite = overwrite;
	refcount_set(&map->refcnt, 0);
}

size_t perf_mmap__mmap_len(struct perf_mmap *map)
{
	return map->mask + 1 + page_size;
}

int perf_mmap__mmap(struct perf_mmap *map, struct perf_mmap_param *mp,
		    int fd, int cpu)
{
	map->prev = 0;
	map->mask = mp->mask;
	map->base = mmap(NULL, perf_mmap__mmap_len(map), mp->prot,
			 MAP_SHARED, fd, 0);
	if (map->base == MAP_FAILED) {
		map->base = NULL;
		return -1;
	}

	map->fd  = fd;
	map->cpu = cpu;
	return 0;
}
