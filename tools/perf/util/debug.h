/* For debugging general purposes */
#ifndef __PERF_DEBUG_H
#define __PERF_DEBUG_H

#include "event.h"

extern int verbose;
extern int dump_trace;

int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(event_t *event);
int browser__show_help(const char *format, va_list ap);

#endif	/* __PERF_DEBUG_H */
