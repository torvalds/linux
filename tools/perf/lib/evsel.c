// SPDX-License-Identifier: GPL-2.0
#include <perf/evsel.h>
#include <linux/list.h>
#include <internal/evsel.h>

void perf_evsel__init(struct perf_evsel *evsel, struct perf_event_attr *attr)
{
	INIT_LIST_HEAD(&evsel->node);
	evsel->attr = *attr;
}
