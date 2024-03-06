// SPDX-License-Identifier: GPL-2.0
#include "arch-tests.h"
#include "debug.h"
#include "evlist.h"
#include "evsel.h"
#include "pmu.h"
#include "pmus.h"
#include "tests/tests.h"

static bool test_config(const struct evsel *evsel, __u64 expected_config)
{
	return (evsel->core.attr.config & PERF_HW_EVENT_MASK) == expected_config;
}

static bool test_perf_config(const struct perf_evsel *evsel, __u64 expected_config)
{
	return (evsel->attr.config & PERF_HW_EVENT_MASK) == expected_config;
}

static bool test_hybrid_type(const struct evsel *evsel, __u64 expected_config)
{
	return (evsel->core.attr.config >> PERF_PMU_TYPE_SHIFT) == expected_config;
}

static int test__hybrid_hw_event_with_pmu(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_CPU_CYCLES));
	return TEST_OK;
}

static int test__hybrid_hw_group_event(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_CPU_CYCLES));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_BRANCH_INSTRUCTIONS));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

static int test__hybrid_sw_hw_group_event(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_CPU_CYCLES));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

static int test__hybrid_hw_sw_group_event(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_CPU_CYCLES));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_SOFTWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

static int test__hybrid_group_modifier1(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_CPU_CYCLES));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong exclude_user", evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", !evsel->core.attr.exclude_kernel);

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_BRANCH_INSTRUCTIONS));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	TEST_ASSERT_VAL("wrong exclude_user", !evsel->core.attr.exclude_user);
	TEST_ASSERT_VAL("wrong exclude_kernel", evsel->core.attr.exclude_kernel);
	return TEST_OK;
}

static int test__hybrid_raw1(struct evlist *evlist)
{
	struct perf_evsel *evsel;

	perf_evlist__for_each_evsel(&evlist->core, evsel) {
		struct perf_pmu *pmu = perf_pmus__find_by_type(evsel->attr.type);

		TEST_ASSERT_VAL("missing pmu", pmu);
		TEST_ASSERT_VAL("unexpected pmu", !strncmp(pmu->name, "cpu_", 4));
		TEST_ASSERT_VAL("wrong config", test_perf_config(evsel, 0x1a));
	}
	return TEST_OK;
}

static int test__hybrid_raw2(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", test_config(evsel, 0x1a));
	return TEST_OK;
}

static int test__hybrid_cache_event(struct evlist *evlist)
{
	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HW_CACHE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", 0x2 == (evsel->core.attr.config & 0xffffffff));
	return TEST_OK;
}

static int test__checkevent_pmu(struct evlist *evlist)
{

	struct evsel *evsel = evlist__first(evlist);

	TEST_ASSERT_VAL("wrong number of entries", 1 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config",    10 == evsel->core.attr.config);
	TEST_ASSERT_VAL("wrong config1",    1 == evsel->core.attr.config1);
	TEST_ASSERT_VAL("wrong config2",    3 == evsel->core.attr.config2);
	TEST_ASSERT_VAL("wrong config3",    0 == evsel->core.attr.config3);
	/*
	 * The period value gets configured within evlist__config,
	 * while this test executes only parse events method.
	 */
	TEST_ASSERT_VAL("wrong period",     0 == evsel->core.attr.sample_period);

	return TEST_OK;
}

static int test__hybrid_hw_group_event_2(struct evlist *evlist)
{
	struct evsel *evsel, *leader;

	evsel = leader = evlist__first(evlist);
	TEST_ASSERT_VAL("wrong number of entries", 2 == evlist->core.nr_entries);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_HARDWARE == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong hybrid type", test_hybrid_type(evsel, PERF_TYPE_RAW));
	TEST_ASSERT_VAL("wrong config", test_config(evsel, PERF_COUNT_HW_CPU_CYCLES));
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));

	evsel = evsel__next(evsel);
	TEST_ASSERT_VAL("wrong type", PERF_TYPE_RAW == evsel->core.attr.type);
	TEST_ASSERT_VAL("wrong config", evsel->core.attr.config == 0x3c);
	TEST_ASSERT_VAL("wrong leader", evsel__has_leader(evsel, leader));
	return TEST_OK;
}

struct evlist_test {
	const char *name;
	bool (*valid)(void);
	int (*check)(struct evlist *evlist);
};

static const struct evlist_test test__hybrid_events[] = {
	{
		.name  = "cpu_core/cycles/",
		.check = test__hybrid_hw_event_with_pmu,
		/* 0 */
	},
	{
		.name  = "{cpu_core/cycles/,cpu_core/branches/}",
		.check = test__hybrid_hw_group_event,
		/* 1 */
	},
	{
		.name  = "{cpu-clock,cpu_core/cycles/}",
		.check = test__hybrid_sw_hw_group_event,
		/* 2 */
	},
	{
		.name  = "{cpu_core/cycles/,cpu-clock}",
		.check = test__hybrid_hw_sw_group_event,
		/* 3 */
	},
	{
		.name  = "{cpu_core/cycles/k,cpu_core/branches/u}",
		.check = test__hybrid_group_modifier1,
		/* 4 */
	},
	{
		.name  = "r1a",
		.check = test__hybrid_raw1,
		/* 5 */
	},
	{
		.name  = "cpu_core/r1a/",
		.check = test__hybrid_raw2,
		/* 6 */
	},
	{
		.name  = "cpu_core/config=10,config1,config2=3,period=1000/u",
		.check = test__checkevent_pmu,
		/* 7 */
	},
	{
		.name  = "cpu_core/LLC-loads/",
		.check = test__hybrid_cache_event,
		/* 8 */
	},
	{
		.name  = "{cpu_core/cycles/,cpu_core/cpu-cycles/}",
		.check = test__hybrid_hw_group_event_2,
		/* 9 */
	},
};

static int test_event(const struct evlist_test *e)
{
	struct parse_events_error err;
	struct evlist *evlist;
	int ret;

	if (e->valid && !e->valid()) {
		pr_debug("... SKIP\n");
		return TEST_OK;
	}

	evlist = evlist__new();
	if (evlist == NULL) {
		pr_err("Failed allocation");
		return TEST_FAIL;
	}
	parse_events_error__init(&err);
	ret = parse_events(evlist, e->name, &err);
	if (ret) {
		pr_debug("failed to parse event '%s', err %d, str '%s'\n",
			 e->name, ret, err.str);
		parse_events_error__print(&err, e->name);
		ret = TEST_FAIL;
		if (strstr(err.str, "can't access trace events"))
			ret = TEST_SKIP;
	} else {
		ret = e->check(evlist);
	}
	parse_events_error__exit(&err);
	evlist__delete(evlist);

	return ret;
}

static int combine_test_results(int existing, int latest)
{
	if (existing == TEST_FAIL)
		return TEST_FAIL;
	if (existing == TEST_SKIP)
		return latest == TEST_OK ? TEST_SKIP : latest;
	return latest;
}

static int test_events(const struct evlist_test *events, int cnt)
{
	int ret = TEST_OK;

	for (int i = 0; i < cnt; i++) {
		const struct evlist_test *e = &events[i];
		int test_ret;

		pr_debug("running test %d '%s'\n", i, e->name);
		test_ret = test_event(e);
		if (test_ret != TEST_OK) {
			pr_debug("Event test failure: test %d '%s'", i, e->name);
			ret = combine_test_results(ret, test_ret);
		}
	}

	return ret;
}

int test__hybrid(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	if (perf_pmus__num_core_pmus() == 1)
		return TEST_SKIP;

	return test_events(test__hybrid_events, ARRAY_SIZE(test__hybrid_events));
}
