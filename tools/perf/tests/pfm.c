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
static int test__pfm_events(void);
static int test__pfm_group(void);
#endif

static const struct {
	int (*func)(void);
	const char *desc;
} pfm_testcase_table[] = {
#ifdef HAVE_LIBPFM
	{
		.func = test__pfm_events,
		.desc = "test of individual --pfm-events",
	},
	{
		.func = test__pfm_group,
		.desc = "test groups of --pfm-events",
	},
#endif
};

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

static int test__pfm_events(void)
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
				evlist->core.nr_groups,
				0);

		evlist__delete(evlist);
	}
	return 0;
}

static int test__pfm_group(void)
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
			.nr_groups = 1,
		},
		{
			.events = "{instructions},{}",
			.nr_events = 1,
			.nr_groups = 1,
		},
		{
			.events = "{},{instructions}",
			.nr_events = 1,
			.nr_groups = 1,
		},
		{
			.events = "{instructions},{instructions}",
			.nr_events = 2,
			.nr_groups = 2,
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
				evlist->core.nr_groups,
				table[i].nr_groups);

		evlist__delete(evlist);
	}
	return 0;
}
#endif

const char *test__pfm_subtest_get_desc(int i)
{
	if (i < 0 || i >= (int)ARRAY_SIZE(pfm_testcase_table))
		return NULL;
	return pfm_testcase_table[i].desc;
}

int test__pfm_subtest_get_nr(void)
{
	return (int)ARRAY_SIZE(pfm_testcase_table);
}

int test__pfm(struct test *test __maybe_unused, int i __maybe_unused)
{
#ifdef HAVE_LIBPFM
	if (i < 0 || i >= (int)ARRAY_SIZE(pfm_testcase_table))
		return TEST_FAIL;
	return pfm_testcase_table[i].func();
#else
	return TEST_SKIP;
#endif
}
