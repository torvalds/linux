#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/bitops.h>
#include "api/fs/fs.h"
#include "smt.h"

int smt_on(void)
{
	static bool cached;
	static int cached_result;
	int cpu;
	int ncpu;

	if (cached)
		return cached_result;

	ncpu = sysconf(_SC_NPROCESSORS_CONF);
	for (cpu = 0; cpu < ncpu; cpu++) {
		unsigned long long siblings;
		char *str;
		size_t strlen;
		char fn[256];

		snprintf(fn, sizeof fn,
			"devices/system/cpu/cpu%d/topology/core_cpus", cpu);
		if (access(fn, F_OK) == -1) {
			snprintf(fn, sizeof fn,
				"devices/system/cpu/cpu%d/topology/thread_siblings",
				cpu);
		}
		if (sysfs__read_str(fn, &str, &strlen) < 0)
			continue;
		/* Entry is hex, but does not have 0x, so need custom parser */
		siblings = strtoull(str, NULL, 16);
		free(str);
		if (hweight64(siblings) > 1) {
			cached_result = 1;
			cached = true;
			break;
		}
	}
	if (!cached) {
		cached_result = 0;
		cached = true;
	}
	return cached_result;
}
