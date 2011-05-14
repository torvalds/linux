/* For debugging general purposes */
#ifndef __PERF_DEBUG_H
#define __PERF_DEBUG_H

#include <stdbool.h>
#include "event.h"

extern int verbose;
extern bool quiet, dump_trace;

int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(union perf_event *event);

struct ui_progress;

#ifdef NO_NEWT_SUPPORT
static inline int ui_helpline__show_help(const char *format __used, va_list ap __used)
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
extern char ui_helpline__last_msg[];
int ui_helpline__show_help(const char *format, va_list ap);
#include "ui/progress.h"
#endif

void ui__warning(const char *format, ...) __attribute__((format(printf, 1, 2)));
void ui__warning_paranoid(void);

#endif	/* __PERF_DEBUG_H */
