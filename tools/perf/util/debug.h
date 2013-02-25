/* For debugging general purposes */
#ifndef __PERF_DEBUG_H
#define __PERF_DEBUG_H

#include <stdbool.h>
#include "event.h"
#include "../ui/helpline.h"

extern int verbose;
extern bool quiet, dump_trace;

int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(union perf_event *event);

struct ui_progress;
struct perf_error_ops;

#if defined(NEWT_SUPPORT) || defined(GTK2_SUPPORT)

#include "../ui/progress.h"
int ui__error(const char *format, ...) __attribute__((format(printf, 1, 2)));
#include "../ui/util.h"

#else

static inline void ui_progress__update(u64 curr __maybe_unused,
				       u64 total __maybe_unused,
				       const char *title __maybe_unused) {}
static inline void ui_progress__finish(void) {}

#define ui__error(format, arg...) ui__warning(format, ##arg)

static inline int
perf_error__register(struct perf_error_ops *eops __maybe_unused)
{
	return 0;
}

static inline int
perf_error__unregister(struct perf_error_ops *eops __maybe_unused)
{
	return 0;
}

#endif /* NEWT_SUPPORT || GTK2_SUPPORT */

int ui__warning(const char *format, ...) __attribute__((format(printf, 1, 2)));
int ui__error_paranoid(void);

#endif	/* __PERF_DEBUG_H */
