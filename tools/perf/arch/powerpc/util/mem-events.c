// SPDX-License-Identifier: GPL-2.0
#include "util/map_symbol.h"
#include "util/mem-events.h"
#include "mem-events.h"

#define E(t, n, s, l, a) { .tag = t, .name = n, .event_name = s, .ldlat = l, .aux_event = a }

struct perf_mem_event perf_mem_events_power[PERF_MEM_EVENTS__MAX] = {
	E("ldlat-loads",	"%s/mem-loads/",	"mem-loads",	false,	0),
	E("ldlat-stores",	"%s/mem-stores/",	"mem-stores",	false,	0),
	E(NULL,			NULL,			NULL,		false,	0),
};
