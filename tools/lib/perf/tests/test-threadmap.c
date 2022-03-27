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

static int test_threadmap_array(int nr, pid_t *array)
{
	struct perf_thread_map *threads;
	int i;

	threads = perf_thread_map__new_array(nr, array);
	__T("Failed to allocate new thread map", threads);

	__T("Unexpected number of threads", perf_thread_map__nr(threads) == nr);

	for (i = 0; i < nr; i++) {
		__T("Unexpected initial value of thread",
		    perf_thread_map__pid(threads, i) == (array ? array[i] : -1));
	}

	for (i = 1; i < nr; i++)
		perf_thread_map__set_pid(threads, i, i * 100);

	__T("Unexpected value of thread 0",
	    perf_thread_map__pid(threads, 0) == (array ? array[0] : -1));

	for (i = 1; i < nr; i++) {
		__T("Unexpected thread value",
		    perf_thread_map__pid(threads, i) == i * 100);
	}

	perf_thread_map__put(threads);

	return 0;
}

#define THREADS_NR	10
int test_threadmap(int argc, char **argv)
{
	struct perf_thread_map *threads;
	pid_t thr_array[THREADS_NR];
	int i;

	__T_START;

	libperf_init(libperf_print);

	threads = perf_thread_map__new_dummy();
	if (!threads)
		return -1;

	perf_thread_map__get(threads);
	perf_thread_map__put(threads);
	perf_thread_map__put(threads);

	test_threadmap_array(THREADS_NR, NULL);

	for (i = 0; i < THREADS_NR; i++)
		thr_array[i] = i + 100;

	test_threadmap_array(THREADS_NR, thr_array);

	__T_END;
	return tests_failed == 0 ? 0 : -1;
}
