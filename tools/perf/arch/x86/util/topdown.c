// SPDX-License-Identifier: GPL-2.0
#include "api/fs/fs.h"
#include "util/evsel.h"
#include "util/evlist.h"
#include "util/pmu.h"
#include "util/pmus.h"
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
	pmu = perf_pmus__find_by_type(PERF_TYPE_RAW);
	if (pmu && perf_pmu__have_event(pmu, "slots"))
		has_perf_metrics = true;

	cached = true;
	return has_perf_metrics;
}

#define TOPDOWN_SLOTS		0x0400
bool arch_is_topdown_slots(const struct evsel *evsel)
{
	if (evsel->core.attr.config == TOPDOWN_SLOTS)
		return true;

	return false;
}

bool arch_is_topdown_metrics(const struct evsel *evsel)
{
	int config = evsel->core.attr.config;
	const char *name_from_config;
	struct perf_pmu *pmu;

	/* All topdown events have an event code of 0. */
	if ((config & 0xFF) != 0)
		return false;

	pmu = evsel__find_pmu(evsel);
	if (!pmu || !pmu->is_core)
		return false;

	name_from_config = perf_pmu__name_from_config(pmu, config);
	return name_from_config && strcasestr(name_from_config, "topdown");
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
