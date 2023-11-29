// SPDX-License-Identifier: GPL-2.0
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <linux/perf_event.h>
#include <linux/kernel.h>
#include <perf/cpumap.h>
#include <perf/threadmap.h>
#include <perf/evsel.h>
#include <internal/evsel.h>
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
	int err, idx;

	cpus = perf_cpu_map__new_online_cpus();
	__T("failed to create cpus", cpus);

	evsel = perf_evsel__new(&attr);
	__T("failed to create evsel", evsel);

	err = perf_evsel__open(evsel, cpus, NULL);
	__T("failed to open evsel", err == 0);

	for (idx = 0; idx < perf_cpu_map__nr(cpus); idx++) {
		struct perf_counts_values counts = { .val = 0 };

		perf_evsel__read(evsel, idx, 0, &counts);
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
#ifdef __aarch64__
		.config1 = 0x2,		/* Request user access */
#endif
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
	__T("failed to get mmapped address", pc);

#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
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

static int test_stat_read_format_single(struct perf_event_attr *attr, struct perf_thread_map *threads)
{
	struct perf_evsel *evsel;
	struct perf_counts_values counts;
	volatile int count = 0x100000;
	int err;

	evsel = perf_evsel__new(attr);
	__T("failed to create evsel", evsel);

	/* skip old kernels that don't support the format */
	err = perf_evsel__open(evsel, NULL, threads);
	if (err < 0)
		return 0;

	while (count--) ;

	memset(&counts, -1, sizeof(counts));
	perf_evsel__read(evsel, 0, 0, &counts);

	__T("failed to read value", counts.val);
	if (attr->read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		__T("failed to read TOTAL_TIME_ENABLED", counts.ena);
	if (attr->read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		__T("failed to read TOTAL_TIME_RUNNING", counts.run);
	if (attr->read_format & PERF_FORMAT_ID)
		__T("failed to read ID", counts.id);
	if (attr->read_format & PERF_FORMAT_LOST)
		__T("failed to read LOST", counts.lost == 0);

	perf_evsel__close(evsel);
	perf_evsel__delete(evsel);
	return 0;
}

static int test_stat_read_format_group(struct perf_event_attr *attr, struct perf_thread_map *threads)
{
	struct perf_evsel *leader, *member;
	struct perf_counts_values counts;
	volatile int count = 0x100000;
	int err;

	attr->read_format |= PERF_FORMAT_GROUP;
	leader = perf_evsel__new(attr);
	__T("failed to create leader", leader);

	attr->read_format &= ~PERF_FORMAT_GROUP;
	member = perf_evsel__new(attr);
	__T("failed to create member", member);

	member->leader = leader;
	leader->nr_members = 2;

	/* skip old kernels that don't support the format */
	err = perf_evsel__open(leader, NULL, threads);
	if (err < 0)
		return 0;
	err = perf_evsel__open(member, NULL, threads);
	if (err < 0)
		return 0;

	while (count--) ;

	memset(&counts, -1, sizeof(counts));
	perf_evsel__read(leader, 0, 0, &counts);

	__T("failed to read leader value", counts.val);
	if (attr->read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		__T("failed to read leader TOTAL_TIME_ENABLED", counts.ena);
	if (attr->read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		__T("failed to read leader TOTAL_TIME_RUNNING", counts.run);
	if (attr->read_format & PERF_FORMAT_ID)
		__T("failed to read leader ID", counts.id);
	if (attr->read_format & PERF_FORMAT_LOST)
		__T("failed to read leader LOST", counts.lost == 0);

	memset(&counts, -1, sizeof(counts));
	perf_evsel__read(member, 0, 0, &counts);

	__T("failed to read member value", counts.val);
	if (attr->read_format & PERF_FORMAT_TOTAL_TIME_ENABLED)
		__T("failed to read member TOTAL_TIME_ENABLED", counts.ena);
	if (attr->read_format & PERF_FORMAT_TOTAL_TIME_RUNNING)
		__T("failed to read member TOTAL_TIME_RUNNING", counts.run);
	if (attr->read_format & PERF_FORMAT_ID)
		__T("failed to read member ID", counts.id);
	if (attr->read_format & PERF_FORMAT_LOST)
		__T("failed to read member LOST", counts.lost == 0);

	perf_evsel__close(member);
	perf_evsel__close(leader);
	perf_evsel__delete(member);
	perf_evsel__delete(leader);
	return 0;
}

static int test_stat_read_format(void)
{
	struct perf_thread_map *threads;
	struct perf_event_attr attr = {
		.type	= PERF_TYPE_SOFTWARE,
		.config	= PERF_COUNT_SW_TASK_CLOCK,
	};
	int err, i;

#define FMT(_fmt)  PERF_FORMAT_ ## _fmt
#define FMT_TIME  (FMT(TOTAL_TIME_ENABLED) | FMT(TOTAL_TIME_RUNNING))

	uint64_t test_formats [] = {
		0,
		FMT_TIME,
		FMT(ID),
		FMT(LOST),
		FMT_TIME | FMT(ID),
		FMT_TIME | FMT(LOST),
		FMT_TIME | FMT(ID) | FMT(LOST),
		FMT(ID) | FMT(LOST),
	};

#undef FMT
#undef FMT_TIME

	threads = perf_thread_map__new_dummy();
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	for (i = 0; i < (int)ARRAY_SIZE(test_formats); i++) {
		attr.read_format = test_formats[i];
		__T_VERBOSE("testing single read with read_format: %lx\n",
			    (unsigned long)test_formats[i]);

		err = test_stat_read_format_single(&attr, threads);
		__T("failed to read single format", err == 0);
	}

	perf_thread_map__put(threads);

	threads = perf_thread_map__new_array(2, NULL);
	__T("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);
	perf_thread_map__set_pid(threads, 1, 0);

	for (i = 0; i < (int)ARRAY_SIZE(test_formats); i++) {
		attr.read_format = test_formats[i];
		__T_VERBOSE("testing group read with read_format: %lx\n",
			    (unsigned long)test_formats[i]);

		err = test_stat_read_format_group(&attr, threads);
		__T("failed to read group format", err == 0);
	}

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
	test_stat_read_format();

	__T_END;
	return tests_failed == 0 ? 0 : -1;
}
