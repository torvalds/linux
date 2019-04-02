/* SPDX-License-Identifier: GPL-2.0 */
/* For deging general purposes */
#ifndef __PERF_DE_H
#define __PERF_DE_H

#include <stdbool.h>
#include <string.h>
#include <linux/compiler.h>
#include "event.h"
#include "../ui/helpline.h"
#include "../ui/progress.h"
#include "../ui/util.h"

extern int verbose;
extern bool quiet, dump_trace;
extern int de_ordered_events;
extern int de_data_convert;

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define pr_err(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_warning(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_info(fmt, ...) \
	eprintf(0, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_de(fmt, ...) \
	eprintf(1, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_deN(n, fmt, ...) \
	eprintf(n, verbose, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_de2(fmt, ...) pr_deN(2, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_de3(fmt, ...) pr_deN(3, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_de4(fmt, ...) pr_deN(4, pr_fmt(fmt), ##__VA_ARGS__)

#define pr_time_N(n, var, t, fmt, ...) \
	eprintf_time(n, var, t, fmt, ##__VA_ARGS__)

#define pr_oe_time(t, fmt, ...)  pr_time_N(1, de_ordered_events, t, pr_fmt(fmt), ##__VA_ARGS__)
#define pr_oe_time2(t, fmt, ...) pr_time_N(2, de_ordered_events, t, pr_fmt(fmt), ##__VA_ARGS__)

#define STRERR_BUFSIZE	128	/* For the buffer size of str_error_r */

int dump_printf(const char *fmt, ...) __printf(1, 2);
void trace_event(union perf_event *event);

int ui__error(const char *format, ...) __printf(1, 2);
int ui__warning(const char *format, ...) __printf(1, 2);

void pr_stat(const char *fmt, ...);

int eprintf(int level, int var, const char *fmt, ...) __printf(3, 4);
int eprintf_time(int level, int var, u64 t, const char *fmt, ...) __printf(4, 5);
int veprintf(int level, int var, const char *fmt, va_list args);

int perf_de_option(const char *str);
void perf_de_setup(void);
int perf_quiet_option(void);

void dump_stack(void);
void sighandler_dump_stack(int sig);

#endif	/* __PERF_DE_H */
