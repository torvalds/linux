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
#include "metricgroup.h"

struct perf_pmu_test_event {
	/* used for matching against events from generated pmu-events.c */
	struct pmu_event event;

	/* used for matching against event aliases */
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
	{
		.event = {
			.name = "l3_cache_rd",
			.event = "event=0x40",
			.desc = "L3 cache access, read",
			.long_desc = "Attributable Level 3 cache access, read",
			.topic = "cache",
		},
		.alias_str = "event=0x40",
		.alias_long_desc = "Attributable Level 3 cache access, read",
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
	struct perf_pmu_alias *a, *tmp;

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

	list_for_each_entry_safe(a, tmp, &aliases, list) {
		list_del(&a->list);
		perf_pmu_free_alias(a);
	}
	free(pmu);
	return res;
}


/* Test that aliases generated are as expected */
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

static int check_parse_id(const char *id, struct parse_events_error *error,
			  struct perf_pmu *fake_pmu)
{
	struct evlist *evlist;
	int ret;

	/* Numbers are always valid. */
	if (is_number(id))
		return 0;

	evlist = evlist__new();
	if (!evlist)
		return -ENOMEM;
	ret = __parse_events(evlist, id, error, fake_pmu);
	evlist__delete(evlist);
	return ret;
}

static int check_parse_cpu(const char *id, bool same_cpu, struct pmu_event *pe)
{
	struct parse_events_error error = { .idx = 0, };

	int ret = check_parse_id(id, &error, NULL);
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
	free(error.str);
	free(error.help);
	free(error.first_str);
	free(error.first_help);
	return ret;
}

static int check_parse_fake(const char *id)
{
	struct parse_events_error error = { .idx = 0, };
	int ret = check_parse_id(id, &error, &perf_pmu__fake);

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

struct metric {
	struct list_head list;
	struct metric_ref metric_ref;
};

static int resolve_metric_simple(struct expr_parse_ctx *pctx,
				 struct list_head *compound_list,
				 struct pmu_events_map *map,
				 const char *metric_name)
{
	struct hashmap_entry *cur, *cur_tmp;
	struct metric *metric, *tmp;
	size_t bkt;
	bool all;
	int rc;

	do {
		all = true;
		hashmap__for_each_entry_safe((&pctx->ids), cur, cur_tmp, bkt) {
			struct metric_ref *ref;
			struct pmu_event *pe;

			pe = metricgroup__find_metric(cur->key, map);
			if (!pe)
				continue;

			if (!strcmp(metric_name, (char *)cur->key)) {
				pr_warning("Recursion detected for metric %s\n", metric_name);
				rc = -1;
				goto out_err;
			}

			all = false;

			/* The metric key itself needs to go out.. */
			expr__del_id(pctx, cur->key);

			metric = malloc(sizeof(*metric));
			if (!metric) {
				rc = -ENOMEM;
				goto out_err;
			}

			ref = &metric->metric_ref;
			ref->metric_name = pe->metric_name;
			ref->metric_expr = pe->metric_expr;
			list_add_tail(&metric->list, compound_list);

			rc = expr__find_other(pe->metric_expr, NULL, pctx, 0);
			if (rc)
				goto out_err;
			break; /* The hashmap has been modified, so restart */
		}
	} while (!all);

	return 0;

out_err:
	list_for_each_entry_safe(metric, tmp, compound_list, list)
		free(metric);

	return rc;

}

static int test_parsing(void)
{
	struct pmu_events_map *cpus_map = pmu_events_map__find();
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
			struct metric *metric, *tmp;
			struct hashmap_entry *cur;
			LIST_HEAD(compound_list);
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

			if (resolve_metric_simple(&ctx, &compound_list, map,
						  pe->metric_name)) {
				expr_failure("Could not resolve metrics", map, pe);
				ret++;
				goto exit; /* Don't tolerate errors due to severity */
			}

			/*
			 * Add all ids with a made up value. The value may
			 * trigger divide by zero when subtracted and so try to
			 * make them unique.
			 */
			k = 1;
			hashmap__for_each_entry((&ctx.ids), cur, bkt)
				expr__add_id_val(&ctx, strdup(cur->key), k++);

			hashmap__for_each_entry((&ctx.ids), cur, bkt) {
				if (check_parse_cpu(cur->key, map == cpus_map,
						   pe))
					ret++;
			}

			list_for_each_entry_safe(metric, tmp, &compound_list, list) {
				expr__add_ref(&ctx, &metric->metric_ref);
				free(metric);
			}

			if (expr__parse(&result, &ctx, pe->metric_expr, 0)) {
				expr_failure("Parse failed", map, pe);
				ret++;
			}
			expr__ctx_clear(&ctx);
		}
	}
	/* TODO: fail when not ok */
exit:
	return ret == 0 ? TEST_OK : TEST_SKIP;
}

struct test_metric {
	const char *str;
};

static struct test_metric metrics[] = {
	{ "(unc_p_power_state_occupancy.cores_c0 / unc_p_clockticks) * 100." },
	{ "imx8_ddr0@read\\-cycles@ * 4 * 4", },
	{ "imx8_ddr0@axid\\-read\\,axi_mask\\=0xffff\\,axi_id\\=0x0000@ * 4", },
	{ "(cstate_pkg@c2\\-residency@ / msr@tsc@) * 100", },
	{ "(imx8_ddr0@read\\-cycles@ + imx8_ddr0@write\\-cycles@)", },
};

static int metric_parse_fake(const char *str)
{
	struct expr_parse_ctx ctx;
	struct hashmap_entry *cur;
	double result;
	int ret = -1;
	size_t bkt;
	int i;

	pr_debug("parsing '%s'\n", str);

	expr__ctx_init(&ctx);
	if (expr__find_other(str, NULL, &ctx, 0) < 0) {
		pr_err("expr__find_other failed\n");
		return -1;
	}

	/*
	 * Add all ids with a made up value. The value may
	 * trigger divide by zero when subtracted and so try to
	 * make them unique.
	 */
	i = 1;
	hashmap__for_each_entry((&ctx.ids), cur, bkt)
		expr__add_id_val(&ctx, strdup(cur->key), i++);

	hashmap__for_each_entry((&ctx.ids), cur, bkt) {
		if (check_parse_fake(cur->key)) {
			pr_err("check_parse_fake failed\n");
			goto out;
		}
	}

	if (expr__parse(&result, &ctx, str, 0))
		pr_err("expr__parse failed\n");
	else
		ret = 0;

out:
	expr__ctx_clear(&ctx);
	return ret;
}

/*
 * Parse all the metrics for current architecture,
 * or all defined cpus via the 'fake_pmu'
 * in parse_events.
 */
static int test_parsing_fake(void)
{
	struct pmu_events_map *map;
	struct pmu_event *pe;
	unsigned int i, j;
	int err = 0;

	for (i = 0; i < ARRAY_SIZE(metrics); i++) {
		err = metric_parse_fake(metrics[i].str);
		if (err)
			return err;
	}

	i = 0;
	for (;;) {
		map = &pmu_events_map[i++];
		if (!map->table)
			break;
		j = 0;
		for (;;) {
			pe = &map->table[j++];
			if (!pe->name && !pe->metric_group && !pe->metric_name)
				break;
			if (!pe->metric_expr)
				continue;
			err = metric_parse_fake(pe->metric_expr);
			if (err)
				return err;
		}
	}

	return 0;
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
	{
		.func = test_parsing_fake,
		.desc = "Parsing of PMU event table metrics with fake PMUs",
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
