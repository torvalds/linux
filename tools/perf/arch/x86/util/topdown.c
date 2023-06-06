// SPDX-License-Identifier: GPL-2.0
#include "api/fs/fs.h"
#include "util/evsel.h"
#include "util/pmu.h"
#include "util/topdown.h"
#include "topdown.h"
#include "evsel.h"

/* Check whether there is a PMU which supports the perf metrics. */
bool topdown_sys_has_perf_metrics(void)
{
	static bool has_perf_metrics;
	static bool cached;
	struct perf_pmu *pmu;

	if (cached)
		return has_perf_metrics;

	/*
	 * The perf metrics feature is a core PMU feature.
	 * The PERF_TYPE_RAW type is the type of a core PMU.
	 * The slots event is only available when the core PMU
	 * supports the perf metrics feature.
	 */
	pmu = perf_pmu__find_by_type(PERF_TYPE_RAW);
	if (pmu && pmu_have_event(pmu->name, "slots"))
		has_perf_metrics = true;

	cached = true;
	return has_perf_metrics;
}

#define TOPDOWN_SLOTS		0x0400

/*
 * Check whether a topdown group supports sample-read.
 *
 * Only Topdown metric supports sample-read. The slots
 * event must be the leader of the topdown group.
 */
bool arch_topdown_sample_read(struct evsel *leader)
{
	if (!evsel__sys_has_perf_metrics(leader))
		return false;

	if (leader->core.attr.config == TOPDOWN_SLOTS)
		return true;

	return false;
}
