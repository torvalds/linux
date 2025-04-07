// SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause)
#include "debug.h"
#include "evlist.h"
#include "parse-events.h"
#include "tests.h"
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
	if (with_pmu ? (evlist->core.nr_entries != 1) : (evlist->core.nr_entries < 1)) {
		pr_debug("FAILED %s:%d Unexpected number of events for '%s' of %d\n",
			 __FILE__, __LINE__, str, evlist->core.nr_entries);
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
	evlist__delete(evlist);
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

static struct test_case tests__tool_pmu[] = {
	TEST_CASE("Parsing without PMU name", tool_pmu_without_pmu),
	TEST_CASE("Parsing with PMU name", tool_pmu_with_pmu),
	{	.name = NULL, }
};

struct test_suite suite__tool_pmu = {
	.desc = "Tool PMU",
	.test_cases = tests__tool_pmu,
};
