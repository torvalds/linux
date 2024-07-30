/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PMU_EVENTS_H
#define PMU_EVENTS_H

#include <stdbool.h>
#include <stddef.h>

struct perf_pmu;

enum aggr_mode_class {
	PerChip = 1,
	PerCore
};

/**
 * enum metric_event_groups - How events within a pmu_metric should be grouped.
 */
enum metric_event_groups {
	/**
	 * @MetricGroupEvents: Default, group events within the metric.
	 */
	MetricGroupEvents = 0,
	/**
	 * @MetricNoGroupEvents: Don't group events for the metric.
	 */
	MetricNoGroupEvents = 1,
	/**
	 * @MetricNoGroupEventsNmi: Don't group events for the metric if the NMI
	 *                          watchdog is enabled.
	 */
	MetricNoGroupEventsNmi = 2,
	/**
	 * @MetricNoGroupEventsSmt: Don't group events for the metric if SMT is
	 *                          enabled.
	 */
	MetricNoGroupEventsSmt = 3,
};
/*
 * Describe each PMU event. Each CPU has a table of PMU events.
 */
struct pmu_event {
	const char *name;
	const char *compat;
	const char *event;
	const char *desc;
	const char *topic;
	const char *long_desc;
	const char *pmu;
	const char *unit;
	bool perpkg;
	bool deprecated;
};

struct pmu_metric {
	const char *pmu;
	const char *metric_name;
	const char *metric_group;
	const char *metric_expr;
	const char *metric_threshold;
	const char *unit;
	const char *compat;
	const char *desc;
	const char *long_desc;
	const char *metricgroup_no_group;
	const char *default_metricgroup_name;
	enum aggr_mode_class aggr_mode;
	enum metric_event_groups event_grouping;
};

struct pmu_events_table;
struct pmu_metrics_table;

#define PMU_EVENTS__NOT_FOUND -1000

typedef int (*pmu_event_iter_fn)(const struct pmu_event *pe,
				 const struct pmu_events_table *table,
				 void *data);

typedef int (*pmu_metric_iter_fn)(const struct pmu_metric *pm,
				  const struct pmu_metrics_table *table,
				  void *data);

int pmu_events_table__for_each_event(const struct pmu_events_table *table,
				    struct perf_pmu *pmu,
				    pmu_event_iter_fn fn,
				    void *data);
/*
 * Search for table and entry matching with pmu__name_match. Each matching event
 * has fn called on it. 0 implies to success/continue the search while non-zero
 * means to terminate. The special value PMU_EVENTS__NOT_FOUND is used to
 * indicate no event was found in one of the tables which doesn't terminate the
 * search of all tables.
 */
int pmu_events_table__find_event(const struct pmu_events_table *table,
                                 struct perf_pmu *pmu,
                                 const char *name,
                                 pmu_event_iter_fn fn,
				 void *data);
size_t pmu_events_table__num_events(const struct pmu_events_table *table,
				    struct perf_pmu *pmu);

int pmu_metrics_table__for_each_metric(const struct pmu_metrics_table *table, pmu_metric_iter_fn fn,
				     void *data);

const struct pmu_events_table *perf_pmu__find_events_table(struct perf_pmu *pmu);
const struct pmu_metrics_table *perf_pmu__find_metrics_table(struct perf_pmu *pmu);
const struct pmu_events_table *find_core_events_table(const char *arch, const char *cpuid);
const struct pmu_metrics_table *find_core_metrics_table(const char *arch, const char *cpuid);
int pmu_for_each_core_event(pmu_event_iter_fn fn, void *data);
int pmu_for_each_core_metric(pmu_metric_iter_fn fn, void *data);

const struct pmu_events_table *find_sys_events_table(const char *name);
const struct pmu_metrics_table *find_sys_metrics_table(const char *name);
int pmu_for_each_sys_event(pmu_event_iter_fn fn, void *data);
int pmu_for_each_sys_metric(pmu_metric_iter_fn fn, void *data);

const char *describe_metricgroup(const char *group);

#endif
