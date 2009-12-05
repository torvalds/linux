/* For debugging general purposes */
#ifndef __PERF_DEBUG_H
#define __PERF_DEBUG_H

#include "event.h"

extern int verbose;
extern int dump_trace;

int eprintf(int level,
	    const char *fmt, ...) __attribute__((format(printf, 2, 3)));
int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(event_t *event);

#endif	/* __PERF_DEBUG_H */
