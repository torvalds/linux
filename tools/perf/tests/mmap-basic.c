// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <perf/cpumap.h>

#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"
#include "tests.h"
#include "util/mmap.h"
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <perf/evlist.h>
#include <perf/mmap.h>

/*
 * This test will generate random numbers of calls to some getpid syscalls,
 * then establish an mmap for a group of events that are created to monitor
 * the syscalls.
 *
 * It will receive the events, using mmap, use its PERF_SAMPLE_ID generated
 * sample.id field to map back to its respective perf_evsel instance.
 *
 * Then it checks if the number of syscalls reported as perf events by
 * the kernel corresponds to the number of syscalls made.
 */
static int test__basic_mmap(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int err = TEST_FAIL;
	union perf_event *event;
	struct perf_thread_map *threads;
	struct perf_cpu_map *cpus;
	struct evlist *evlist;
	cpu_set_t cpu_set;
	const char *syscall_names[] = { "getsid", "getppid", "getpgid", };
	pid_t (*syscalls[])(void) = { (void *)getsid, getppid, (void*)getpgid };
#define nsyscalls ARRAY_SIZE(syscall_names)
	unsigned int nr_events[nsyscalls],
		     expected_nr_events[nsyscalls], i, j;
	struct evsel *evsels[nsyscalls], *evsel;
	char sbuf[STRERR_BUFSIZE];
	struct mmap *md;

	threads = thread_map__new(-1, getpid(), UINT_MAX);
	if (threads == NULL) {
		pr_debug("thread_map__new\n");
		return -1;
	}

	cpus = perf_cpu_map__new(NULL);
	if (cpus == NULL) {
		pr_debug("perf_cpu_map__new\n");
		goto out_free_threads;
	}

	CPU_ZERO(&cpu_set);
	CPU_SET(perf_cpu_map__cpu(cpus, 0).cpu, &cpu_set);
	sched_setaffinity(0, sizeof(cpu_set), &cpu_set);
	if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) < 0) {
		pr_debug("sched_setaffinity() failed on CPU %d: %s ",
			 perf_cpu_map__cpu(cpus, 0).cpu,
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_free_cpus;
	}

	evlist = evlist__new();
	if (evlist == NULL) {
		pr_debug("evlist__new\n");
		goto out_free_cpus;
	}

	perf_evlist__set_maps(&evlist->core, cpus, threads);

	for (i = 0; i < nsyscalls; ++i) {
		char name[64];

		snprintf(name, sizeof(name), "sys_enter_%s", syscall_names[i]);
		evsels[i] = evsel__newtp("syscalls", name);
		if (IS_ERR(evsels[i])) {
			pr_debug("evsel__new(%s)\n", name);
			if (PTR_ERR(evsels[i]) == -EACCES) {
				/* Permissions failure, flag the failure as a skip. */
				err = TEST_SKIP;
			}
			goto out_delete_evlist;
		}

		evsels[i]->core.attr.wakeup_events = 1;
		evsel__set_sample_id(evsels[i], false);

		evlist__add(evlist, evsels[i]);

		if (evsel__open(evsels[i], cpus, threads) < 0) {
			pr_debug("failed to open counter: %s, "
				 "tweak /proc/sys/kernel/perf_event_paranoid?\n",
				 str_error_r(errno, sbuf, sizeof(sbuf)));
			goto out_delete_evlist;
		}

		nr_events[i] = 0;
		expected_nr_events[i] = 1 + rand() % 127;
	}

	if (evlist__mmap(evlist, 128) < 0) {
		pr_debug("failed to mmap events: %d (%s)\n", errno,
			 str_error_r(errno, sbuf, sizeof(sbuf)));
		goto out_delete_evlist;
	}

	for (i = 0; i < nsyscalls; ++i)
		for (j = 0; j < expected_nr_events[i]; ++j) {
			syscalls[i]();
		}

	md = &evlist->mmap[0];
	if (perf_mmap__read_init(&md->core) < 0)
		goto out_init;

	while ((event = perf_mmap__read_event(&md->core)) != NULL) {
		struct perf_sample sample;

		if (event->header.type != PERF_RECORD_SAMPLE) {
			pr_debug("unexpected %s event\n",
				 perf_event__name(event->header.type));
			goto out_delete_evlist;
		}

		err = evlist__parse_sample(evlist, event, &sample);
		if (err) {
			pr_err("Can't parse sample, err = %d\n", err);
			goto out_delete_evlist;
		}

		err = -1;
		evsel = evlist__id2evsel(evlist, sample.id);
		if (evsel == NULL) {
			pr_debug("event with id %" PRIu64
				 " doesn't map to an evsel\n", sample.id);
			goto out_delete_evlist;
		}
		nr_events[evsel->core.idx]++;
		perf_mmap__consume(&md->core);
	}
	perf_mmap__read_done(&md->core);

out_init:
	err = 0;
	evlist__for_each_entry(evlist, evsel) {
		if (nr_events[evsel->core.idx] != expected_nr_events[evsel->core.idx]) {
			pr_debug("expected %d %s events, got %d\n",
				 expected_nr_events[evsel->core.idx],
				 evsel__name(evsel), nr_events[evsel->core.idx]);
			err = -1;
			goto out_delete_evlist;
		}
	}

out_delete_evlist:
	evlist__delete(evlist);
out_free_cpus:
	perf_cpu_map__put(cpus);
out_free_threads:
	perf_thread_map__put(threads);
	return err;
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
	int err, i, ret = TEST_FAIL;
	bool opened = false, mapped = false;

	threads = perf_thread_map__new_dummy();
	TEST_ASSERT_VAL("failed to create threads", threads);

	perf_thread_map__set_pid(threads, 0, 0);

	evsel = perf_evsel__new(&attr);
	TEST_ASSERT_VAL("failed to create evsel", evsel);

	err = perf_evsel__open(evsel, NULL, threads);
	if (err) {
		pr_err("failed to open evsel: %s\n", strerror(-err));
		ret = TEST_SKIP;
		goto out;
	}
	opened = true;

	err = perf_evsel__mmap(evsel, 0);
	if (err) {
		pr_err("failed to mmap evsel: %s\n", strerror(-err));
		goto out;
	}
	mapped = true;

	pc = perf_evsel__mmap_base(evsel, 0, 0);
	if (!pc) {
		pr_err("failed to get mmapped address\n");
		goto out;
	}

	if (!pc->cap_user_rdpmc || !pc->index) {
		pr_err("userspace counter access not %s\n",
			!pc->cap_user_rdpmc ? "supported" : "enabled");
		ret = TEST_SKIP;
		goto out;
	}
	if (pc->pmc_width < 32) {
		pr_err("userspace counter width not set (%d)\n", pc->pmc_width);
		goto out;
	}

	perf_evsel__read(evsel, 0, 0, &counts);
	if (counts.val == 0) {
		pr_err("failed to read value for evsel\n");
		goto out;
	}

	for (i = 0; i < 5; i++) {
		volatile int count = 0x10000 << i;
		__u64 start, end, last = 0;

		pr_debug("\tloop = %u, ", count);

		perf_evsel__read(evsel, 0, 0, &counts);
		start = counts.val;

		while (count--) ;

		perf_evsel__read(evsel, 0, 0, &counts);
		end = counts.val;

		if ((end - start) < last) {
			pr_err("invalid counter data: end=%llu start=%llu last= %llu\n",
				end, start, last);
			goto out;
		}
		last = end - start;
		pr_debug("count = %llu\n", end - start);
	}
	ret = TEST_OK;

out:
	if (mapped)
		perf_evsel__munmap(evsel);
	if (opened)
		perf_evsel__close(evsel);
	perf_evsel__delete(evsel);

	perf_thread_map__put(threads);
	return ret;
}

static int test__mmap_user_read_instr(struct test_suite *test __maybe_unused,
				      int subtest __maybe_unused)
{
	return test_stat_user_read(PERF_COUNT_HW_INSTRUCTIONS);
}

static int test__mmap_user_read_cycles(struct test_suite *test __maybe_unused,
				       int subtest __maybe_unused)
{
	return test_stat_user_read(PERF_COUNT_HW_CPU_CYCLES);
}

static struct test_case tests__basic_mmap[] = {
	TEST_CASE_REASON("Read samples using the mmap interface",
			 basic_mmap,
			 "permissions"),
	TEST_CASE_REASON("User space counter reading of instructions",
			 mmap_user_read_instr,
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
			 "permissions"
#else
			 "unsupported"
#endif
		),
	TEST_CASE_REASON("User space counter reading of cycles",
			 mmap_user_read_cycles,
#if defined(__i386__) || defined(__x86_64__) || defined(__aarch64__)
			 "permissions"
#else
			 "unsupported"
#endif
		),
	{	.name = NULL, }
};

struct test_suite suite__basic_mmap = {
	.desc = "mmap interface tests",
	.test_cases = tests__basic_mmap,
};
