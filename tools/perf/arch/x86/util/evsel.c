// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include "util/evsel.h"
#include "util/env.h"
#include "util/pmu.h"
#include "util/pmus.h"
#include "linux/string.h"
#include "evsel.h"
#include "util/debug.h"
#include "env.h"

#define IBS_FETCH_L3MISSONLY   (1ULL << 59)
#define IBS_OP_L3MISSONLY      (1ULL << 16)

void arch_evsel__set_sample_weight(struct evsel *evsel)
{
	evsel__set_sample_bit(evsel, WEIGHT_STRUCT);
}

/* Check whether the evsel's PMU supports the perf metrics */
bool evsel__sys_has_perf_metrics(const struct evsel *evsel)
{
	const char *pmu_name = evsel->pmu_name ? evsel->pmu_name : "cpu";

	/*
	 * The PERF_TYPE_RAW type is the core PMU type, e.g., "cpu" PMU
	 * on a non-hybrid machine, "cpu_core" PMU on a hybrid machine.
	 * The slots event is only available for the core PMU, which
	 * supports the perf metrics feature.
	 * Checking both the PERF_TYPE_RAW type and the slots event
	 * should be good enough to detect the perf metrics feature.
	 */
	if ((evsel->core.attr.type == PERF_TYPE_RAW) &&
	    perf_pmus__have_event(pmu_name, "slots"))
		return true;

	return false;
}

bool arch_evsel__must_be_in_group(const struct evsel *evsel)
{
	if (!evsel__sys_has_perf_metrics(evsel))
		return false;

	return evsel->name &&
		(strcasestr(evsel->name, "slots") ||
		 strcasestr(evsel->name, "topdown"));
}

int arch_evsel__hw_name(struct evsel *evsel, char *bf, size_t size)
{
	u64 event = evsel->core.attr.config & PERF_HW_EVENT_MASK;
	u64 pmu = evsel->core.attr.config >> PERF_PMU_TYPE_SHIFT;
	const char *event_name;

	if (event < PERF_COUNT_HW_MAX && evsel__hw_names[event])
		event_name = evsel__hw_names[event];
	else
		event_name = "unknown-hardware";

	/* The PMU type is not required for the non-hybrid platform. */
	if (!pmu)
		return  scnprintf(bf, size, "%s", event_name);

	return scnprintf(bf, size, "%s/%s/",
			 evsel->pmu_name ? evsel->pmu_name : "cpu",
			 event_name);
}

static void ibs_l3miss_warn(void)
{
	pr_warning(
"WARNING: Hw internally resets sampling period when L3 Miss Filtering is enabled\n"
"and tagged operation does not cause L3 Miss. This causes sampling period skew.\n");
}

void arch__post_evsel_config(struct evsel *evsel, struct perf_event_attr *attr)
{
	struct perf_pmu *evsel_pmu, *ibs_fetch_pmu, *ibs_op_pmu;
	static int warned_once;

	if (warned_once || !x86__is_amd_cpu())
		return;

	evsel_pmu = evsel__find_pmu(evsel);
	if (!evsel_pmu)
		return;

	ibs_fetch_pmu = perf_pmus__find("ibs_fetch");
	ibs_op_pmu = perf_pmus__find("ibs_op");

	if (ibs_fetch_pmu && ibs_fetch_pmu->type == evsel_pmu->type) {
		if (attr->config & IBS_FETCH_L3MISSONLY) {
			ibs_l3miss_warn();
			warned_once = 1;
		}
	} else if (ibs_op_pmu && ibs_op_pmu->type == evsel_pmu->type) {
		if (attr->config & IBS_OP_L3MISSONLY) {
			ibs_l3miss_warn();
			warned_once = 1;
		}
	}
}
