// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include "util/evsel.h"
#include "util/env.h"
#include "util/pmu.h"
#include "linux/string.h"
#include "evsel.h"

void arch_evsel__set_sample_weight(struct evsel *evsel)
{
	evsel__set_sample_bit(evsel, WEIGHT_STRUCT);
}

void arch_evsel__fixup_new_cycles(struct perf_event_attr *attr)
{
	struct perf_env env = { .total_mem = 0, } ;

	if (!perf_env__cpuid(&env))
		return;

	/*
	 * On AMD, precise cycles event sampling internally uses IBS pmu.
	 * But IBS does not have filtering capabilities and perf by default
	 * sets exclude_guest = 1. This makes IBS pmu event init fail and
	 * thus perf ends up doing non-precise sampling. Avoid it by clearing
	 * exclude_guest.
	 */
	if (env.cpuid && strstarts(env.cpuid, "AuthenticAMD"))
		attr->exclude_guest = 0;

	free(env.cpuid);
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
	    pmu_have_event(pmu_name, "slots"))
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
