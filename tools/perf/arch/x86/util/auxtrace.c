/*
 * auxtrace.c: AUX area tracing support
 * Copyright (c) 2013-2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <stdbool.h>

#include "../../util/header.h"
#include "../../util/debug.h"
#include "../../util/pmu.h"
#include "../../util/auxtrace.h"
#include "../../util/intel-pt.h"
#include "../../util/intel-bts.h"
#include "../../util/evlist.h"

static
struct auxtrace_record *auxtrace_record__init_intel(struct perf_evlist *evlist,
						    int *err)
{
	struct perf_pmu *intel_pt_pmu;
	struct perf_pmu *intel_bts_pmu;
	struct perf_evsel *evsel;
	bool found_pt = false;
	bool found_bts = false;

	intel_pt_pmu = perf_pmu__find(INTEL_PT_PMU_NAME);
	intel_bts_pmu = perf_pmu__find(INTEL_BTS_PMU_NAME);

	if (evlist) {
		evlist__for_each_entry(evlist, evsel) {
			if (intel_pt_pmu &&
			    evsel->attr.type == intel_pt_pmu->type)
				found_pt = true;
			if (intel_bts_pmu &&
			    evsel->attr.type == intel_bts_pmu->type)
				found_bts = true;
		}
	}

	if (found_pt && found_bts) {
		pr_err("intel_pt and intel_bts may not be used together\n");
		*err = -EINVAL;
		return NULL;
	}

	if (found_pt)
		return intel_pt_recording_init(err);

	if (found_bts)
		return intel_bts_recording_init(err);

	return NULL;
}

struct auxtrace_record *auxtrace_record__init(struct perf_evlist *evlist,
					      int *err)
{
	char buffer[64];
	int ret;

	*err = 0;

	ret = get_cpuid(buffer, sizeof(buffer));
	if (ret) {
		*err = ret;
		return NULL;
	}

	if (!strncmp(buffer, "GenuineIntel,", 13))
		return auxtrace_record__init_intel(evlist, err);

	return NULL;
}
