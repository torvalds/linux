/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __TOOL_PMU_H
#define __TOOL_PMU_H

#include "pmu.h"

struct evsel;
struct perf_thread_map;
struct print_callbacks;

enum tool_pmu_event {
	TOOL_PMU__EVENT_NONE = 0,
	TOOL_PMU__EVENT_DURATION_TIME = 1,
	TOOL_PMU__EVENT_USER_TIME = 2,
	TOOL_PMU__EVENT_SYSTEM_TIME = 3,

	TOOL_PMU__EVENT_MAX,
};

#define perf_tool_event__for_each_event(ev)				\
	for ((ev) = TOOL_PMU__EVENT_DURATION_TIME; (ev) < TOOL_PMU__EVENT_MAX; ev++)

static inline size_t tool_pmu__num_events(void)
{
	return TOOL_PMU__EVENT_MAX - 1;
}

const char *perf_tool_event__to_str(enum tool_pmu_event ev);
enum tool_pmu_event perf_tool_event__from_str(const char *str);
int tool_pmu__config_terms(struct perf_event_attr *attr,
			   struct parse_events_terms *terms,
			   struct parse_events_error *err);
int tool_pmu__for_each_event_cb(struct perf_pmu *pmu, void *state, pmu_event_callback cb);

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
int evsel__read_tool(struct evsel *evsel, int cpu_map_idx, int thread);

struct perf_pmu *perf_pmus__tool_pmu(void);

#endif /* __TOOL_PMU_H */
