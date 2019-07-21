// SPDX-License-Identifier: GPL-2.0
#include <perf/evsel.h>
#include <linux/list.h>
#include <internal/evsel.h>
#include <linux/zalloc.h>

void perf_evsel__init(struct perf_evsel *evsel, struct perf_event_attr *attr)
{
	INIT_LIST_HEAD(&evsel->node);
	evsel->attr = *attr;
}

struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr)
{
	struct perf_evsel *evsel = zalloc(sizeof(*evsel));

	if (evsel != NULL)
		perf_evsel__init(evsel, attr);

	return evsel;
}
