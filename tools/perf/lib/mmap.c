// SPDX-License-Identifier: GPL-2.0
#include <internal/mmap.h>

void perf_mmap__init(struct perf_mmap *map, bool overwrite)
{
	map->fd = -1;
	map->overwrite = overwrite;
	refcount_set(&map->refcnt, 0);
}
