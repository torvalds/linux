// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include "util/evsel.h"
#include "util/env.h"
#include "linux/string.h"

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
