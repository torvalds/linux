// SPDX-License-Identifier: GPL-2.0
#include "cacheline.h"
#include <unistd.h>

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
#define cache_line_size(cacheline_sizep) *cacheline_sizep = sysconf(_SC_LEVEL1_DCACHE_LINESIZE)
#else
#include <api/fs/fs.h>
#include "debug.h"
static void cache_line_size(int *cacheline_sizep)
{
	if (sysfs__read_int("devices/system/cpu/cpu0/cache/index0/coherency_line_size", cacheline_sizep))
		pr_debug("cannot determine cache line size");
}
#endif

int cacheline_size(void)
{
	static int size;

	if (!size)
		cache_line_size(&size);

	return size;
}
