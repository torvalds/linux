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

	/* PMU which we should match against */
	const char *matching_pmu;
};

struct perf_pmu_test_pmu {
	struct perf_pmu pmu;
	struct perf_pmu_test_event const *aliases[10];
};

static const struct perf_pmu_test_event bp_l1_btb_correct = {
	.event = {
		.name = "bp_l1_btb_correct",
		.event = "event=0x8a",
		.desc = "L1 BTB Correction",
		.topic = "branch",
	},
	.alias_str = "event=0x8a",
	.alias_long_desc = "L1 BTB Correction",
};

static const struct perf_pmu_test_event bp_l2_btb_correct = {
	.event = {
		.name = "bp_l2_btb_correct",
		.event = "event=0x8b",
		.desc = "L2 BTB Correction",
		.topic = "branch",
	},
	.alias_str = "event=0x8b",
	.alias_long_desc = "L2 BTB Correction",
};

static const struct perf_pmu_test_event segment_reg_loads_any = {
	.event = {
		.name = "segment_reg_loads.any",
		.event = "event=0x6,period=200000,umask=0x80",
		.desc = "Number of segment register loads",
		.topic = "other",
	},
	.alias_str = "event=0x6,period=0x30d40,umask=0x80",
	.alias_long_desc = "Number of segment register loads",
};

static const struct perf_pmu_test_event dispatch_blocked_any = {
	.event = {
		.name = "dispatch_blocked.any",
		.event = "event=0x9,period=200000,umask=0x20",
		.desc = "Memory cluster signals to block micro-op dispatch for any reason",
		.topic = "other",
	},
	.alias_str = "event=0x9,period=0x30d40,umask=0x20",
	.alias_long_desc = "Memory cluster signals to block micro-op dispatch for any reason",
};

static const struct perf_pmu_test_event eist_trans = {
	.event = {
		.name = "eist_trans",
		.event = "event=0x3a,period=200000,umask=0x0",
		.desc = "Number of Enhanced Intel SpeedStep(R) Technology (EIST) transitions",
		.topic = "other",
	},
	.alias_str = "event=0x3a,period=0x30d40,umask=0",
	.alias_long_desc = "Number of Enhanced Intel SpeedStep(R) Technology (EIST) transitions",
};

static const struct perf_pmu_test_event l3_cache_rd = {
	.event = {
		.name = "l3_cache_rd",
		.event = "event=0x40",
		.desc = "L3 cache access, read",
		.long_desc = "Attributable Level 3 cache access, read",
		.topic = "cache",
	},
	.alias_str = "event=0x40",
	.alias_long_desc = "Attributable Level 3 cache access, read",
};

static const struct perf_pmu_test_event *core_events[] = {
	&bp_l1_btb_correct,
	&bp_l2_btb_correct,
	&segment_reg_loads_any,
	&dispatch_blocked_any,
	&eist_trans,
	&l3_cache_rd,
	NULL
};

static const struct perf_pmu_test_event uncore_hisi_ddrc_flux_wcmd = {
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
	.matching_pmu = "hisi_sccl1_ddrc2",
};

static const struct perf_pmu_test_event unc_cbo_xsnp_response_miss_eviction = {
	.event = {
		.name = "unc_cbo_xsnp_response.miss_eviction",
		.event = "event=0x22,umask=0x81",
		.desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core. Unit: uncore_cbox ",
		.topic = "uncore",
		.long_desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core",
		.pmu = "uncore_cbox",
	},
	.alias_str = "event=0x22,umask=0x81",
	.alias_long_desc = "A cross-core snoop resulted from L3 Eviction which misses in some processor core",
	.matching_pmu = "uncore_cbox_0",
};

static const struct perf_pmu_test_event uncore_hyphen = {
	.event = {
		.name = "event-hyphen",
		.event = "event=0xe0,umask=0x00",
		.desc = "UNC_CBO_HYPHEN. Unit: uncore_cbox ",
		.topic = "uncore",
		.long_desc = "UNC_CBO_HYPHEN",
		.pmu = "uncore_cbox",
	},
	.alias_str = "event=0xe0,umask=0",
	.alias_long_desc = "UNC_CBO_HYPHEN",
	.matching_pmu = "uncore_cbox_0",
};

static const struct perf_pmu_test_event uncore_two_hyph = {
	.event = {
		.name = "event-two-hyph",
		.event = "event=0xc0,umask=0x00",
		.desc = "UNC_CBO_TWO_HYPH. Unit: uncore_cbox ",
		.topic = "uncore",
		.long_desc = "UNC_CBO_TWO_HYPH",
		.pmu = "uncore_cbox",
	},
	.alias_str = "event=0xc0,umask=0",
	.alias_long_desc = "UNC_CBO_TWO_HYPH",
	.matching_pmu = "uncore_cbox_0",
};

static const struct perf_pmu_test_event uncore_hisi_l3c_rd_hit_cpipe = {
	.event = {
		.name = "uncore_hisi_l3c.rd_hit_cpipe",
		.event = "event=0x7",
		.desc = "Total read hits. Unit: hisi_sccl,l3c ",
		.topic = "uncore",
		.long_desc = "Total read hits",
		.pmu = "hisi_sccl,l3c",
	},
	.alias_str = "event=0x7",
	.alias_long_desc = "Total read hits",
	.matching_pmu = "hisi_sccl3_l3c7",
};

static const struct perf_pmu_test_event uncore_imc_free_running_cache_miss = {
	.event = {
		.name = "uncore_imc_free_running.cache_miss",
		.event = "event=0x12",
		.desc = "Total cache misses. Unit: uncore_imc_free_running ",
		.topic = "uncore",
		.long_desc = "Total cache misses",
		.pmu = "uncore_imc_free_running",
	},
	.alias_str = "event=0x12",
	.alias_long_desc = "Total cache misses",
	.matching_pmu = "uncore_imc_free_running_0",
};

static const struct perf_pmu_test_event uncore_imc_cache_hits = {
	.event = {
		.name = "uncore_imc.cache_hits",
		.event = "event=0x34",
		.desc = "Total cache hits. Unit: uncore_imc ",
		.topic = "uncore",
		.long_desc = "Total cache hits",
		.pmu = "uncore_imc",
	},
	.alias_str = "event=0x34",
	.alias_long_desc = "Total cache hits",
	.matching_pmu = "uncore_imc_0",
};

static const struct perf_pmu_test_event *uncore_events[] = {
	&uncore_hisi_ddrc_flux_wcmd,
	&unc_cbo_xsnp_response_miss_eviction,
	&uncore_hyphen,
	&uncore_two_hyph,
	&uncore_hisi_l3c_rd_hit_cpipe,
	&uncore_imc_free_running_cache_miss,
	&uncore_imc_cache_hits,
	NULL
};

static const struct perf_pmu_test_event sys_ddr_pmu_write_cycles = {
	.event = {
		.name = "sys_ddr_pmu.write_cycles",
		.event = "event=0x2b",
		.desc = "ddr write-cycles event. Unit: uncore_sys_ddr_pmu ",
		.topic = "uncore",
		.pmu = "uncore_sys_ddr_pmu",
		.compat = "v8",
	},
	.alias_str = "event=0x2b",
	.alias_long_desc = "ddr write-cycles event. Unit: uncore_sys_ddr_pmu ",
	.matching_pmu = "uncore_sys_ddr_pmu",
};

static const struct perf_pmu_test_event sys_ccn_pmu_read_cycles = {
	.event = {
		.name = "sys_ccn_pmu.read_cycles",
		.event = "config=0x2c",
		.desc = "ccn read-cycles event. Unit: uncore_sys_ccn_pmu ",
		.topic = "uncore",
		.pmu = "uncore_sys_ccn_pmu",
		.compat = "0x01",
	},
	.alias_str = "config=0x2c",
	.alias_long_desc = "ccn read-cycles event. Unit: uncore_sys_ccn_pmu ",
	.matching_pmu = "uncore_sys_ccn_pmu",
};

static const struct perf_pmu_test_event *sys_events[] = {
	&sys_ddr_pmu_write_cycles,
	&sys_ccn_pmu_read_cycles,
	NULL
};

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

static const struct pmu_events_map *__test_pmu_get_events_map(void)
{
	const struct pmu_events_map *map;

	for (map = &pmu_events_map[0]; map->cpuid; map++) {
		if (!strcmp(map->cpuid, "testcpu"))
			return map;
	}

	pr_err("could not find test events map\n");

	return NULL;
}

static const struct pmu_event *__test_pmu_get_sys_events_table(void)
{
	const struct pmu_sys_events *tables = &pmu_sys_event_tables[0];

	for ( ; tables->name; tables++) {
		if (!strcmp("pme_test_soc_sys", tables->name))
			return tables->table;
	}

	return NULL;
}

static int compare_pmu_events(const struct pmu_event *e1, const struct pmu_event *e2)
{
	if (!is_same(e1->name, e2->name)) {
		pr_debug2("testing event e1 %s: mismatched name string, %s vs %s\n",
			  e1->name, e1->name, e2->name);
		return -1;
	}

	if (!is_same(e1->compat, e2->compat)) {
		pr_debug2("testing event e1 %s: mismatched compat string, %s vs %s\n",
			  e1->name, e1->compat, e2->compat);
		return -1;
	}

	if (!is_same(e1->event, e2->event)) {
		pr_debug2("testing event e1 %s: mismatched event, %s vs %s\n",
			  e1->name, e1->event, e2->event);
		return -1;
	}

	if (!is_same(e1->desc, e2->desc)) {
		pr_debug2("testing event e1 %s: mismatched desc, %s vs %s\n",
			  e1->name, e1->desc, e2->desc);
		return -1;
	}

	if (!is_same(e1->topic, e2->topic)) {
		pr_debug2("testing event e1 %s: mismatched topic, %s vs %s\n",
			  e1->name, e1->topic, e2->topic);
		return -1;
	}

	if (!is_same(e1->long_desc, e2->long_desc)) {
		pr_debug2("testing event e1 %s: mismatched long_desc, %s vs %s\n",
			  e1->name, e1->long_desc, e2->long_desc);
		return -1;
	}

	if (!is_same(e1->pmu, e2->pmu)) {
		pr_debug2("testing event e1 %s: mismatched pmu string, %s vs %s\n",
			  e1->name, e1->pmu, e2->pmu);
		return -1;
	}

	if (!is_same(e1->unit, e2->unit)) {
		pr_debug2("testing event e1 %s: mismatched unit, %s vs %s\n",
			  e1->name, e1->unit, e2->unit);
		return -1;
	}

	if (!is_same(e1->perpkg, e2->perpkg)) {
		pr_debug2("testing event e1 %s: mismatched perpkg, %s vs %s\n",
			  e1->name, e1->perpkg, e2->perpkg);
		return -1;
	}

	if (!is_same(e1->aggr_mode, e2->aggr_mode)) {
		pr_debug2("testing event e1 %s: mismatched aggr_mode, %s vs %s\n",
			  e1->name, e1->aggr_mode, e2->aggr_mode);
		return -1;
	}

	if (!is_same(e1->metric_expr, e2->metric_expr)) {
		pr_debug2("testing event e1 %s: mismatched metric_expr, %s vs %s\n",
			  e1->name, e1->metric_expr, e2->metric_expr);
		return -1;
	}

	if (!is_same(e1->metric_name, e2->metric_name)) {
		pr_debug2("testing event e1 %s: mismatched metric_name, %s vs %s\n",
			  e1->name,	e1->metric_name, e2->metric_name);
		return -1;
	}

	if (!is_same(e1->metric_group, e2->metric_group)) {
		pr_debug2("testing event e1 %s: mismatched metric_group, %s vs %s\n",
			  e1->name, e1->metric_group, e2->metric_group);
		return -1;
	}

	if (!is_same(e1->deprecated, e2->deprecated)) {
		pr_debug2("testing event e1 %s: mismatched deprecated, %s vs %s\n",
			  e1->name, e1->deprecated, e2->deprecated);
		return -1;
	}

	if (!is_same(e1->metric_constraint, e2->metric_constraint)) {
		pr_debug2("testing event e1 %s: mismatched metric_constant, %s vs %s\n",
			  e1->name, e1->metric_constraint, e2->metric_constraint);
		return -1;
	}

	return 0;
}

static int compare_alias_to_test_event(struct perf_pmu_alias *alias,
				struct perf_pmu_test_event const *test_event,
				char const *pmu_name)
{
	struct pmu_event const *event = &test_event->event;

	/* An alias was found, ensure everything is in order */
	if (!is_same(alias->name, event->name)) {
		pr_debug("testing aliases PMU %s: mismatched name, %s vs %s\n",
			  pmu_name, alias->name, event->name);
		return -1;
	}

	if (!is_same(alias->desc, event->desc)) {
		pr_debug("testing aliases PMU %s: mismatched desc, %s vs %s\n",
			  pmu_name, alias->desc, event->desc);
		return -1;
	}

	if (!is_same(alias->long_desc, test_event->alias_long_desc)) {
		pr_debug("testing aliases PMU %s: mismatched long_desc, %s vs %s\n",
			  pmu_name, alias->long_desc,
			  test_event->alias_long_desc);
		return -1;
	}

	if (!is_same(alias->topic, event->topic)) {
		pr_debug("testing aliases PMU %s: mismatched topic, %s vs %s\n",
			  pmu_name, alias->topic, event->topic);
		return -1;
	}

	if (!is_same(alias->str, test_event->alias_str)) {
		pr_debug("testing aliases PMU %s: mismatched str, %s vs %s\n",
			  pmu_name, alias->str, test_event->alias_str);
		return -1;
	}

	if (!is_same(alias->long_desc, test_event->alias_long_desc)) {
		pr_debug("testing aliases PMU %s: mismatched long desc, %s vs %s\n",
			  pmu_name, alias->str, test_event->alias_long_desc);
		return -1;
	}


	if (!is_same(alias->pmu_name, test_event->event.pmu)) {
		pr_debug("testing aliases PMU %s: mismatched pmu_name, %s vs %s\n",
			  pmu_name, alias->pmu_name, test_event->event.pmu);
		return -1;
	}

	return 0;
}

/* Verify generated events from pmu-events.c are as expected */
static int test__pmu_event_table(struct test_suite *test __maybe_unused,
				 int subtest __maybe_unused)
{
	const struct pmu_event *sys_event_tables = __test_pmu_get_sys_events_table();
	const struct pmu_events_map *map = __test_pmu_get_events_map();
	const struct pmu_event *table;
	int map_events = 0, expected_events;

	/* ignore 3x sentinels */
	expected_events = ARRAY_SIZE(core_events) +
			  ARRAY_SIZE(uncore_events) +
			  ARRAY_SIZE(sys_events) - 3;

	if (!map || !sys_event_tables)
		return -1;

	for (table = map->table; table->name; table++) {
		struct perf_pmu_test_event const **test_event_table;
		bool found = false;

		if (table->pmu)
			test_event_table = &uncore_events[0];
		else
			test_event_table = &core_events[0];

		for (; *test_event_table; test_event_table++) {
			struct perf_pmu_test_event const *test_event = *test_event_table;
			struct pmu_event const *event = &test_event->event;

			if (strcmp(table->name, event->name))
				continue;
			found = true;
			map_events++;

			if (compare_pmu_events(table, event))
				return -1;

			pr_debug("testing event table %s: pass\n", table->name);
		}

		if (!found) {
			pr_err("testing event table: could not find event %s\n",
			       table->name);
			return -1;
		}
	}

	for (table = sys_event_tables; table->name; table++) {
		struct perf_pmu_test_event const **test_event_table;
		bool found = false;

		test_event_table = &sys_events[0];

		for (; *test_event_table; test_event_table++) {
			struct perf_pmu_test_event const *test_event = *test_event_table;
			struct pmu_event const *event = &test_event->event;

			if (strcmp(table->name, event->name))
				continue;
			found = true;
			map_events++;

			if (compare_pmu_events(table, event))
				return -1;

			pr_debug("testing sys event table %s: pass\n", table->name);
		}
		if (!found) {
			pr_debug("testing event table: could not find event %s\n",
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
static int __test_core_pmu_event_aliases(char *pmu_name, int *count)
{
	struct perf_pmu_test_event const **test_event_table;
	struct perf_pmu *pmu;
	LIST_HEAD(aliases);
	int res = 0;
	const struct pmu_events_map *map = __test_pmu_get_events_map();
	struct perf_pmu_alias *a, *tmp;

	if (!map)
		return -1;

	test_event_table = &core_events[0];

	pmu = zalloc(sizeof(*pmu));
	if (!pmu)
		return -1;

	pmu->name = pmu_name;

	pmu_add_cpu_aliases_map(&aliases, pmu, map);

	for (; *test_event_table; test_event_table++) {
		struct perf_pmu_test_event const *test_event = *test_event_table;
		struct pmu_event const *event = &test_event->event;
		struct perf_pmu_alias *alias = find_alias(event->name, &aliases);

		if (!alias) {
			pr_debug("testing aliases core PMU %s: no alias, alias_table->name=%s\n",
				  pmu_name, event->name);
			res = -1;
			break;
		}

		if (compare_alias_to_test_event(alias, test_event, pmu_name)) {
			res = -1;
			break;
		}

		(*count)++;
		pr_debug2("testing aliases core PMU %s: matched event %s\n",
			  pmu_name, alias->name);
	}

	list_for_each_entry_safe(a, tmp, &aliases, list) {
		list_del(&a->list);
		perf_pmu_free_alias(a);
	}
	free(pmu);
	return res;
}

static int __test_uncore_pmu_event_aliases(struct perf_pmu_test_pmu *test_pmu)
{
	int alias_count = 0, to_match_count = 0, matched_count = 0;
	struct perf_pmu_test_event const **table;
	struct perf_pmu *pmu = &test_pmu->pmu;
	const char *pmu_name = pmu->name;
	struct perf_pmu_alias *a, *tmp, *alias;
	const struct pmu_events_map *map;
	LIST_HEAD(aliases);
	int res = 0;

	map = __test_pmu_get_events_map();
	if (!map)
		return -1;
	pmu_add_cpu_aliases_map(&aliases, pmu, map);
	pmu_add_sys_aliases(&aliases, pmu);

	/* Count how many aliases we generated */
	list_for_each_entry(alias, &aliases, list)
		alias_count++;

	/* Count how many aliases we expect from the known table */
	for (table = &test_pmu->aliases[0]; *table; table++)
		to_match_count++;

	if (alias_count != to_match_count) {
		pr_debug("testing aliases uncore PMU %s: mismatch expected aliases (%d) vs found (%d)\n",
			 pmu_name, to_match_count, alias_count);
		res = -1;
		goto out;
	}

	list_for_each_entry(alias, &aliases, list) {
		bool matched = false;

		for (table = &test_pmu->aliases[0]; *table; table++) {
			struct perf_pmu_test_event const *test_event = *table;
			struct pmu_event const *event = &test_event->event;

			if (!strcmp(event->name, alias->name)) {
				if (compare_alias_to_test_event(alias,
							test_event,
							pmu_name)) {
					continue;
				}
				matched = true;
				matched_count++;
			}
		}

		if (matched == false) {
			pr_debug("testing aliases uncore PMU %s: could not match alias %s\n",
				 pmu_name, alias->name);
			res = -1;
			goto out;
		}
	}

	if (alias_count != matched_count) {
		pr_debug("testing aliases uncore PMU %s: mismatch found aliases (%d) vs matched (%d)\n",
			 pmu_name, matched_count, alias_count);
		res = -1;
	}

out:
	list_for_each_entry_safe(a, tmp, &aliases, list) {
		list_del(&a->list);
		perf_pmu_free_alias(a);
	}
	return res;
}

static struct perf_pmu_test_pmu test_pmus[] = {
	{
		.pmu = {
			.name = (char *)"hisi_sccl1_ddrc2",
			.is_uncore = 1,
		},
		.aliases = {
			&uncore_hisi_ddrc_flux_wcmd,
		},
	},
	{
		.pmu = {
			.name = (char *)"uncore_cbox_0",
			.is_uncore = 1,
		},
		.aliases = {
			&unc_cbo_xsnp_response_miss_eviction,
			&uncore_hyphen,
			&uncore_two_hyph,
		},
	},
	{
		.pmu = {
			.name = (char *)"hisi_sccl3_l3c7",
			.is_uncore = 1,
		},
		.aliases = {
			&uncore_hisi_l3c_rd_hit_cpipe,
		},
	},
	{
		.pmu = {
			.name = (char *)"uncore_imc_free_running_0",
			.is_uncore = 1,
		},
		.aliases = {
			&uncore_imc_free_running_cache_miss,
		},
	},
	{
		.pmu = {
			.name = (char *)"uncore_imc_0",
			.is_uncore = 1,
		},
		.aliases = {
			&uncore_imc_cache_hits,
		},
	},
	{
		.pmu = {
			.name = (char *)"uncore_sys_ddr_pmu0",
			.is_uncore = 1,
			.id = (char *)"v8",
		},
		.aliases = {
			&sys_ddr_pmu_write_cycles,
		},
	},
	{
		.pmu = {
			.name = (char *)"uncore_sys_ccn_pmu4",
			.is_uncore = 1,
			.id = (char *)"0x01",
		},
		.aliases = {
			&sys_ccn_pmu_read_cycles,
		},
	},
};

/* Test that aliases generated are as expected */
static int test__aliases(struct test_suite *test __maybe_unused,
			int subtest __maybe_unused)
{
	struct perf_pmu *pmu = NULL;
	unsigned long i;

	while ((pmu = perf_pmu__scan(pmu)) != NULL) {
		int count = 0;

		if (!is_pmu_core(pmu->name))
			continue;

		if (list_empty(&pmu->format)) {
			pr_debug2("skipping testing core PMU %s\n", pmu->name);
			continue;
		}

		if (__test_core_pmu_event_aliases(pmu->name, &count)) {
			pr_debug("testing core PMU %s aliases: failed\n", pmu->name);
			return -1;
		}

		if (count == 0) {
			pr_debug("testing core PMU %s aliases: no events to match\n",
				  pmu->name);
			return -1;
		}

		pr_debug("testing core PMU %s aliases: pass\n", pmu->name);
	}

	for (i = 0; i < ARRAY_SIZE(test_pmus); i++) {
		int res = __test_uncore_pmu_event_aliases(&test_pmus[i]);

		if (res)
			return res;
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
	char *dup, *cur;

	/* Numbers are always valid. */
	if (is_number(id))
		return 0;

	evlist = evlist__new();
	if (!evlist)
		return -ENOMEM;

	dup = strdup(id);
	if (!dup)
		return -ENOMEM;

	for (cur = strchr(dup, '@') ; cur; cur = strchr(++cur, '@'))
		*cur = '/';

	ret = __parse_events(evlist, dup, error, fake_pmu);
	free(dup);

	evlist__delete(evlist);
	return ret;
}

static int check_parse_cpu(const char *id, bool same_cpu, const struct pmu_event *pe)
{
	struct parse_events_error error;
	int ret;

	parse_events_error__init(&error);
	ret = check_parse_id(id, &error, NULL);
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
	parse_events_error__exit(&error);
	return ret;
}

static int check_parse_fake(const char *id)
{
	struct parse_events_error error;
	int ret;

	parse_events_error__init(&error);
	ret = check_parse_id(id, &error, &perf_pmu__fake);
	parse_events_error__exit(&error);
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
				 const struct pmu_events_map *map,
				 const char *metric_name)
{
	struct hashmap_entry *cur, *cur_tmp;
	struct metric *metric, *tmp;
	size_t bkt;
	bool all;
	int rc;

	do {
		all = true;
		hashmap__for_each_entry_safe(pctx->ids, cur, cur_tmp, bkt) {
			struct metric_ref *ref;
			const struct pmu_event *pe;

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

			rc = expr__find_ids(pe->metric_expr, NULL, pctx);
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

static int test__parsing(struct test_suite *test __maybe_unused,
			 int subtest __maybe_unused)
{
	const struct pmu_events_map *cpus_map = pmu_events_map__find();
	const struct pmu_events_map *map;
	const struct pmu_event *pe;
	int i, j, k;
	int ret = 0;
	struct expr_parse_ctx *ctx;
	double result;

	ctx = expr__ctx_new();
	if (!ctx) {
		pr_debug("expr__ctx_new failed");
		return TEST_FAIL;
	}
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
			expr__ctx_clear(ctx);
			if (expr__find_ids(pe->metric_expr, NULL, ctx) < 0) {
				expr_failure("Parse find ids failed", map, pe);
				ret++;
				continue;
			}

			if (resolve_metric_simple(ctx, &compound_list, map,
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
			hashmap__for_each_entry(ctx->ids, cur, bkt)
				expr__add_id_val(ctx, strdup(cur->key), k++);

			hashmap__for_each_entry(ctx->ids, cur, bkt) {
				if (check_parse_cpu(cur->key, map == cpus_map,
						   pe))
					ret++;
			}

			list_for_each_entry_safe(metric, tmp, &compound_list, list) {
				expr__add_ref(ctx, &metric->metric_ref);
				free(metric);
			}

			if (expr__parse(&result, ctx, pe->metric_expr)) {
				/*
				 * Parsing failed, make numbers go from large to
				 * small which can resolve divide by zero
				 * issues.
				 */
				k = 1024;
				hashmap__for_each_entry(ctx->ids, cur, bkt)
					expr__add_id_val(ctx, strdup(cur->key), k--);
				if (expr__parse(&result, ctx, pe->metric_expr)) {
					expr_failure("Parse failed", map, pe);
					ret++;
				}
			}
		}
	}
	expr__ctx_free(ctx);
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
	struct expr_parse_ctx *ctx;
	struct hashmap_entry *cur;
	double result;
	int ret = -1;
	size_t bkt;
	int i;

	pr_debug("parsing '%s'\n", str);

	ctx = expr__ctx_new();
	if (!ctx) {
		pr_debug("expr__ctx_new failed");
		return TEST_FAIL;
	}
	if (expr__find_ids(str, NULL, ctx) < 0) {
		pr_err("expr__find_ids failed\n");
		return -1;
	}

	/*
	 * Add all ids with a made up value. The value may
	 * trigger divide by zero when subtracted and so try to
	 * make them unique.
	 */
	i = 1;
	hashmap__for_each_entry(ctx->ids, cur, bkt)
		expr__add_id_val(ctx, strdup(cur->key), i++);

	hashmap__for_each_entry(ctx->ids, cur, bkt) {
		if (check_parse_fake(cur->key)) {
			pr_err("check_parse_fake failed\n");
			goto out;
		}
	}

	ret = 0;
	if (expr__parse(&result, ctx, str)) {
		/*
		 * Parsing failed, make numbers go from large to small which can
		 * resolve divide by zero issues.
		 */
		i = 1024;
		hashmap__for_each_entry(ctx->ids, cur, bkt)
			expr__add_id_val(ctx, strdup(cur->key), i--);
		if (expr__parse(&result, ctx, str)) {
			pr_err("expr__parse failed\n");
			ret = -1;
		}
	}

out:
	expr__ctx_free(ctx);
	return ret;
}

/*
 * Parse all the metrics for current architecture,
 * or all defined cpus via the 'fake_pmu'
 * in parse_events.
 */
static int test__parsing_fake(struct test_suite *test __maybe_unused,
			      int subtest __maybe_unused)
{
	const struct pmu_events_map *map;
	const struct pmu_event *pe;
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

static struct test_case pmu_events_tests[] = {
	TEST_CASE("PMU event table sanity", pmu_event_table),
	TEST_CASE("PMU event map aliases", aliases),
	TEST_CASE_REASON("Parsing of PMU event table metrics", parsing,
			 "some metrics failed"),
	TEST_CASE("Parsing of PMU event table metrics with fake PMUs", parsing_fake),
	{ .name = NULL, }
};

struct test_suite suite__pmu_events = {
	.desc = "PMU events",
	.test_cases = pmu_events_tests,
};
