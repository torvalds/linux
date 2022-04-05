// SPDX-License-Identifier: GPL-2.0-only

#define __printf(a, b)  __attribute__((format(printf, a, b)))

#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <perf/core.h>
#include <internal/lib.h>
#include "internal.h"

static int __base_pr(enum libperf_print_level level, const char *format,
		     va_list args)
{
	return vfprintf(stderr, format, args);
}

static libperf_print_fn_t __libperf_pr = __base_pr;

__printf(2, 3)
void libperf_print(enum libperf_print_level level, const char *format, ...)
{
	va_list args;

	if (!__libperf_pr)
		return;

	va_start(args, format);
	__libperf_pr(level, format, args);
	va_end(args);
}

void libperf_init(libperf_print_fn_t fn)
{
	page_size = sysconf(_SC_PAGE_SIZE);
	__libperf_pr = fn;
}
