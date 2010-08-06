/* For debugging general purposes */
#ifndef __PERF_DEBUG_H
#define __PERF_DEBUG_H

#include <stdbool.h>
#include "event.h"

extern int verbose;
extern bool dump_trace;

int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(event_t *event);

struct ui_progress;

#ifdef NO_NEWT_SUPPORT
static inline int browser__show_help(const char *format __used, va_list ap __used)
{
	return 0;
}

static inline struct ui_progress *ui_progress__new(const char *title __used,
						   u64 total __used)
{
	return (struct ui_progress *)1;
}

static inline void ui_progress__update(struct ui_progress *self __used,
				       u64 curr __used) {}

static inline void ui_progress__delete(struct ui_progress *self __used) {}
#else
int browser__show_help(const char *format, va_list ap);
struct ui_progress *ui_progress__new(const char *title, u64 total);
void ui_progress__update(struct ui_progress *self, u64 curr);
void ui_progress__delete(struct ui_progress *self);
#endif

#endif	/* __PERF_DEBUG_H */
