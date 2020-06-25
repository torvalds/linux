// SPDX-License-Identifier: GPL-2.0
#include "math.h"
#include "parse-events.h"
#include "pmu.h"
#include "tests.h"
#include <errno.h>
#include <stdio.h>
#include <linux/kernel.h>
#include <linux/zalloc.h>
#include "debug.h"
#include "../pmu-events/pmu-events.h"
#include "util/evlist.h"
#include "util/expr.h"
#include "util/parse-events.h"

struct perf_pmu_test_event {
	struct pmu_event event;

	/* extra events for aliases */
	const char *alias_str;

	/*
	 * Note: For when PublicDescription does not exist in the JSON, we
	 * will have no long_desc in pmu_event.long_desc, but long_desc may
	 * be set in the alias.
	 */
	const char *alias_long_desc;
};

static struct perf_pmu_test_event test_cpu_events[] = {
	{
		.event = {
			.name = "bp_l1_btb_correct",
			.event = "event=0x8a",
			.desc = "L1 BTB Correction",
			.topic = "branch",
		},
		.alias_str = "event=0x8a",
		.alias_long_desc = "L1 BTB Correction",
	},
	{
		.event = {
			.name = "bp_l2_btb_correct",
			.event = "event=0x8b",
			.desc = "L2 BTB Correction",
			.topic = "branch",
		},
		.alias_str = "event=0x8b",
		.alias_long_desc = "L2 BTB Correction",
	},
	{
		.event = {
			.name = "segment_reg_loads.any",
			.event = "umask=0x80,period=200000,event=0x6",
			.desc = "Number of segment register loads",
			.topic = "other",
		},
		.alias_str = "umask=0x80,(null)=0x30d40,event=0x6",
		.alias_long_desc = "Number of segment register loads",
	},
	{
		.event = {
			.name = "dispatch_blocked.any",
			.event = "umask=0x20,period=200000,event=0x9",
			.desc = "Memory cluster signals to block micro-op dispatch for any reason",
			.topic = "other",
		},
		.alias_str = "umask=0x20,(null)=0x30d40,event=0x9",
		.alias_long_desc = "Memory cluster signals to block micro-op dispatch for any reason",
	},
	{
		.event = {
			.name = "eist_trans",
			.event = "umask=0x0,period=200000,event=0x3a",
			.desc = "Number of Enhanced Intel SpeedStep(R) Technology (EIST) transitions",
			.topic = "other",
		},
		.alias_str = "umask=0,(null)=0x30d40,event=0x3a",
		.alias_long_desc = "Number of Enhanced Intel SpeedStep(R) Technology (EIST) transitions",
	},
	{ /* sentinel */
		.event = {
			.name = NULL,
		},
	},
};

static struct perf_pmu_test_event test_uncore_events[] = {
	{
		.event = {
			.name = "uncore_hisi_ddrc.flux_wcmd",
			.event = "event=0x2",
			.desc = "DDRC write commands. Unit: hisi_sccl,ddrc ",
			.topic = "uncore",
			.long_desc = "DDRC write commands",
			.pmu = "hisi_sccl,ddrc",
		},
		.alias_str = "event=0x2",
		.alias_long_desc = "DDRC write commands",
	},
	{
		.event = {
			.name = "unc_cbo_xsnp_response.miss_eviction",
			.event = "umask=0x81,event=0x22",
			.desc = "Unit: uncore_cbox A cross-core snoop resulted from L3 Eviction which misses in some processor core",
			.topic = "uncore",
			.long_desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core",
			.pmu = "uncore_cbox",
		},
		.alias_str = "umask=0x81,event=0x22",
		.alias_long_desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core",
	},
	{ /* sentinel */
		.event = {
			.name = NULL,
		},
	}
};

const int total_test_events_size = ARRAY_SIZE(test_uncore_events);

static bool is_same(const char *reference, const char *test)
{
	if (!reference && !test)
		return true;

	if (reference && !test)
		return false;

	if (!reference && test)
		return false;

	return !strcmp(reference, test);
}

static struct pmu_events_map *__test_pmu_get_events_map(void)
{
	struct pmu_events_map *map;

	for (map = &pmu_events_map[0]; map->cpuid; map++) {
		if (!strcmp(map->cpuid, "testcpu"))
			return map;
	}

	pr_err("could not find test events map\n");

	return NULL;
}

/* Verify generated events from pmu-events.c is as expected */
static int test_pmu_event_table(void)
{
	struct pmu_events_map *map = __test_pmu_get_events_map();
	struct pmu_event *table;
	int map_events = 0, expected_events;

	/* ignore 2x sentinels */
	expected_events = ARRAY_SIZE(test_cpu_events) +
			  ARRAY_SIZE(test_uncore_events) - 2;

	if (!map)
		return -1;

	for (table = map->table; table->name; table++) {
		struct perf_pmu_test_event *test;
		struct pmu_event *te;
		bool found = false;

		if (table->pmu)
			test = &test_uncore_events[0];
		else
			test = &test_cpu_events[0];

		te = &test->event;

		for (; te->name; test++, te = &test->event) {
			if (strcmp(table->name, te->name))
				continue;
			found = true;
			map_events++;

			if (!is_same(table->desc, te->desc)) {
				pr_debug2("testing event table %s: mismatched desc, %s vs %s\n",
					  table->name, table->desc, te->desc);
				return -1;
			}

			if (!is_same(table->topic, te->topic)) {
				pr_debug2("testing event table %s: mismatched topic, %s vs %s\n",
					  table->name, table->topic,
					  te->topic);
				return -1;
			}

			if (!is_same(table->long_desc, te->long_desc)) {
				pr_debug2("testing event table %s: mismatched long_desc, %s vs %s\n",
					  table->name, table->long_desc,
					  te->long_desc);
				return -1;
			}

			if (!is_same(table->unit, te->unit)) {
				pr_debug2("testing event table %s: mismatched unit, %s vs %s\n",
					  table->name, table->unit,
					  te->unit);
				return -1;
			}

			if (!is_same(table->perpkg, te->perpkg)) {
				pr_debug2("testing event table %s: mismatched perpkg, %s vs %s\n",
					  table->name, table->perpkg,
					  te->perpkg);
				return -1;
			}

			if (!is_same(table->metric_expr, te->metric_expr)) {
				pr_debug2("testing event table %s: mismatched metric_expr, %s vs %s\n",
					  table->name, table->metric_expr,
					  te->metric_expr);
				return -1;
			}

			if (!is_same(table->metric_name, te->metric_name)) {
				pr_debug2("testing event table %s: mismatched metric_name, %s vs %s\n",
					  table->name,  table->metric_name,
					  te->metric_name);
				return -1;
			}

			if (!is_same(table->deprecated, te->deprecated)) {
				pr_debug2("testing event table %s: mismatched deprecated, %s vs %s\n",
					  table->name, table->deprecated,
					  te->deprecated);
				return -1;
			}

			pr_debug("testing event table %s: pass\n", table->name);
		}

		if (!found) {
			pr_err("testing event table: could not find event %s\n",
			       table->name);
			return -1;
		}
	}

	if (map_events != expected_events) {
		pr_err("testing event table: found %d, but expected %d\n",
		       map_events, expected_events);
		return -1;
	}

	return 0;
}

static struct perf_pmu_alias *find_alias(const char *test_event, struct list_head *aliases)
{
	struct perf_pmu_alias *alias;

	list_for_each_entry(alias, aliases, list)
		if (!strcmp(test_event, alias->name))
			return alias;

	return NULL;
}

/* Verify aliases are as expected */
static int __test__pmu_event_aliases(char *pmu_name, int *count)
{
	struct perf_pmu_test_event *test;
	struct pmu_event *te;
	struct perf_pmu *pmu;
	LIST_HEAD(aliases);
	int res = 0;
	bool use_uncore_table;
	struct pmu_events_map *map = __test_pmu_get_events_map();

	if (!map)
		return -1;

	if (is_pmu_core(pmu_name)) {
		test = &test_cpu_events[0];
		use_uncore_table = false;
	} else {
		test = &test_uncore_events[0];
		use_uncore_table = true;
	}

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return -1;

	pmu->name = pmu_name;

	pmu_add_cpu_aliases_map(&aliases, pmu, map);

	for (te = &test->event; te->name; test++, te = &test->event) {
		struct perf_pmu_alias *alias = find_alias(te->name, &aliases);

		if (!alias) {
			bool uncore_match = pmu_uncore_alias_match(pmu_name,
								   te->pmu);

			if (use_uncore_table && !uncore_match) {
				pr_debug3("testing aliases PMU %s: skip matching alias %s\n",
					  pmu_name, te->name);
				continue;
			}

			pr_debug2("testing aliases PMU %s: no alias, alias_table->name=%s\n",
				  pmu_name, te->name);
			res = -1;
			break;
		}

		if (!is_same(alias->desc, te->desc)) {
			pr_debug2("testing aliases PMU %s: mismatched desc, %s vs %s\n",
				  pmu_name, alias->desc, te->desc);
			res = -1;
			break;
		}

		if (!is_same(alias->long_desc, test->alias_long_desc)) {
			pr_debug2("testing aliases PMU %s: mismatched long_desc, %s vs %s\n",
				  pmu_name, alias->long_desc,
				  test->alias_long_desc);
			res = -1;
			break;
		}

		if (!is_same(alias->str, test->alias_str)) {
			pr_debug2("testing aliases PMU %s: mismatched str, %s vs %s\n",
				  pmu_name, alias->str, test->alias_str);
			res = -1;
			break;
		}

		if (!is_same(alias->topic, te->topic)) {
			pr_debug2("testing aliases PMU %s: mismatched topic, %s vs %s\n",
				  pmu_name, alias->topic, te->topic);
			res = -1;
			break;
		}

		(*count)++;
		pr_debug2("testing aliases PMU %s: matched event %s\n",
			  pmu_name, alias->name);
	}

	free(pmu);
	return res;
}


static int test_aliases(void)
{
	struct perf_pmu *pmu = NULL;

	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		int count = 0;

		if (list_empty(&pmu->format)) {
			pr_debug2("skipping testing PMU %s\n", pmu->name);
			continue;
		}

		if (__test__pmu_event_aliases(pmu->name, &count)) {
			pr_debug("testing PMU %s aliases: failed\n", pmu->name);
			return -1;
		}

		if (count == 0)
			pr_debug3("testing PMU %s aliases: no events to match\n",
				  pmu->name);
		else
			pr_debug("testing PMU %s aliases: pass\n", pmu->name);
	}

	return 0;
}

static bool is_number(const char *str)
{
	char *end_ptr;
	double v;

	errno = 0;
	v = strtod(str, &end_ptr);
	(void)v; // We're not interested in this value, only if it is valid
	return errno == 0 && end_ptr != str;
}

static int check_parse_id(const char *id, bool same_cpu, struct pmu_event *pe)
{
	struct parse_events_error error;
	struct evlist *evlist;
	int ret;

	/* Numbers are always valid. */
	if (is_number(id))
		return 0;

	evlist = evlist__new();
	memset(&error, 0, sizeof(error));
	ret = parse_events(evlist, id, &error);
	if (ret && same_cpu) {
		pr_warning("Parse event failed metric '%s' id '%s' expr '%s'\n",
			pe->metric_name, id, pe->metric_expr);
		pr_warning("Error string '%s' help '%s'\n", error.str,
			error.help);
	} else if (ret) {
		pr_debug3("Parse event failed, but for an event that may not be supported by this CPU.\nid '%s' metric '%s' expr '%s'\n",
			  id, pe->metric_name, pe->metric_expr);
		ret = 0;
	}
	evlist__delete(evlist);
	free(error.str);
	free(error.help);
	free(error.first_str);
	free(error.first_help);
	return ret;
}

static void expr_failure(const char *msg,
			 const struct pmu_events_map *map,
			 const struct pmu_event *pe)
{
	pr_debug("%s for map %s %s %s\n",
		msg, map->cpuid, map->version, map->type);
	pr_debug("On metric %s\n", pe->metric_name);
	pr_debug("On expression %s\n", pe->metric_expr);
}

static int test_parsing(void)
{
	struct pmu_events_map *cpus_map = perf_pmu__find_map(NULL);
	struct pmu_events_map *map;
	struct pmu_event *pe;
	int i, j, k;
	int ret = 0;
	struct expr_parse_ctx ctx;
	double result;

	i = 0;
	for (;;) {
		map = &pmu_events_map[i++];
		if (!map->table)
			break;
		j = 0;
		for (;;) {
			struct hashmap_entry *cur;
			size_t bkt;

			pe = &map->table[j++];
			if (!pe->name && !pe->metric_group && !pe->metric_name)
				break;
			if (!pe->metric_expr)
				continue;
			expr__ctx_init(&ctx);
			if (expr__find_other(pe->metric_expr, NULL, &ctx, 0)
				  < 0) {
				expr_failure("Parse other failed", map, pe);
				ret++;
				continue;
			}

			/*
			 * Add all ids with a made up value. The value may
			 * trigger divide by zero when subtracted and so try to
			 * make them unique.
			 */
			k = 1;
			hashmap__for_each_entry((&ctx.ids), cur, bkt)
				expr__add_id(&ctx, strdup(cur->key), k++);

			hashmap__for_each_entry((&ctx.ids), cur, bkt) {
				if (check_parse_id(cur->key, map == cpus_map,
						   pe))
					ret++;
			}

			if (expr__parse(&result, &ctx, pe->metric_expr, 0)) {
				expr_failure("Parse failed", map, pe);
				ret++;
			}
			expr__ctx_clear(&ctx);
		}
	}
	/* TODO: fail when not ok */
	return ret == 0 ? TEST_OK : TEST_SKIP;
}

static const struct {
	int (*func)(void);
	const char *desc;
} pmu_events_testcase_table[] = {
	{
		.func = test_pmu_event_table,
		.desc = "PMU event table sanity",
	},
	{
		.func = test_aliases,
		.desc = "PMU event map aliases",
	},
	{
		.func = test_parsing,
		.desc = "Parsing of PMU event table metrics",
	},
};

const char *test__pmu_events_subtest_get_desc(int subtest)
{
	if (subtest < 0 ||
	    subtest >= (int)ARRAY_SIZE(pmu_events_testcase_table))
		return NULL;
	return pmu_events_testcase_table[subtest].desc;
}

const char *test__pmu_events_subtest_skip_reason(int subtest)
{
	if (subtest < 0 ||
	    subtest >= (int)ARRAY_SIZE(pmu_events_testcase_table))
		return NULL;
	if (pmu_events_testcase_table[subtest].func != test_parsing)
		return NULL;
	return "some metrics failed";
}

int test__pmu_events_subtest_get_nr(void)
{
	return (int)ARRAY_SIZE(pmu_events_testcase_table);
}

int test__pmu_events(struct test *test __maybe_unused, int subtest)
{
	if (subtest < 0 ||
	    subtest >= (int)ARRAY_SIZE(pmu_events_testcase_table))
		return TEST_FAIL;
	return pmu_events_testcase_table[subtest].func();
}
