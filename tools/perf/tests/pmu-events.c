// SPDX-License-Identifier: GPL-2.0
#include "parse-events.h"
#include "pmu.h"
#include "tests.h"
#include <errno.h>
#include <stdio.h>
#include <linux/kernel.h>

#include "debug.h"
#include "../pmu-events/pmu-events.h"

struct perf_pmu_test_event {
	struct pmu_event event;
};
static struct perf_pmu_test_event test_cpu_events[] = {
	{
		.event = {
			.name = "bp_l1_btb_correct",
			.event = "event=0x8a",
			.desc = "L1 BTB Correction",
			.topic = "branch",
		},
	},
	{
		.event = {
			.name = "bp_l2_btb_correct",
			.event = "event=0x8b",
			.desc = "L2 BTB Correction",
			.topic = "branch",
		},
	},
	{
		.event = {
			.name = "segment_reg_loads.any",
			.event = "umask=0x80,period=200000,event=0x6",
			.desc = "Number of segment register loads",
			.topic = "other",
		},
	},
	{
		.event = {
			.name = "dispatch_blocked.any",
			.event = "umask=0x20,period=200000,event=0x9",
			.desc = "Memory cluster signals to block micro-op dispatch for any reason",
			.topic = "other",
		},
	},
	{
		.event = {
			.name = "eist_trans",
			.event = "umask=0x0,period=200000,event=0x3a",
			.desc = "Number of Enhanced Intel SpeedStep(R) Technology (EIST) transitions",
			.topic = "other",
		},
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
static int __test_pmu_event_table(void)
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
int test__pmu_events(struct test *test __maybe_unused,
		     int subtest __maybe_unused)
{
	if (__test_pmu_event_table())
		return -1;

	return 0;
}
