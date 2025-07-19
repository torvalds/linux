// SPDX-License-Identifier: GPL-2.0
#include "util/evlist.h"
#include "util/pmu.h"
#include "util/pmus.h"
#include "util/topdown.h"
#include "topdown.h"
#include "evsel.h"

// cmask=0, inv=0, pc=0, edge=0, umask=4, event=0
#define TOPDOWN_SLOTS		0x0400

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
	pmu = perf_pmus__find_by_type(PERF_TYPE_RAW);
	if (pmu && perf_pmu__have_event(pmu, "slots"))
		has_perf_metrics = true;

	cached = true;
	return has_perf_metrics;
}

bool arch_is_topdown_slots(const struct evsel *evsel)
{
	return evsel->core.attr.type == PERF_TYPE_RAW &&
	       evsel->core.attr.config == TOPDOWN_SLOTS &&
	       evsel->core.attr.config1 == 0;
}

bool arch_is_topdown_metrics(const struct evsel *evsel)
{
	// cmask=0, inv=0, pc=0, edge=0, umask=0x80-0x87, event=0
	return evsel->core.attr.type == PERF_TYPE_RAW &&
		(evsel->core.attr.config & 0xFFFFF8FF) == 0x8000 &&
		evsel->core.attr.config1 == 0;
}

/*
 * Check whether a topdown group supports sample-read.
 *
 * Only Topdown metric supports sample-read. The slots
 * event must be the leader of the topdown group.
 */
bool arch_topdown_sample_read(struct evsel *leader)
{
	struct evsel *evsel;

	if (!evsel__sys_has_perf_metrics(leader))
		return false;

	if (!arch_is_topdown_slots(leader))
		return false;

	/*
	 * If slots event as leader event but no topdown metric events
	 * in group, slots event should still sample as leader.
	 */
	evlist__for_each_entry(leader->evlist, evsel) {
		if (evsel->core.leader != leader->core.leader)
			continue;
		if (evsel != leader && arch_is_topdown_metrics(evsel))
			return true;
	}

	return false;
}
