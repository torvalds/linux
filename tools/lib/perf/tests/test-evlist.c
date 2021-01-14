// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE // needed for sched.h to get sched_[gs]etaffinity and CPU_(ZERO,SET)
#include <sched.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/perf_event.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <perf/evlist.h>
#include <perf/evsel.h>
#include <perf/mmap.h>
#include <perf/event.h>
#include <internal/tests.h>
#include <api/fs/fs.h>

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

static int test_mmap_thread(void)
{
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_mmap *map;
	struct perf_cpu_map *cpus;
	struct perf_thread_map *threads;
	struct perf_event_attr attr = {
		.type             = PERF_TYPE_TRACEPOINT,
		.sample_period    = 1,
		.wakeup_watermark = 1,
		.disabled         = 1,
	};
	char path[PATH_MAX];
	int id, err, pid, go_pipe[2];
	union perf_event *event;
	char bf;
	int count = 0;

	snprintf(path, PATH_MAX, "%s/kernel/debug/tracing/events/syscalls/sys_enter_prctl/id",
		 sysfs__mountpoint());

	if (filename__read_int(path, &id)) {
		tests_failed++;
		fprintf(stderr, "error: failed to get tracepoint id: %s\n", path);
		return -1;
	}

	attr.config = id;

	err = pipe(go_pipe);
	__T("failed to create pipe", err == 0);

	fflush(NULL);

	pid = fork();
	if (!pid) {
		int i;

		read(go_pipe[0], &bf, 1);

		/* Generate 100 prctl calls. */
		for (i = 0; i < 100; i++)
			prctl(0, 0, 0, 0, 0);

		exit(0);
	}

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	cpus = perf_cpu_map__dummy_new();
	__T("failed to create cpus", cpus);

	perf_thread_map__set_pid(threads, 0, pid);

	evlist = perf_evlist__new();
	__T("failed to create evlist", evlist);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel1", evsel);

	perf_evlist__add(evlist, evsel);

	perf_evlist__set_maps(evlist, cpus, threads);

	err = perf_evlist__open(evlist);
	__T("failed to open evlist", err == 0);

	err = perf_evlist__mmap(evlist, 4);
	__T("failed to mmap evlist", err == 0);

	perf_evlist__enable(evlist);

	/* kick the child and wait for it to finish */
	write(go_pipe[1], &bf, 1);
	waitpid(pid, NULL, 0);

	/*
	 * There's no need to call perf_evlist__disable,
	 * monitored process is dead now.
	 */

	perf_evlist__for_each_mmap(evlist, map, false) {
		if (perf_mmap__read_init(map) < 0)
			continue;

		while ((event = perf_mmap__read_event(map)) != NULL) {
			count++;
			perf_mmap__consume(map);
		}

		perf_mmap__read_done(map);
	}

	/* calls perf_evlist__munmap/perf_evlist__close */
	perf_evlist__delete(evlist);

	perf_thread_map__put(threads);
	perf_cpu_map__put(cpus);

	/*
	 * The generated prctl calls should match the
	 * number of events in the buffer.
	 */
	__T("failed count", count == 100);

	return 0;
}

static int test_mmap_cpus(void)
{
	struct perf_evlist *evlist;
	struct perf_evsel *evsel;
	struct perf_mmap *map;
	struct perf_cpu_map *cpus;
	struct perf_event_attr attr = {
		.type             = PERF_TYPE_TRACEPOINT,
		.sample_period    = 1,
		.wakeup_watermark = 1,
		.disabled         = 1,
	};
	cpu_set_t saved_mask;
	char path[PATH_MAX];
	int id, err, cpu, tmp;
	union perf_event *event;
	int count = 0;

	snprintf(path, PATH_MAX, "%s/kernel/debug/tracing/events/syscalls/sys_enter_prctl/id",
		 sysfs__mountpoint());

	if (filename__read_int(path, &id)) {
		fprintf(stderr, "error: failed to get tracepoint id: %s\n", path);
		return -1;
	}

	attr.config = id;

	cpus = perf_cpu_map__new(NULL);
	__T("failed to create cpus", cpus);

	evlist = perf_evlist__new();
	__T("failed to create evlist", evlist);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel1", evsel);

	perf_evlist__add(evlist, evsel);

	perf_evlist__set_maps(evlist, cpus, NULL);

	err = perf_evlist__open(evlist);
	__T("failed to open evlist", err == 0);

	err = perf_evlist__mmap(evlist, 4);
	__T("failed to mmap evlist", err == 0);

	perf_evlist__enable(evlist);

	err = sched_getaffinity(0, sizeof(saved_mask), &saved_mask);
	__T("sched_getaffinity failed", err == 0);

	perf_cpu_map__for_each_cpu(cpu, tmp, cpus) {
		cpu_set_t mask;

		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);

		err = sched_setaffinity(0, sizeof(mask), &mask);
		__T("sched_setaffinity failed", err == 0);

		prctl(0, 0, 0, 0, 0);
	}

	err = sched_setaffinity(0, sizeof(saved_mask), &saved_mask);
	__T("sched_setaffinity failed", err == 0);

	perf_evlist__disable(evlist);

	perf_evlist__for_each_mmap(evlist, map, false) {
		if (perf_mmap__read_init(map) < 0)
			continue;

		while ((event = perf_mmap__read_event(map)) != NULL) {
			count++;
			perf_mmap__consume(map);
		}

		perf_mmap__read_done(map);
	}

	/* calls perf_evlist__munmap/perf_evlist__close */
	perf_evlist__delete(evlist);

	/*
	 * The generated prctl events should match the
	 * number of cpus or be bigger (we are system-wide).
	 */
	__T("failed count", count >= perf_cpu_map__nr(cpus));

	perf_cpu_map__put(cpus);

	return 0;
}

int main(int argc, char **argv)
{
	__T_START;

	libperf_init(libperf_print);

	test_stat_cpu();
	test_stat_thread();
	test_stat_thread_enable();
	test_mmap_thread();
	test_mmap_cpus();

	__T_END;
	return tests_failed == 0 ? 0 : -1;
}
