// SPDX-License-Identifier: GPL-2.0-only
#ifndef METRICGROUP_H
#define METRICGROUP_H 1

#include <linux/list.h>
#include <linux/rbtree.h>
#include <stdbool.h>
#include "pmu-events/pmu-events.h"

struct evlist;
struct evsel;
struct option;
struct rblist;
struct pmu_events_map;
struct cgroup;

/**
 * A node in a rblist keyed by the evsel. The global rblist of metric events
 * generally exists in perf_stat_config. The evsel is looked up in the rblist
 * yielding a list of metric_expr.
 */
struct metric_event {
	struct rb_node nd;
	struct evsel *evsel;
	struct list_head head; /* list of metric_expr */
};

/**
 * A metric referenced by a metric_expr. When parsing a metric expression IDs
 * will be looked up, matching either a value (from metric_events) or a
 * metric_ref. A metric_ref will then be parsed recursively. The metric_refs and
 * metric_events need to be known before parsing so that their values may be
 * placed in the parse context for lookup.
 */
struct metric_ref {
	const char *metric_name;
	const char *metric_expr;
};

/**
 * One in a list of metric_expr associated with an evsel. The data is used to
 * generate a metric value during stat output.
 */
struct metric_expr {
	struct list_head nd;
	/** The expression to parse, for example, "instructions/cycles". */
	const char *metric_expr;
	/** The name of the meric such as "IPC". */
	const char *metric_name;
	/**
	 * The "ScaleUnit" that scales and adds a unit to the metric during
	 * output. For example, "6.4e-05MiB" means to scale the resulting metric
	 * by 6.4e-05 (typically converting a unit like cache lines to something
	 * more human intelligible) and then add "MiB" afterward when displayed.
	 */
	const char *metric_unit;
	/** Null terminated array of events used by the metric. */
	struct evsel **metric_events;
	/** Null terminated array of referenced metrics. */
	struct metric_ref *metric_refs;
	/** A value substituted for '?' during parsing. */
	int runtime;
};

struct metric_event *metricgroup__lookup(struct rblist *metric_events,
					 struct evsel *evsel,
					 bool create);
int metricgroup__parse_groups(const struct option *opt,
			      const char *str,
			      bool metric_no_group,
			      bool metric_no_merge,
			      struct rblist *metric_events);
const struct pmu_event *metricgroup__find_metric(const char *metric,
						 const struct pmu_events_map *map);
int metricgroup__parse_groups_test(struct evlist *evlist,
				   const struct pmu_events_map *map,
				   const char *str,
				   bool metric_no_group,
				   bool metric_no_merge,
				   struct rblist *metric_events);

void metricgroup__print(bool metrics, bool groups, char *filter,
			bool raw, bool details, const char *pmu_name);
bool metricgroup__has_metric(const char *metric);
int arch_get_runtimeparam(const struct pmu_event *pe __maybe_unused);
void metricgroup__rblist_exit(struct rblist *metric_events);

int metricgroup__copy_metric_events(struct evlist *evlist, struct cgroup *cgrp,
				    struct rblist *new_metric_events,
				    struct rblist *old_metric_events);
#endif
