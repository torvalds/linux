// SPDX-License-Identifier: GPL-2.0
#include "util/map_symbol.h"
#include "util/mem-events.h"
#include "mem-events.h"

#define E(t, n, s, l, a) { .tag = t, .name = n, .event_name = s, .ldlat = l, .aux_event = a }

struct perf_mem_event perf_mem_events_arm[PERF_MEM_EVENTS__MAX] = {
	E("spe-load",	"%s/ts_enable=1,pa_enable=1,load_filter=1,store_filter=0,min_latency=%u/",	NULL,	true,	0),
	E("spe-store",	"%s/ts_enable=1,pa_enable=1,load_filter=0,store_filter=1/",			NULL,	false,	0),
	E("spe-ldst",	"%s/ts_enable=1,pa_enable=1,load_filter=1,store_filter=1,min_latency=%u/",	NULL,	true,	0),
};
