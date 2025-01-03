// SPDX-License-Identifier: GPL-2.0

#include "../../../util/pmu.h"
#include "../../../util/pmus.h"
#include "../../../util/tool_pmu.h"
#include <api/fs/fs.h>

u64 tool_pmu__cpu_slots_per_cycle(void)
{
	char path[PATH_MAX];
	unsigned long long slots = 0;
	struct perf_pmu *pmu = perf_pmus__find_core_pmu();

	if (pmu) {
		perf_pmu__pathname_scnprintf(path, sizeof(path),
					     pmu->name, "caps/slots");
		/*
		 * The value of slots is not greater than 32 bits, but
		 * filename__read_int can't read value with 0x prefix,
		 * so use filename__read_ull instead.
		 */
		filename__read_ull(path, &slots);
	}

	return slots;
}
