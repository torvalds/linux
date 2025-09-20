// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <linux/types.h>
#include <sys/prctl.h>
#include <perf/cpumap.h>
#include <perf/evlist.h>
#include <perf/mmap.h>

#include "debug.h"
#include "parse-events.h"
#include "evlist.h"
#include "evsel.h"
#include "thread_map.h"
#include "record.h"
#include "tsc.h"
#include "mmap.h"
#include "tests.h"
#include "util/sample.h"

/*
 * Except x86_64/i386 and Arm64, other archs don't support TSC in perf.  Just
 * enable the test for x86_64/i386 and Arm64 archs.
 */
#if defined(__x86_64__) || defined(__i386__) || defined(__aarch64__)
#define TSC_IS_SUPPORTED 1
#else
#define TSC_IS_SUPPORTED 0
#endif

#define CHECK__(x) {				\
	while ((x) < 0) {			\
		pr_debug(#x " failed!\n");	\
		goto out_err;			\
	}					\
}

#define CHECK_NOT_NULL__(x) {			\
	while ((x) == NULL) {			\
		pr_debug(#x " failed!\n");	\
		goto out_err;			\
	}					\
}

static int test__tsc_is_supported(struct test_suite *test __maybe_unused,
				  int subtest __maybe_unused)
{
	if (!TSC_IS_SUPPORTED) {
		pr_debug("Test not supported on this architecture\n");
		return TEST_SKIP;
	}

	return TEST_OK;
}

/**
 * test__perf_time_to_tsc - test converting perf time to TSC.
 *
 * This function implements a test that checks that the conversion of perf time
 * to and from TSC is consistent with the order of events.  If the test passes
 * %0 is returned, otherwise %-1 is returned.  If TSC conversion is not
 * supported then the test passes but " (not supported)" is printed.
 */
static int test__perf_time_to_tsc(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	struct record_opts opts = {
		.mmap_pages	     = UINT_MAX,
		.user_freq	     = UINT_MAX,
		.user_interval	     = ULLONG_MAX,
		.target		     = {
			.uses_mmap   = true,
		},
		.sample_time	     = true,
	};
	struct perf_thread_map *threads = NULL;
	struct perf_cpu_map *cpus = NULL;
	struct evlist *evlist = NULL;
	struct evsel *evsel = NULL;
	int err = TEST_FAIL, ret, i;
	const char *comm1, *comm2;
	struct perf_tsc_conversion tc;
	struct perf_event_mmap_page *pc;
	union perf_event *event;
	u64 test_tsc, comm1_tsc, comm2_tsc;
	u64 test_time, comm1_time = 0, comm2_time = 0;
	struct mmap *md;


	threads = thread_map__new_by_tid(getpid());
	CHECK_NOT_NULL__(threads);

	cpus = perf_cpu_map__new_online_cpus();
	CHECK_NOT_NULL__(cpus);

	evlist = evlist__new();
	CHECK_NOT_NULL__(evlist);

	perf_evlist__set_maps(&evlist->core, cpus, threads);

	CHECK__(parse_event(evlist, "cycles:u"));

	evlist__config(evlist, &opts, NULL);

	/* For hybrid "cycles:u", it creates two events */
	evlist__for_each_entry(evlist, evsel) {
		evsel->core.attr.comm = 1;
		evsel->core.attr.disabled = 1;
		evsel->core.attr.enable_on_exec = 0;
	}

	ret = evlist__open(evlist);
	if (ret < 0) {
		if (ret == -ENOENT)
			err = TEST_SKIP;
		else
			pr_debug("evlist__open() failed\n");
		goto out_err;
	}

	CHECK__(evlist__mmap(evlist, UINT_MAX));

	pc = evlist->mmap[0].core.base;
	ret = perf_read_tsc_conversion(pc, &tc);
	if (ret) {
		if (ret == -EOPNOTSUPP) {
			pr_debug("perf_read_tsc_conversion is not supported in current kernel\n");
			err = TEST_SKIP;
		}
		goto out_err;
	}

	evlist__enable(evlist);

	comm1 = "Test COMM 1";
	CHECK__(prctl(PR_SET_NAME, (unsigned long)comm1, 0, 0, 0));

	test_tsc = rdtsc();

	comm2 = "Test COMM 2";
	CHECK__(prctl(PR_SET_NAME, (unsigned long)comm2, 0, 0, 0));

	evlist__disable(evlist);

	for (i = 0; i < evlist->core.nr_mmaps; i++) {
		md = &evlist->mmap[i];
		if (perf_mmap__read_init(&md->core) < 0)
			continue;

		while ((event = perf_mmap__read_event(&md->core)) != NULL) {
			struct perf_sample sample;

			perf_sample__init(&sample, /*all=*/false);
			if (event->header.type != PERF_RECORD_COMM ||
			    (pid_t)event->comm.pid != getpid() ||
			    (pid_t)event->comm.tid != getpid())
				goto next_event;

			if (strcmp(event->comm.comm, comm1) == 0) {
				CHECK_NOT_NULL__(evsel = evlist__event2evsel(evlist, event));
				CHECK__(evsel__parse_sample(evsel, event, &sample));
				comm1_time = sample.time;
			}
			if (strcmp(event->comm.comm, comm2) == 0) {
				CHECK_NOT_NULL__(evsel = evlist__event2evsel(evlist, event));
				CHECK__(evsel__parse_sample(evsel, event, &sample));
				comm2_time = sample.time;
			}
next_event:
			perf_mmap__consume(&md->core);
			perf_sample__exit(&sample);
		}
		perf_mmap__read_done(&md->core);
	}

	if (!comm1_time || !comm2_time)
		goto out_err;

	test_time = tsc_to_perf_time(test_tsc, &tc);
	comm1_tsc = perf_time_to_tsc(comm1_time, &tc);
	comm2_tsc = perf_time_to_tsc(comm2_time, &tc);

	pr_debug("1st event perf time %"PRIu64" tsc %"PRIu64"\n",
		 comm1_time, comm1_tsc);
	pr_debug("rdtsc          time %"PRIu64" tsc %"PRIu64"\n",
		 test_time, test_tsc);
	pr_debug("2nd event perf time %"PRIu64" tsc %"PRIu64"\n",
		 comm2_time, comm2_tsc);

	if (test_time <= comm1_time ||
	    test_time >= comm2_time)
		goto out_err;

	if (test_tsc <= comm1_tsc ||
	    test_tsc >= comm2_tsc)
		goto out_err;

	err = TEST_OK;

out_err:
	evlist__delete(evlist);
	perf_cpu_map__put(cpus);
	perf_thread_map__put(threads);
	return err;
}

static struct test_case time_to_tsc_tests[] = {
	TEST_CASE_REASON("TSC support", tsc_is_supported,
			 "This architecture does not support"),
	TEST_CASE_REASON("Perf time to TSC", perf_time_to_tsc,
			 "perf_read_tsc_conversion is not supported"),
	{ .name = NULL, }
};

struct test_suite suite__perf_time_to_tsc = {
	.desc = "Convert perf time to TSC",
	.test_cases = time_to_tsc_tests,
};
