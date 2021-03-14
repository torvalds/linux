// SPDX-License-Identifier: GPL-2.0
#include "util/pmu.h"
#include "map_symbol.h"
#include "mem-events.h"

static char mem_loads_name[100];
static bool mem_loads_name__init;

#define MEM_LOADS_AUX		0x8203
#define MEM_LOADS_AUX_NAME	"{cpu/mem-loads-aux/,cpu/mem-loads,ldlat=%u/pp}:S"

bool is_mem_loads_aux_event(struct evsel *leader)
{
	if (!pmu_have_event("cpu", "mem-loads-aux"))
		return false;

	return leader->core.attr.config == MEM_LOADS_AUX;
}

char *perf_mem_events__name(int i)
{
	struct perf_mem_event *e = perf_mem_events__ptr(i);

	if (!e)
		return NULL;

	if (i == PERF_MEM_EVENTS__LOAD) {
		if (mem_loads_name__init)
			return mem_loads_name;

		mem_loads_name__init = true;

		if (pmu_have_event("cpu", "mem-loads-aux")) {
			scnprintf(mem_loads_name, sizeof(mem_loads_name),
				  MEM_LOADS_AUX_NAME, perf_mem_events__loads_ldlat);
		} else {
			scnprintf(mem_loads_name, sizeof(mem_loads_name),
				  e->name, perf_mem_events__loads_ldlat);
		}
		return mem_loads_name;
	}

	return (char *)e->name;
}
