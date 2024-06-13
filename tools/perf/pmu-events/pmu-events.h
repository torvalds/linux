/* SPDX-License-Identifier: GPL-2.0 */
#ifndef PMU_EVENTS_H
#define PMU_EVENTS_H

struct perf_pmu;

enum aggr_mode_class {
	PerChip = 1,
	PerCore
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
	const char *perpkg;
	const char *aggr_mode;
	const char *metric_expr;
	const char *metric_name;
	const char *metric_group;
	const char *deprecated;
	const char *metric_constraint;
};

struct pmu_events_table;

typedef int (*pmu_event_iter_fn)(const struct pmu_event *pe,
				 const struct pmu_events_table *table,
				 void *data);

int pmu_events_table_for_each_event(const struct pmu_events_table *table, pmu_event_iter_fn fn,
				    void *data);

const struct pmu_events_table *perf_pmu__find_table(struct perf_pmu *pmu);
const struct pmu_events_table *find_core_events_table(const char *arch, const char *cpuid);
int pmu_for_each_core_event(pmu_event_iter_fn fn, void *data);

const struct pmu_events_table *find_sys_events_table(const char *name);
int pmu_for_each_sys_event(pmu_event_iter_fn fn, void *data);

#endif
