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

static int compare_topdown_event(void *vstate, struct pmu_event_info *info)
{
	int *config = vstate;
	int event = 0;
	int umask = 0;
	char *str;

	if (!strcasestr(info->name, "topdown"))
		return 0;

	str = strcasestr(info->str, "event=");
	if (str)
		sscanf(str, "event=%x", &event);

	str = strcasestr(info->str, "umask=");
	if (str)
		sscanf(str, "umask=%x", &umask);

	if (event == 0 && *config == (event | umask << 8))
		return 1;

	return 0;
}

bool arch_is_topdown_metrics(const struct evsel *evsel)
{
	struct perf_pmu *pmu = evsel__find_pmu(evsel);
	int config = evsel->core.attr.config;

	if (!pmu || !pmu->is_core)
		return false;

	if (perf_pmu__for_each_event(pmu, false, &config,
				     compare_topdown_event))
		return true;

	return false;
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
			return false;
		if (evsel != leader && arch_is_topdown_metrics(evsel))
			return true;
	}

	return false;
}
