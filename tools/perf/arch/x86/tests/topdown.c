// SPDX-License-Identifier: GPL-2.0
#include <errno.h>
#include "arch-tests.h"
#include "../util/topdown.h"
#include "debug.h"
#include "evlist.h"
#include "parse-events.h"
#include "pmu.h"
#include "pmus.h"

static int event_cb(void *state, struct pmu_event_info *info)
{
	char buf[256];
	struct parse_events_error parse_err;
	int *ret = state, err;
	struct evlist *evlist = evlist__new();
	struct evsel *evsel;

	if (!evlist)
		return -ENOMEM;

	parse_events_error__init(&parse_err);
	snprintf(buf, sizeof(buf), "%s/%s/", info->pmu->name, info->name);
	err = parse_events(evlist, buf, &parse_err);
	if (err) {
		parse_events_error__print(&parse_err, buf);
		*ret = TEST_FAIL;
	}
	parse_events_error__exit(&parse_err);
	evlist__for_each_entry(evlist, evsel) {
		bool fail = false;
		bool p_core_pmu = evsel->pmu->type == PERF_TYPE_RAW;
		const char *name = evsel__name(evsel);

		if (strcasestr(name, "uops_retired.slots") ||
		    strcasestr(name, "topdown.backend_bound_slots") ||
		    strcasestr(name, "topdown.br_mispredict_slots") ||
		    strcasestr(name, "topdown.memory_bound_slots") ||
		    strcasestr(name, "topdown.bad_spec_slots") ||
		    strcasestr(name, "topdown.slots_p")) {
			if (arch_is_topdown_slots(evsel) || arch_is_topdown_metrics(evsel))
				fail = true;
		} else if (strcasestr(name, "slots")) {
			if (arch_is_topdown_slots(evsel) != p_core_pmu ||
			    arch_is_topdown_metrics(evsel))
				fail = true;
		} else if (strcasestr(name, "topdown")) {
			if (arch_is_topdown_slots(evsel) ||
			    arch_is_topdown_metrics(evsel) != p_core_pmu)
				fail = true;
		} else if (arch_is_topdown_slots(evsel) || arch_is_topdown_metrics(evsel)) {
			fail = true;
		}
		if (fail) {
			pr_debug("Broken topdown information for '%s'\n", evsel__name(evsel));
			*ret = TEST_FAIL;
		}
	}
	evlist__delete(evlist);
	return 0;
}

static int test__x86_topdown(struct test_suite *test __maybe_unused, int subtest __maybe_unused)
{
	int ret = TEST_OK;
	struct perf_pmu *pmu = NULL;

	if (!topdown_sys_has_perf_metrics())
		return TEST_OK;

	while ((pmu = perf_pmus__scan_core(pmu)) != NULL) {
		if (perf_pmu__for_each_event(pmu, /*skip_duplicate_pmus=*/false, &ret, event_cb))
			break;
	}
	return ret;
}

#define CHECK_COND(cond, text)					\
do {								\
	if (!(cond)) {						\
		pr_debug("FAILED %s:%d %s\n", __FILE__, __LINE__, text); \
		ret = TEST_FAIL;				\
		goto out_err;					\
	}							\
} while (0)

#define CHECK_EQUAL(val, expected, text)			\
do {								\
	if ((val) != (expected)) {				\
		pr_debug("FAILED %s:%d %s (%d != %d)\n",	\
			 __FILE__, __LINE__, text, (val), (expected)); \
		ret = TEST_FAIL;				\
		goto out_err;					\
	}							\
} while (0)

static int test_sort(const char *str, int expected_slots_group_size,
		     int expected_instructions_group_size)
{
	struct evlist *evlist = NULL;
	struct parse_events_error err;
	struct evsel *evsel;
	int ret = TEST_FAIL;
	bool slots_seen = false;

	parse_events_error__init(&err);

	evlist = evlist__new();
	if (!evlist)
		goto out_err;

	if (parse_events(evlist, str, &err)) {
		pr_debug("parse_events failed for %s\n", str);
		goto out_err;
	}

	evlist__for_each_entry(evlist, evsel) {
		if (!evsel__is_group_leader(evsel))
			continue;

		if (strstr(evsel__name(evsel), "slots")) {
			/*
			 * Slots as a leader means the PMU is for a perf metric
			 * group as the slots event isn't present when not.
			 */
			slots_seen = true;
			CHECK_EQUAL(evsel->core.nr_members, expected_slots_group_size,
				    "slots group size");
			if (expected_slots_group_size == 3) {
				struct evsel *next = evsel__next(evsel);
				struct evsel *next2 = evsel__next(next);

				CHECK_COND(strstr(evsel__name(next), "instructions") != NULL,
					   "slots second event is instructions");
				CHECK_COND(strstr(evsel__name(next2), "topdown-retiring") != NULL,
					   "slots third event is topdown-retiring");
			} else if (expected_slots_group_size == 2) {
				struct evsel *next = evsel__next(evsel);

				CHECK_COND(strstr(evsel__name(next), "topdown-retiring") != NULL,
					   "slots second event is topdown-retiring");
			}
		} else if (strstr(evsel__name(evsel), "instructions")) {
			CHECK_EQUAL(evsel->core.nr_members, expected_instructions_group_size,
				    "instructions group size");
			if (expected_instructions_group_size == 2) {
				/*
				 * On Intel hybrid CPUs (e.g., Alder Lake/
				 * Raptor Lake), E-cores (cpu_atom) do not
				 * support/enforce the slots event. When
				 * parsing event groups containing slots
				 * across all PMUs, slots is automatically
				 * filtered out from cpu_atom, leaving
				 * {cpu_atom/instructions/,
				 *  cpu_atom/topdown-retiring/}. On cpu_atom,
				 * instructions correctly leads this group of
				 * 2 without slots reordering.
				 */
				struct evsel *next = evsel__next(evsel);

				CHECK_COND(strstr(evsel__name(next), "topdown-retiring") != NULL,
					   "instructions second event is topdown-retiring");
			}
		} else if (strstr(evsel__name(evsel), "topdown-retiring")) {
			/*
			 * A perf metric event where the PMU doesn't require
			 * slots as a leader.
			 */
			CHECK_EQUAL(evsel->core.nr_members, 1, "topdown-retiring group size");
		} else if (strstr(evsel__name(evsel), "cycles")) {
			CHECK_EQUAL(evsel->core.nr_members, 1, "cycles group size");
		}
	}
	CHECK_COND(slots_seen, "slots seen");
	ret = TEST_OK;
out_err:
	evlist__delete(evlist);
	parse_events_error__exit(&err);
	return ret;
}

static int test__x86_topdown_sorting(struct test_suite *test __maybe_unused,
				     int subtest __maybe_unused)
{
	int ret;

	if (!topdown_sys_has_perf_metrics())
		return TEST_OK;

	ret = test_sort("{instructions,topdown-retiring,slots}", 3, 2);
	TEST_ASSERT_EQUAL("all events in a group", ret, TEST_OK);
	ret = test_sort("instructions,topdown-retiring,slots", 2, 1);
	TEST_ASSERT_EQUAL("all events not in a group", ret, TEST_OK);
	ret = test_sort("{instructions,slots},topdown-retiring", 2, 1);
	TEST_ASSERT_EQUAL("slots event in a group but topdown metrics events outside the group",
			  ret, TEST_OK);
	ret = test_sort("{instructions,slots},{topdown-retiring}", 2, 1);
	TEST_ASSERT_EQUAL("slots event and topdown metrics events in two groups",
			  ret, TEST_OK);
	ret = test_sort("{instructions,slots},cycles,topdown-retiring", 2, 1);
	TEST_ASSERT_EQUAL("slots event and metrics event are not in a group and not adjacent",
			  ret, TEST_OK);

	return TEST_OK;
}

static int test__x86_topdown_slots_injection(struct test_suite *test __maybe_unused,
					     int subtest __maybe_unused)
{
	int ret;

	if (!topdown_sys_has_perf_metrics())
		return TEST_OK;

	ret = test_sort("{instructions,topdown-retiring}", 3, 2);
	TEST_ASSERT_EQUAL("all events in a group", ret, TEST_OK);
	ret = test_sort("instructions,topdown-retiring", 2, 1);
	TEST_ASSERT_EQUAL("all events not in a group", ret, TEST_OK);
	ret = test_sort("{instructions},topdown-retiring", 2, 1);
	TEST_ASSERT_EQUAL("event in a group but topdown metrics events outside the group",
			  ret, TEST_OK);
	ret = test_sort("{instructions},{topdown-retiring}", 2, 1);
	TEST_ASSERT_EQUAL("event and topdown metrics events in two groups",
			  ret, TEST_OK);
	ret = test_sort("{instructions},cycles,topdown-retiring", 2, 1);
	TEST_ASSERT_EQUAL("event and metrics event are not in a group and not adjacent",
			  ret, TEST_OK);

	return TEST_OK;
}

static struct test_case x86_topdown_tests[] = {
	TEST_CASE("topdown events", x86_topdown),
	TEST_CASE("topdown sorting", x86_topdown_sorting),
	TEST_CASE("topdown slots injection", x86_topdown_slots_injection),
	{ .name = NULL, }
};

struct test_suite suite__x86_topdown = {
	.desc = "x86 topdown",
	.test_cases = x86_topdown_tests,
};
