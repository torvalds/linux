// SPDX-License-Identifier: GPL-2.0
#include "util/pmu.h"
#include "util/pmus.h"
#include "util/env.h"
#include "map_symbol.h"
#include "mem-events.h"
#include "linux/string.h"
#include "env.h"

#define MEM_LOADS_AUX		0x8203

#define E(t, n, s, l, a) { .tag = t, .name = n, .event_name = s, .ldlat = l, .aux_event = a }

struct perf_mem_event perf_mem_events_intel[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"%s/mem-loads,ldlat=%u/P",	"mem-loads",	true,	0),
	E("ldlat-stores",	"%s/mem-stores/P",		"mem-stores",	false,	0),
	E(NULL,			NULL,				NULL,		false,	0),
};

struct perf_mem_event perf_mem_events_intel_aux[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"{%s/mem-loads-aux/,%s/mem-loads,ldlat=%u/}:P",	"mem-loads",	true,	MEM_LOADS_AUX),
	E("ldlat-stores",	"%s/mem-stores/P",		"mem-stores",	false,	0),
	E(NULL,			NULL,				NULL,		false,	0),
};

struct perf_mem_event perf_mem_events_amd[PERF_MEM_EVENTS__MAX] = {
	E(NULL,		NULL,		NULL,	false,	0),
	E(NULL,		NULL,		NULL,	false,	0),
	E("mem-ldst",	"%s//",		NULL,	false,	0),
};

bool is_mem_loads_aux_event(struct evsel *leader)
{
	struct perf_pmu *pmu = perf_pmus__find("cpu");

	if (!pmu)
		pmu = perf_pmus__find("cpu_core");

	if (pmu && !perf_pmu__have_event(pmu, "mem-loads-aux"))
		return false;

	return leader->core.attr.config == MEM_LOADS_AUX;
}
