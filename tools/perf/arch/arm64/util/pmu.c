// SPDX-License-Identifier: GPL-2.0

#include <internal/cpumap.h>
#include "../../../util/cpumap.h"
#include "../../../util/pmu.h"

const struct pmu_events_table *pmu_events_table__find(void)
{
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmu__scan(pmu))) {
		if (!is_pmu_core(pmu->name))
			continue;

		/*
		 * The cpumap should cover all CPUs. Otherwise, some CPUs may
		 * not support some events or have different event IDs.
		 */
		if (pmu->cpus->nr != cpu__max_cpu().cpu)
			return NULL;

		return perf_pmu__find_table(pmu);
	}

	return NULL;
}
