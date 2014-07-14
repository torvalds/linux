/* For debugging general purposes */
#ifndef __PERF_DEBUG_H
#define __PERF_DEBUG_H

#include <stdbool.h>
#include "event.h"
#include "../ui/helpline.h"
#include "../ui/progress.h"
#include "../ui/util.h"

extern int verbose;
extern bool quiet, dump_trace;

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_err(fmt, ...) \
	eprintf(0, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning(fmt, ...) \
	eprintf(0, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	eprintf(0, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug(fmt, ...) \
	eprintf(1, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debugN(n, fmt, ...) \
	eprintf(n, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug2(fmt, ...) pr_debugN(2, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug3(fmt, ...) pr_debugN(3, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_debug4(fmt, ...) pr_debugN(4, pr_fmt(fmt), ##__VA_ARGS__)

int dump_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void trace_event(union perf_event *event);

int ui__error(const char *format, ...) __attribute__((format(printf, 1, 2)));
int ui__warning(const char *format, ...) __attribute__((format(printf, 1, 2)));

void pr_stat(const char *fmt, ...);

int eprintf(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif	/* __PERF_DEBUG_H */
