#ifndef __PERF_MEM_EVENTS_H
#define __PERF_MEM_EVENTS_H

#include <stdbool.h>

struct perf_mem_event {
	bool		record;
	bool		supported;
	const char	*tag;
	const char	*name;
	const char	*sysfs_name;
};

enum {
	PERF_MEM_EVENTS__LOAD,
	PERF_MEM_EVENTS__STORE,
	PERF_MEM_EVENTS__MAX,
};

extern struct perf_mem_event perf_mem_events[PERF_MEM_EVENTS__MAX];

int perf_mem_events__parse(const char *str);
int perf_mem_events__init(void);

char *perf_mem_events__name(int i);
#endif /* __PERF_MEM_EVENTS_H */
