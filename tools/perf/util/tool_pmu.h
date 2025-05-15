/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOL_PMU_H
#define __TOOL_PMU_H

#include "pmu.h"

struct evsel;
struct perf_thread_map;
struct print_callbacks;

enum tool_pmu_event {
	TOOL_PMU__EVENT_NONE = 0,
	TOOL_PMU__EVENT_DURATION_TIME,
	TOOL_PMU__EVENT_USER_TIME,
	TOOL_PMU__EVENT_SYSTEM_TIME,
	TOOL_PMU__EVENT_HAS_PMEM,
	TOOL_PMU__EVENT_NUM_CORES,
	TOOL_PMU__EVENT_NUM_CPUS,
	TOOL_PMU__EVENT_NUM_CPUS_ONLINE,
	TOOL_PMU__EVENT_NUM_DIES,
	TOOL_PMU__EVENT_NUM_PACKAGES,
	TOOL_PMU__EVENT_SLOTS,
	TOOL_PMU__EVENT_SMT_ON,
	TOOL_PMU__EVENT_SYSTEM_TSC_FREQ,

	TOOL_PMU__EVENT_MAX,
};

#define tool_pmu__for_each_event(ev)					\
	for ((ev) = TOOL_PMU__EVENT_DURATION_TIME; (ev) < TOOL_PMU__EVENT_MAX; ev++)

const char *tool_pmu__event_to_str(enum tool_pmu_event ev);
enum tool_pmu_event tool_pmu__str_to_event(const char *str);
bool tool_pmu__skip_event(const char *name);
int tool_pmu__num_skip_events(void);

bool tool_pmu__read_event(enum tool_pmu_event ev, u64 *result);

u64 tool_pmu__cpu_slots_per_cycle(void);

bool perf_pmu__is_tool(const struct perf_pmu *pmu);

bool evsel__is_tool(const struct evsel *evsel);
enum tool_pmu_event evsel__tool_event(const struct evsel *evsel);
const char *evsel__tool_pmu_event_name(const struct evsel *evsel);
int evsel__tool_pmu_prepare_open(struct evsel *evsel,
				 struct perf_cpu_map *cpus,
				 int nthreads);
int evsel__tool_pmu_open(struct evsel *evsel,
			 struct perf_thread_map *threads,
			 int start_cpu_map_idx, int end_cpu_map_idx);
int evsel__tool_pmu_read(struct evsel *evsel, int cpu_map_idx, int thread);

struct perf_pmu *tool_pmu__new(void);

#endif /* __TOOL_PMU_H */
