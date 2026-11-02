// SPDX-License-Identifier: GPL-2.0
#include <stdlib.h>
#include <string.h>
#include <linux/perf_event.h>
#include "debug.h"
#include "env.h"
#include "evlist.h"
#include "evsel.h"
#include "tests.h"

/* A hybrid machine with 2 core PMUs, as read from a perf.data file. */
static struct hybrid_node two_core_pmus[] = {
	{ .pmu_name = (char *)"cpu_atom", .cpus = (char *)"0-3", },
	{ .pmu_name = (char *)"cpu_core", .cpus = (char *)"4-7", },
};

/* A machine with more than 2 kinds of core, like some Arm big.LITTLE. */
static struct hybrid_node three_core_pmus[] = {
	{ .pmu_name = (char *)"cpu_atom", .cpus = (char *)"0-3", },
	{ .pmu_name = (char *)"cpu_core", .cpus = (char *)"4-7", },
	{ .pmu_name = (char *)"cpu_lowpower", .cpus = (char *)"8", },
};

static struct evsel *test_evsel__new(struct evlist *evlist, const char *name)
{
	struct perf_event_attr attr = {
		.type = PERF_TYPE_RAW,
		.size = sizeof(attr),
		.config = 0x3c,
	};
	struct evsel *evsel = evsel__new(&attr);

	if (!evsel)
		return NULL;

	evsel->name = strdup(name);
	if (!evsel->name) {
		evsel__put(evsel);
		return NULL;
	}
	evlist__add(evlist, evsel);
	return evsel;
}

/*
 * The assertions are in helpers taking an already allocated evlist, so that the
 * caller can release the evlist however an assertion fails.
 */
static int check_merge_events(struct evlist *evlist, struct perf_env *env)
{
	struct evsel *atom_cycles, *core_cycles, *atom_insns, *core_insns, *pos;

	/* As if "perf record -e cycles,instructions" ran on a hybrid machine. */
	atom_cycles = test_evsel__new(evlist, "cpu_atom/cycles/");
	core_cycles = test_evsel__new(evlist, "cpu_core/cycles/");
	atom_insns = test_evsel__new(evlist, "cpu_atom/instructions/");
	core_insns = test_evsel__new(evlist, "cpu_core/instructions/");
	TEST_ASSERT_VAL("failed to allocate evsels",
			atom_cycles && core_cycles && atom_insns && core_insns);

	TEST_ASSERT_VAL("events should be mergeable",
			evlist__can_merge_hybrid(evlist, env));

	/* Testing for merging must not alter the evlist. */
	evlist__for_each_entry(evlist, pos) {
		TEST_ASSERT_VAL("evlist modified by evlist__can_merge_hybrid",
				!pos->first_wildcard_match);
	}

	evlist__merge_hybrid(evlist, env);

	TEST_ASSERT_VAL("cycles not merged",
			core_cycles->first_wildcard_match == atom_cycles);
	/* All the events must be merged, not just the first pair found. */
	TEST_ASSERT_VAL("instructions not merged",
			core_insns->first_wildcard_match == atom_insns);
	TEST_ASSERT_VAL("wrong cycles leader",
			evsel__leader(core_cycles) == atom_cycles);
	TEST_ASSERT_VAL("wrong instructions leader",
			evsel__leader(core_insns) == atom_insns);
	TEST_ASSERT_VAL("wrong cycles group size", atom_cycles->core.nr_members == 2);
	TEST_ASSERT_VAL("wrong instructions group size", atom_insns->core.nr_members == 2);

	return TEST_OK;
}

static int test__hybrid_merge_events(struct test_suite *test __maybe_unused,
				     int subtest __maybe_unused)
{
	struct perf_env env = {
		.nr_hybrid_nodes = ARRAY_SIZE(two_core_pmus),
		.hybrid_nodes = two_core_pmus,
	};
	struct evlist *evlist = evlist__new();
	int ret;

	TEST_ASSERT_VAL("failed to allocate evlist", evlist);

	ret = check_merge_events(evlist, &env);
	evlist__put(evlist);
	return ret;
}

static int check_merge_3_core_pmus(struct evlist *evlist, struct perf_env *env)
{
	struct evsel *atom_cycles, *core_cycles, *lowpower_cycles;

	atom_cycles = test_evsel__new(evlist, "cpu_atom/cycles/");
	core_cycles = test_evsel__new(evlist, "cpu_core/cycles/");
	lowpower_cycles = test_evsel__new(evlist, "cpu_lowpower/cycles/");
	TEST_ASSERT_VAL("failed to allocate evsels",
			atom_cycles && core_cycles && lowpower_cycles);

	evlist__merge_hybrid(evlist, env);

	TEST_ASSERT_VAL("second core PMU not merged",
			core_cycles->first_wildcard_match == atom_cycles);
	TEST_ASSERT_VAL("third core PMU not merged",
			lowpower_cycles->first_wildcard_match == atom_cycles);
	TEST_ASSERT_VAL("wrong group size", atom_cycles->core.nr_members == 3);

	return TEST_OK;
}

static int test__hybrid_merge_3_core_pmus(struct test_suite *test __maybe_unused,
					  int subtest __maybe_unused)
{
	struct perf_env env = {
		.nr_hybrid_nodes = ARRAY_SIZE(three_core_pmus),
		.hybrid_nodes = three_core_pmus,
	};
	struct evlist *evlist = evlist__new();
	int ret;

	TEST_ASSERT_VAL("failed to allocate evlist", evlist);

	ret = check_merge_3_core_pmus(evlist, &env);
	evlist__put(evlist);
	return ret;
}

static int check_unmergeable_core_events(struct evlist *evlist, struct perf_env *env)
{
	/* A single event has nothing to merge with. */
	TEST_ASSERT_VAL("failed to allocate evsel",
			test_evsel__new(evlist, "cpu_core/cycles/"));
	TEST_ASSERT_VAL("a single event shouldn't merge",
			!evlist__can_merge_hybrid(evlist, env));

	/* Events of different names shouldn't merge. */
	TEST_ASSERT_VAL("failed to allocate evsel",
			test_evsel__new(evlist, "cpu_atom/instructions/"));
	TEST_ASSERT_VAL("events with different names shouldn't merge",
			!evlist__can_merge_hybrid(evlist, env));

	return TEST_OK;
}

static int check_unmergeable_uncore_events(struct evlist *evlist, struct perf_env *env)
{
	/* Matching events on non-core PMUs shouldn't merge. */
	TEST_ASSERT_VAL("failed to allocate evsels",
			test_evsel__new(evlist, "uncore_imc_0/clockticks/") &&
			test_evsel__new(evlist, "uncore_imc_1/clockticks/"));
	TEST_ASSERT_VAL("uncore events shouldn't merge",
			!evlist__can_merge_hybrid(evlist, env));

	return TEST_OK;
}

static int test__hybrid_merge_unmergeable(struct test_suite *test __maybe_unused,
					  int subtest __maybe_unused)
{
	struct perf_env env = {
		.nr_hybrid_nodes = ARRAY_SIZE(two_core_pmus),
		.hybrid_nodes = two_core_pmus,
	};
	struct evlist *evlist = evlist__new();
	int ret;

	TEST_ASSERT_VAL("failed to allocate evlist", evlist);

	ret = check_unmergeable_core_events(evlist, &env);
	evlist__put(evlist);
	if (ret != TEST_OK)
		return ret;

	evlist = evlist__new();
	TEST_ASSERT_VAL("failed to allocate evlist", evlist);

	ret = check_unmergeable_uncore_events(evlist, &env);
	evlist__put(evlist);
	return ret;
}

static struct test_case tests__hybrid_merge[] = {
	TEST_CASE("Merge events of 2 core PMUs", hybrid_merge_events),
	TEST_CASE("Merge events of 3 core PMUs", hybrid_merge_3_core_pmus),
	TEST_CASE("Events that shouldn't merge", hybrid_merge_unmergeable),
	{	.name = NULL, }
};

struct test_suite suite__hybrid_merge = {
	.desc = "Hybrid event merging",
	.test_cases = tests__hybrid_merge,
};
