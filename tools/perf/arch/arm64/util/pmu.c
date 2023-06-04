// SPDX-License-Identifier: GPL-2.0

#include <internal/cpumap.h>
#include "../../../util/cpumap.h"
#include "../../../util/pmu.h"
#include <api/fs/fs.h>
#include <math.h>

static struct perf_pmu *pmu__find_core_pmu(void)
{
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmu__scan(pmu))) {
		if (!is_pmu_core(pmu->name))
			continue;

		/*
		 * The cpumap should cover all CPUs. Otherwise, some CPUs may
		 * not support some events or have different event IDs.
		 */
		if (RC_CHK_ACCESS(pmu->cpus)->nr != cpu__max_cpu().cpu)
			return NULL;

		return pmu;
	}
	return NULL;
}

const struct pmu_metrics_table *pmu_metrics_table__find(void)
{
	struct perf_pmu *pmu = pmu__find_core_pmu();

	if (pmu)
		return perf_pmu__find_metrics_table(pmu);

	return NULL;
}

const struct pmu_events_table *pmu_events_table__find(void)
{
	struct perf_pmu *pmu = pmu__find_core_pmu();

	if (pmu)
		return perf_pmu__find_events_table(pmu);

	return NULL;
}

double perf_pmu__cpu_slots_per_cycle(void)
{
	char path[PATH_MAX];
	unsigned long long slots = 0;
	struct perf_pmu *pmu = pmu__find_core_pmu();

	if (pmu) {
		perf_pmu__pathname_scnprintf(path, sizeof(path),
					     pmu->name, "caps/slots");
		/*
		 * The value of slots is not greater than 32 bits, but sysfs__read_int
		 * can't read value with 0x prefix, so use sysfs__read_ull instead.
		 */
		sysfs__read_ull(path, &slots);
	}

	return slots ? (double)slots : NAN;
}
