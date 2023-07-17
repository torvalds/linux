// SPDX-License-Identifier: GPL-2.0
/*
 * Test support for libpfm4 event encodings.
 *
 * Copyright 2020 Google LLC.
 */
#include "tests.h"
#include "util/debug.h"
#include "util/evlist.h"
#include "util/pfm.h"

#include <linux/kernel.h>

#ifdef HAVE_LIBPFM
static int count_pfm_events(struct perf_evlist *evlist)
{
	struct perf_evsel *evsel;
	int count = 0;

	perf_evlist__for_each_entry(evlist, evsel) {
		count++;
	}
	return count;
}

static int test__pfm_events(struct test_suite *test __maybe_unused,
			    int subtest __maybe_unused)
{
	struct evlist *evlist;
	struct option opt;
	size_t i;
	const struct {
		const char *events;
		int nr_events;
	} table[] = {
		{
			.events = "",
			.nr_events = 0,
		},
		{
			.events = "instructions",
			.nr_events = 1,
		},
		{
			.events = "instructions,cycles",
			.nr_events = 2,
		},
		{
			.events = "stereolab",
			.nr_events = 0,
		},
		{
			.events = "instructions,instructions",
			.nr_events = 2,
		},
		{
			.events = "stereolab,instructions",
			.nr_events = 0,
		},
		{
			.events = "instructions,stereolab",
			.nr_events = 1,
		},
	};

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		evlist = evlist__new();
		if (evlist == NULL)
			return -ENOMEM;

		opt.value = evlist;
		parse_libpfm_events_option(&opt,
					table[i].events,
					0);
		TEST_ASSERT_EQUAL(table[i].events,
				count_pfm_events(&evlist->core),
				table[i].nr_events);
		TEST_ASSERT_EQUAL(table[i].events,
				evlist__nr_groups(evlist),
				0);

		evlist__delete(evlist);
	}
	return 0;
}

static int test__pfm_group(struct test_suite *test __maybe_unused,
			   int subtest __maybe_unused)
{
	struct evlist *evlist;
	struct option opt;
	size_t i;
	const struct {
		const char *events;
		int nr_events;
		int nr_groups;
	} table[] = {
		{
			.events = "{},",
			.nr_events = 0,
			.nr_groups = 0,
		},
		{
			.events = "{instructions}",
			.nr_events = 1,
			.nr_groups = 0,
		},
		{
			.events = "{instructions},{}",
			.nr_events = 1,
			.nr_groups = 0,
		},
		{
			.events = "{},{instructions}",
			.nr_events = 1,
			.nr_groups = 0,
		},
		{
			.events = "{instructions},{instructions}",
			.nr_events = 2,
			.nr_groups = 0,
		},
		{
			.events = "{instructions,cycles},{instructions,cycles}",
			.nr_events = 4,
			.nr_groups = 2,
		},
		{
			.events = "{stereolab}",
			.nr_events = 0,
			.nr_groups = 0,
		},
		{
			.events =
			"{instructions,cycles},{instructions,stereolab}",
			.nr_events = 3,
			.nr_groups = 1,
		},
		{
			.events = "instructions}",
			.nr_events = 1,
			.nr_groups = 0,
		},
		{
			.events = "{{instructions}}",
			.nr_events = 0,
			.nr_groups = 0,
		},
	};

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		evlist = evlist__new();
		if (evlist == NULL)
			return -ENOMEM;

		opt.value = evlist;
		parse_libpfm_events_option(&opt,
					table[i].events,
					0);
		TEST_ASSERT_EQUAL(table[i].events,
				count_pfm_events(&evlist->core),
				table[i].nr_events);
		TEST_ASSERT_EQUAL(table[i].events,
				evlist__nr_groups(evlist),
				table[i].nr_groups);

		evlist__delete(evlist);
	}
	return 0;
}
#else
static int test__pfm_events(struct test_suite *test __maybe_unused,
			    int subtest __maybe_unused)
{
	return TEST_SKIP;
}

static int test__pfm_group(struct test_suite *test __maybe_unused,
			   int subtest __maybe_unused)
{
	return TEST_SKIP;
}
#endif

static struct test_case pfm_tests[] = {
	TEST_CASE_REASON("test of individual --pfm-events", pfm_events, "not compiled in"),
	TEST_CASE_REASON("test groups of --pfm-events", pfm_group, "not compiled in"),
	{ .name = NULL, }
};

struct test_suite suite__pfm = {
	.desc = "Test libpfm4 support",
	.test_cases = pfm_tests,
};
