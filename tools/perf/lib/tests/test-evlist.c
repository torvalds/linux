// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdarg.h>
#include <linux/perf_event.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <internal/tests.h>

static int libperf_print(enum libperf_print_level level,
			 const char *fmt, va_list ap)
{
	return vfprintf(stderr, fmt, ap);
}

static int test_stat_cpu(void)
{
	struct perf_cpu_map *cpus;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_event_attr attr1 = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_CPU_CLOCK,
	};
	struct perf_event_attr attr2 = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_TASK_CLOCK,
	};
	int err, cpu, tmp;

	cpus = perf_cpu_map__new(NULL);
	__T("failed to create cpus", cpus);

	evlist = perf_evlist__new();
	__T("failed to create evlist", evlist);

	evsel = perf_evsel__new(&attr1);
	__T("failed to create evsel1", evsel);

	perf_evlist__add(evlist, evsel);

	evsel = perf_evsel__new(&attr2);
	__T("failed to create evsel2", evsel);

	perf_evlist__add(evlist, evsel);

	perf_evlist__set_maps(evlist, cpus, NULL);

	err = perf_evlist__open(evlist);
	__T("failed to open evsel", err == 0);

	perf_evlist__for_each_evsel(evlist, evsel) {
		cpus = perf_evsel__cpus(evsel);

		perf_cpu_map__for_each_cpu(cpu, tmp, cpus) {
			struct perf_counts_values counts = { .val = 0 };

			perf_evsel__read(evsel, cpu, 0, &counts);
			__T("failed to read value for evsel", counts.val != 0);
		}
	}

	perf_evlist__close(evlist);
	perf_evlist__delete(evlist);

	perf_cpu_map__put(cpus);
	return 0;
}

static int test_stat_thread(void)
{
	struct perf_counts_values counts = { .val = 0 };
	struct perf_thread_map *threads;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_event_attr attr1 = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_CPU_CLOCK,
	};
	struct perf_event_attr attr2 = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_TASK_CLOCK,
	};
	int err;

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	evlist = perf_evlist__new();
	__T("failed to create evlist", evlist);

	evsel = perf_evsel__new(&attr1);
	__T("failed to create evsel1", evsel);

	perf_evlist__add(evlist, evsel);

	evsel = perf_evsel__new(&attr2);
	__T("failed to create evsel2", evsel);

	perf_evlist__add(evlist, evsel);

	perf_evlist__set_maps(evlist, NULL, threads);

	err = perf_evlist__open(evlist);
	__T("failed to open evsel", err == 0);

	perf_evlist__for_each_evsel(evlist, evsel) {
		perf_evsel__read(evsel, 0, 0, &counts);
		__T("failed to read value for evsel", counts.val != 0);
	}

	perf_evlist__close(evlist);
	perf_evlist__delete(evlist);

	perf_thread_map__put(threads);
	return 0;
}

static int test_stat_thread_enable(void)
{
	struct perf_counts_values counts = { .val = 0 };
	struct perf_thread_map *threads;
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_event_attr attr1 = {
		.type	  = PERF_TYPE_SOFTWARE,
		.config	  = PERF_COUNT_SW_CPU_CLOCK,
		.disabled = 1,
	};
	struct perf_event_attr attr2 = {
		.type	  = PERF_TYPE_SOFTWARE,
		.config	  = PERF_COUNT_SW_TASK_CLOCK,
		.disabled = 1,
	};
	int err;

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	evlist = perf_evlist__new();
	__T("failed to create evlist", evlist);

	evsel = perf_evsel__new(&attr1);
	__T("failed to create evsel1", evsel);

	perf_evlist__add(evlist, evsel);

	evsel = perf_evsel__new(&attr2);
	__T("failed to create evsel2", evsel);

	perf_evlist__add(evlist, evsel);

	perf_evlist__set_maps(evlist, NULL, threads);

	err = perf_evlist__open(evlist);
	__T("failed to open evsel", err == 0);

	perf_evlist__for_each_evsel(evlist, evsel) {
		perf_evsel__read(evsel, 0, 0, &counts);
		__T("failed to read value for evsel", counts.val == 0);
	}

	perf_evlist__enable(evlist);

	perf_evlist__for_each_evsel(evlist, evsel) {
		perf_evsel__read(evsel, 0, 0, &counts);
		__T("failed to read value for evsel", counts.val != 0);
	}

	perf_evlist__disable(evlist);

	perf_evlist__close(evlist);
	perf_evlist__delete(evlist);

	perf_thread_map__put(threads);
	return 0;
}

int main(int argc, char **argv)
{
	__T_START;

	libperf_init(libperf_print);

	test_stat_cpu();
	test_stat_thread();
	test_stat_thread_enable();

	__T_OK;
	return 0;
}
