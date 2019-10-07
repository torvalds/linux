// SPDX-License-Identifier: GPL-2.0
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
