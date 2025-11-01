/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
#ifndef __DRM_PMU_H
#define __DRM_PMU_H
/*
 * Linux DRM clients expose information through usage stats as documented in
 * Documentation/gpu/drm-usage-stats.rst (available online at
 * https://docs.kernel.org/gpu/drm-usage-stats.html). This is a tool like PMU
 * that exposes DRM information.
 */

#include "pmu.h"
#include <stdbool.h>

struct list_head;
struct perf_thread_map;

void drm_pmu__exit(struct perf_pmu *pmu);
bool drm_pmu__have_event(const struct perf_pmu *pmu, const char *name);
int drm_pmu__for_each_event(const struct perf_pmu *pmu, void *state, pmu_event_callback cb);
size_t drm_pmu__num_events(const struct perf_pmu *pmu);
int drm_pmu__config_terms(const struct perf_pmu *pmu,
			  struct perf_event_attr *attr,
			  struct parse_events_terms *terms,
			  struct parse_events_error *err);
int drm_pmu__check_alias(const struct perf_pmu *pmu, struct parse_events_terms *terms,
			 struct perf_pmu_info *info, struct parse_events_error *err);


bool perf_pmu__is_drm(const struct perf_pmu *pmu);
bool evsel__is_drm(const struct evsel *evsel);

int perf_pmus__read_drm_pmus(struct list_head *pmus);

int evsel__drm_pmu_open(struct evsel *evsel,
			struct perf_thread_map *threads,
			int start_cpu_map_idx, int end_cpu_map_idx);
int evsel__drm_pmu_read(struct evsel *evsel, int cpu_map_idx, int thread);

#endif /* __DRM_PMU_H */
