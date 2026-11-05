// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <internal/xyarray.h>
#include "cpumap.h"
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "parse-events.h"
#include "tests.h"
#include "thread_map.h"
#include "tool_pmu.h"

static int do_test(enum tool_pmu_event ev, bool with_pmu)
{
	struct evlist *evlist = evlist__new();
	struct evsel *evsel;
	struct parse_events_error err;
	int ret;
	char str[128];
	bool found = false;

	if (!evlist) {
		pr_err("evlist allocation failed\n");
		return TEST_FAIL;
	}

	if (with_pmu)
		snprintf(str, sizeof(str), "tool/%s/", tool_pmu__event_to_str(ev));
	else
		snprintf(str, sizeof(str), "%s", tool_pmu__event_to_str(ev));

	parse_events_error__init(&err);
	ret = parse_events(evlist, str, &err);
	if (ret) {
		if (!tool_pmu__event_to_str(ev)) {
			ret = TEST_OK;
			goto out;
		}

		pr_debug("FAILED %s:%d failed to parse event '%s', err %d\n",
			 __FILE__, __LINE__, str, ret);
		parse_events_error__print(&err, str);
		ret = TEST_FAIL;
		goto out;
	}

	ret = TEST_OK;
	if (with_pmu ? (evlist__nr_entries(evlist) != 1)
		     : (evlist__nr_entries(evlist) < 1)) {
		pr_debug("FAILED %s:%d Unexpected number of events for '%s' of %d\n",
			 __FILE__, __LINE__, str, evlist__nr_entries(evlist));
		ret = TEST_FAIL;
		goto out;
	}

	evlist__for_each_entry(evlist, evsel) {
		if (perf_pmu__is_tool(evsel->pmu)) {
			if (evsel->core.attr.config != ev) {
				pr_debug("FAILED %s:%d Unexpected config for '%s', %lld != %d\n",
					__FILE__, __LINE__, str, evsel->core.attr.config, ev);
				ret = TEST_FAIL;
				goto out;
			}
			found = true;
		}
	}

	if (!found && tool_pmu__event_to_str(ev)) {
		pr_debug("FAILED %s:%d Didn't find tool event '%s' in parsed evsels\n",
			 __FILE__, __LINE__, str);
		ret = TEST_FAIL;
	}

out:
	parse_events_error__exit(&err);
	evlist__put(evlist);
	return ret;
}

static int test__tool_pmu_without_pmu(struct test_suite *test __maybe_unused,
				      int subtest __maybe_unused)
{
	int i;

	tool_pmu__for_each_event(i) {
		int ret = do_test(i, /*with_pmu=*/false);

		if (ret != TEST_OK)
			return ret;
	}
	return TEST_OK;
}

static int test__tool_pmu_with_pmu(struct test_suite *test __maybe_unused,
				   int subtest __maybe_unused)
{
	int i;

	tool_pmu__for_each_event(i) {
		int ret = do_test(i, /*with_pmu=*/true);

		if (ret != TEST_OK)
			return ret;
	}
	return TEST_OK;
}

static int test__tool_pmu_incremental_open_unwind(struct test_suite *test __maybe_unused,
						  int subtest __maybe_unused)
{
	struct evlist *evlist = evlist__new();
	struct parse_events_error err;
	struct perf_cpu_map *cpus = NULL;
	struct perf_thread_map *threads = NULL;
	struct evsel *evsel;
	int ret = TEST_FAIL, fd0 = -1;

	if (!evlist)
		return TEST_FAIL;

	parse_events_error__init(&err);
	if (parse_events(evlist, "tool/user_time/", &err)) {
		parse_events_error__exit(&err);
		evlist__put(evlist);
		return TEST_FAIL;
	}
	parse_events_error__exit(&err);

	evsel = evlist__first(evlist);
	cpus = perf_cpu_map__new("0,1");
	threads = thread_map__new_by_tid(getpid());
	if (!cpus || !threads)
		goto out;

	/* Step 1: Open CPU index 0 successfully */
	if (evsel__open_per_cpu_and_thread(evsel, cpus, 0, threads) < 0) {
		pr_debug("Failed to open CPU index 0\n");
		goto out;
	}

	fd0 = (*(int *)xyarray__entry(evsel->core.fd, 0, 0));
	if (fd0 < 0 || fcntl(fd0, F_GETFD) < 0) {
		pr_debug("CPU index 0 FD is invalid (%d)\n", fd0);
		goto out;
	}

	/* Step 2: Intentionally trigger failure on CPU index 1 */
	evsel->core.attr.sample_period = 1; /* Not supported for tool PMU -> -EINVAL */
	if (evsel__open_per_cpu_and_thread(evsel, cpus, 1, threads) >= 0) {
		pr_debug("Unexpected success opening CPU index 1 with sample_period=1\n");
		goto out;
	}

	/*
	 * Step 3: Check that CPU index 0's FD was NOT destroyed by CPU index 1's unwind.
	 */
	if ((*(int *)xyarray__entry(evsel->core.fd, 0, 0)) != fd0) {
		pr_debug("FAILED: CPU 0 FD overwritten: FD(evsel, 0, 0)=%d, expected %d\n",
			 (*(int *)xyarray__entry(evsel->core.fd, 0, 0)), fd0);
		goto out;
	}

	if (fcntl(fd0, F_GETFD) < 0) {
		pr_debug("FAILED: CPU 0 FD %d was closed by error unwind! errno=%d (%s)\n",
			 fd0, errno, strerror(errno));
		goto out;
	}

	ret = TEST_OK;
out:
	evsel__close(evsel);
	perf_cpu_map__put(cpus);
	perf_thread_map__put(threads);
	evlist__put(evlist);
	return ret;
}

static struct test_case tests__tool_pmu[] = {
	TEST_CASE("Parsing without PMU name", tool_pmu_without_pmu),
	TEST_CASE("Parsing with PMU name", tool_pmu_with_pmu),
	TEST_CASE("Incremental open error unwind boundary", tool_pmu_incremental_open_unwind),
	{	.name = NULL, }
};

struct test_suite suite__tool_pmu = {
	.desc = "Tool PMU",
	.test_cases = tests__tool_pmu,
};
