// SPDX-License-Identifier: GPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <perf/threadmap.h>
#include <internal/tests.h>
#include "tests.h"

static int libperf_print(enum libperf_print_level level,
			 const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

int test_threadmap(int argc, char **argv)
{
	struct perf_thread_map *threads;

	__T_START;

	libperf_init(libperf_print);

	threads = perf_thread_map__new_dummy();
	if (!threads)
		return -1;

	perf_thread_map__get(threads);
	perf_thread_map__put(threads);
	perf_thread_map__put(threads);

	__T_END;
	return tests_failed == 0 ? 0 : -1;
}
