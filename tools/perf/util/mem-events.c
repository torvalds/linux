#include "mem-events.h"

#define E(n) { .name = n }

struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX] = {
	E("cpu/mem-loads,ldlat=30/P"),
	E("cpu/mem-stores/P"),
};

#undef E
