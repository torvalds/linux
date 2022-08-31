// SPDX-License-Identifier: GPL-2.0-only
#include <string.h>
#include "api/fs/fs.h"
#include "cputopo.h"
#include "smt.h"

bool smt_on(const struct cpu_topology *topology)
{
	static bool cached;
	static bool cached_result;
	int fs_value;

	if (cached)
		return cached_result;

	if (sysfs__read_int("devices/system/cpu/smt/active", &fs_value) >= 0)
		cached_result = (fs_value == 1);
	else
		cached_result = cpu_topology__smt_on(topology);

	cached = true;
	return cached_result;
}
