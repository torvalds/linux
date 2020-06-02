// SPDX-License-Identifier: GPL-2.0
#include <linux/compiler.h>
#include <string.h>
#include <perf/cpumap.h>
#include <perf/evlist.h>
#include "metricgroup.h"
#include "tests.h"
#include "pmu-events/pmu-events.h"
#include "evlist.h"
#include "rblist.h"
#include "debug.h"
#include "expr.h"
#include "stat.h"

static struct pmu_event pme_test[] = {
{
	.metric_expr	= "inst_retired.any / cpu_clk_unhalted.thread",
	.metric_name	= "IPC",
},
{
	.metric_expr	= "idq_uops_not_delivered.core / (4 * (( ( cpu_clk_unhalted.thread / 2 ) * "
			  "( 1 + cpu_clk_unhalted.one_thread_active / cpu_clk_unhalted.ref_xclk ) )))",
	.metric_name	= "Frontend_Bound_SMT",
},
};

static struct pmu_events_map map = {
	.cpuid		= "test",
	.version	= "1",
	.type		= "core",
	.table		= pme_test,
};

struct value {
	const char	*event;
	u64		 val;
};

static u64 find_value(const char *name, struct value *values)
{
	struct value *v = values;

	while (v->event) {
		if (!strcmp(name, v->event))
			return v->val;
		v++;
	};
	return 0;
}

static void load_runtime_stat(struct runtime_stat *st, struct evlist *evlist,
			      struct value *vals)
{
	struct evsel *evsel;
	u64 count;

	evlist__for_each_entry(evlist, evsel) {
		count = find_value(evsel->name, vals);
		perf_stat__update_shadow_stats(evsel, count, 0, st);
	}
}

static double compute_single(struct rblist *metric_events, struct evlist *evlist,
			     struct runtime_stat *st)
{
	struct evsel *evsel = evlist__first(evlist);
	struct metric_event *me;

	me = metricgroup__lookup(metric_events, evsel, false);
	if (me != NULL) {
		struct metric_expr *mexp;

		mexp = list_first_entry(&me->head, struct metric_expr, nd);
		return test_generic_metric(mexp, 0, st);
	}
	return 0.;
}

static int compute_metric(const char *name, struct value *vals, double *ratio)
{
	struct rblist metric_events = {
		.nr_entries = 0,
	};
	struct perf_cpu_map *cpus;
	struct runtime_stat st;
	struct evlist *evlist;
	int err;

	/*
	 * We need to prepare evlist for stat mode running on CPU 0
	 * because that's where all the stats are going to be created.
	 */
	evlist = evlist__new();
	if (!evlist)
		return -ENOMEM;

	cpus = perf_cpu_map__new("0");
	if (!cpus)
		return -ENOMEM;

	perf_evlist__set_maps(&evlist->core, cpus, NULL);

	/* Parse the metric into metric_events list. */
	err = metricgroup__parse_groups_test(evlist, &map, name,
					     false, false,
					     &metric_events);

	TEST_ASSERT_VAL("failed to parse metric", err == 0);

	if (perf_evlist__alloc_stats(evlist, false))
		return -1;

	/* Load the runtime stats with given numbers for events. */
	runtime_stat__init(&st);
	load_runtime_stat(&st, evlist, vals);

	/* And execute the metric */
	*ratio = compute_single(&metric_events, evlist, &st);

	/* ... clenup. */
	metricgroup__rblist_exit(&metric_events);
	runtime_stat__exit(&st);
	perf_evlist__free_stats(evlist);
	perf_cpu_map__put(cpus);
	evlist__delete(evlist);
	return 0;
}

static int test_ipc(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "inst_retired.any",        .val = 300 },
		{ .event = "cpu_clk_unhalted.thread", .val = 200 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("IPC", vals, &ratio) == 0);

	TEST_ASSERT_VAL("IPC failed, wrong ratio",
			ratio == 1.5);
	return 0;
}

static int test_frontend(void)
{
	double ratio;
	struct value vals[] = {
		{ .event = "idq_uops_not_delivered.core",        .val = 300 },
		{ .event = "cpu_clk_unhalted.thread",            .val = 200 },
		{ .event = "cpu_clk_unhalted.one_thread_active", .val = 400 },
		{ .event = "cpu_clk_unhalted.ref_xclk",          .val = 600 },
		{ .event = NULL, },
	};

	TEST_ASSERT_VAL("failed to compute metric",
			compute_metric("Frontend_Bound_SMT", vals, &ratio) == 0);

	TEST_ASSERT_VAL("Frontend_Bound_SMT failed, wrong ratio",
			ratio == 0.45);
	return 0;
}

int test__parse_metric(struct test *test __maybe_unused, int subtest __maybe_unused)
{
	TEST_ASSERT_VAL("IPC failed", test_ipc() == 0);
	TEST_ASSERT_VAL("frontend failed", test_frontend() == 0);
	return 0;
}
