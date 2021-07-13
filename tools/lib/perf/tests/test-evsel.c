// SPDX-License-Identifier: GPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <linux/perf_event.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <perf/evsel.h>
#include <internal/tests.h>
#include "tests.h"

static int libperf_print(enum libperf_print_level level,
			 const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

static int test_stat_cpu(void)
{
	struct perf_cpu_map *cpus;
	struct perf_evsel *evsel;
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_CPU_CLOCK,
	};
	int err, cpu, tmp;

	cpus = perf_cpu_map__new(NULL);
	__T("failed to create cpus", cpus);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel", evsel);

	err = perf_evsel__open(evsel, cpus, NULL);
	__T("failed to open evsel", err == 0);

	perf_cpu_map__for_each_cpu(cpu, tmp, cpus) {
		struct perf_counts_values counts = { .val = 0 };

		perf_evsel__read(evsel, cpu, 0, &counts);
		__T("failed to read value for evsel", counts.val != 0);
	}

	perf_evsel__close(evsel);
	perf_evsel__delete(evsel);

	perf_cpu_map__put(cpus);
	return 0;
}

static int test_stat_thread(void)
{
	struct perf_counts_values counts = { .val = 0 };
	struct perf_thread_map *threads;
	struct perf_evsel *evsel;
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_TASK_CLOCK,
	};
	int err;

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel", evsel);

	err = perf_evsel__open(evsel, NULL, threads);
	__T("failed to open evsel", err == 0);

	perf_evsel__read(evsel, 0, 0, &counts);
	__T("failed to read value for evsel", counts.val != 0);

	perf_evsel__close(evsel);
	perf_evsel__delete(evsel);

	perf_thread_map__put(threads);
	return 0;
}

static int test_stat_thread_enable(void)
{
	struct perf_counts_values counts = { .val = 0 };
	struct perf_thread_map *threads;
	struct perf_evsel *evsel;
	struct perf_event_attr attr = {
		.type	  = PERF_TYPE_SOFTWARE,
		.config	  = PERF_COUNT_SW_TASK_CLOCK,
		.disabled = 1,
	};
	int err;

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel", evsel);

	err = perf_evsel__open(evsel, NULL, threads);
	__T("failed to open evsel", err == 0);

	perf_evsel__read(evsel, 0, 0, &counts);
	__T("failed to read value for evsel", counts.val == 0);

	err = perf_evsel__enable(evsel);
	__T("failed to enable evsel", err == 0);

	perf_evsel__read(evsel, 0, 0, &counts);
	__T("failed to read value for evsel", counts.val != 0);

	err = perf_evsel__disable(evsel);
	__T("failed to enable evsel", err == 0);

	perf_evsel__close(evsel);
	perf_evsel__delete(evsel);

	perf_thread_map__put(threads);
	return 0;
}

static int test_stat_user_read(int event)
{
	struct perf_counts_values counts = { .val = 0 };
	struct perf_thread_map *threads;
	struct perf_evsel *evsel;
	struct perf_event_mmap_page *pc;
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_HARDWARE,
		.config	= event,
	};
	int err, i;

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel", evsel);

	err = perf_evsel__open(evsel, NULL, threads);
	__T("failed to open evsel", err == 0);

	err = perf_evsel__mmap(evsel, 0);
	__T("failed to mmap evsel", err == 0);

	pc = perf_evsel__mmap_base(evsel, 0, 0);

#if defined(__i386__) || defined(__x86_64__)
	__T("userspace counter access not supported", pc->cap_user_rdpmc);
	__T("userspace counter access not enabled", pc->index);
	__T("userspace counter width not set", pc->pmc_width >= 32);
#endif

	perf_evsel__read(evsel, 0, 0, &counts);
	__T("failed to read value for evsel", counts.val != 0);

	for (i = 0; i < 5; i++) {
		volatile int count = 0x10000 << i;
		__u64 start, end, last = 0;

		__T_VERBOSE("\tloop = %u, ", count);

		perf_evsel__read(evsel, 0, 0, &counts);
		start = counts.val;

		while (count--) ;

		perf_evsel__read(evsel, 0, 0, &counts);
		end = counts.val;

		__T("invalid counter data", (end - start) > last);
		last = end - start;
		__T_VERBOSE("count = %llu\n", end - start);
	}

	perf_evsel__munmap(evsel);
	perf_evsel__close(evsel);
	perf_evsel__delete(evsel);

	perf_thread_map__put(threads);
	return 0;
}

int test_evsel(int argc, char **argv)
{
	__T_START;

	libperf_init(libperf_print);

	test_stat_cpu();
	test_stat_thread();
	test_stat_thread_enable();
	test_stat_user_read(PERF_COUNT_HW_INSTRUCTIONS);
	test_stat_user_read(PERF_COUNT_HW_CPU_CYCLES);

	__T_END;
	return tests_failed == 0 ? 0 : -1;
}
