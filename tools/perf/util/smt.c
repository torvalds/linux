// SPDX-License-Identifier: GPL-2.0-only
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <linux/bitops.h>
#include "api/fs/fs.h"
#include "smt.h"

/**
 * hweight_str - Returns the number of bits set in str. Stops at first non-hex
 *	       or ',' character.
 */
static int hweight_str(char *str)
{
	int result = 0;

	while (*str) {
		switch (*str++) {
		case '0':
		case ',':
			break;
		case '1':
		case '2':
		case '4':
		case '8':
			result++;
			break;
		case '3':
		case '5':
		case '6':
		case '9':
		case 'a':
		case 'A':
		case 'c':
		case 'C':
			result += 2;
			break;
		case '7':
		case 'b':
		case 'B':
		case 'd':
		case 'D':
		case 'e':
		case 'E':
			result += 3;
			break;
		case 'f':
		case 'F':
			result += 4;
			break;
		default:
			goto done;
		}
	}
done:
	return result;
}

int smt_on(void)
{
	static bool cached;
	static int cached_result;
	int cpu;
	int ncpu;

	if (cached)
		return cached_result;

	if (sysfs__read_int("devices/system/cpu/smt/active", &cached_result) >= 0) {
		cached = true;
		return cached_result;
	}

	cached_result = 0;
	ncpu = sysconf(_SC_NPROCESSORS_CONF);
	for (cpu = 0; cpu < ncpu; cpu++) {
		unsigned long long siblings;
		char *str;
		size_t strlen;
		char fn[256];

		snprintf(fn, sizeof fn,
			"devices/system/cpu/cpu%d/topology/thread_siblings", cpu);
		if (sysfs__read_str(fn, &str, &strlen) < 0) {
			snprintf(fn, sizeof fn,
				"devices/system/cpu/cpu%d/topology/core_cpus", cpu);
			if (sysfs__read_str(fn, &str, &strlen) < 0)
				continue;
		}
		/* Entry is hex, but does not have 0x, so need custom parser */
		siblings = hweight_str(str);
		free(str);
		if (siblings > 1) {
			cached_result = 1;
			break;
		}
	}
	cached = true;
	return cached_result;
}
