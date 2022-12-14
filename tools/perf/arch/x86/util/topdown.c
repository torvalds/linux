// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include "api/fs/fs.h"
#include "util/pmu.h"
#include "util/topdown.h"
#include "util/evlist.h"
#include "util/debug.h"
#include "util/pmu-hybrid.h"
#include "topdown.h"
#include "evsel.h"

#define TOPDOWN_L1_EVENTS       "{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound}"
#define TOPDOWN_L1_EVENTS_CORE  "{slots,cpu_core/topdown-retiring/,cpu_core/topdown-bad-spec/,cpu_core/topdown-fe-bound/,cpu_core/topdown-be-bound/}"
#define TOPDOWN_L2_EVENTS       "{slots,topdown-retiring,topdown-bad-spec,topdown-fe-bound,topdown-be-bound,topdown-heavy-ops,topdown-br-mispredict,topdown-fetch-lat,topdown-mem-bound}"
#define TOPDOWN_L2_EVENTS_CORE  "{slots,cpu_core/topdown-retiring/,cpu_core/topdown-bad-spec/,cpu_core/topdown-fe-bound/,cpu_core/topdown-be-bound/,cpu_core/topdown-heavy-ops/,cpu_core/topdown-br-mispredict/,cpu_core/topdown-fetch-lat/,cpu_core/topdown-mem-bound/}"

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

/*
 * Check whether we can use a group for top down.
 * Without a group may get bad results due to multiplexing.
 */
bool arch_topdown_check_group(bool *warn)
{
	int n;

	if (sysctl__read_int("kernel/nmi_watchdog", &n) < 0)
		return false;
	if (n > 0) {
		*warn = true;
		return false;
	}
	return true;
}

void arch_topdown_group_warn(void)
{
	fprintf(stderr,
		"nmi_watchdog enabled with topdown. May give wrong results.\n"
		"Disable with echo 0 > /proc/sys/kernel/nmi_watchdog\n");
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

const char *arch_get_topdown_pmu_name(struct evlist *evlist, bool warn)
{
	const char *pmu_name;

	if (!perf_pmu__has_hybrid())
		return "cpu";

	if (!evlist->hybrid_pmu_name) {
		if (warn)
			pr_warning("WARNING: default to use cpu_core topdown events\n");
		evlist->hybrid_pmu_name = perf_pmu__hybrid_type_to_pmu("core");
	}

	pmu_name = evlist->hybrid_pmu_name;

	return pmu_name;
}

int topdown_parse_events(struct evlist *evlist)
{
	const char *topdown_events;
	const char *pmu_name;

	if (!topdown_sys_has_perf_metrics())
		return 0;

	pmu_name = arch_get_topdown_pmu_name(evlist, false);

	if (pmu_have_event(pmu_name, "topdown-heavy-ops")) {
		if (!strcmp(pmu_name, "cpu_core"))
			topdown_events = TOPDOWN_L2_EVENTS_CORE;
		else
			topdown_events = TOPDOWN_L2_EVENTS;
	} else {
		if (!strcmp(pmu_name, "cpu_core"))
			topdown_events = TOPDOWN_L1_EVENTS_CORE;
		else
			topdown_events = TOPDOWN_L1_EVENTS;
	}

	return parse_event(evlist, topdown_events);
}
