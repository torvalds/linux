/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVSEL_H
#define __LIBPERF_EVSEL_H

#include <perf/core.h>

struct perf_evsel;
struct perf_event_attr;
struct perf_cpu_map;
struct perf_thread_map;

LIBPERF_API void perf_evsel__init(struct perf_evsel *evsel,
				  struct perf_event_attr *attr);
LIBPERF_API struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr);
LIBPERF_API void perf_evsel__delete(struct perf_evsel *evsel);
LIBPERF_API int perf_evsel__open(struct perf_evsel *evsel, struct perf_cpu_map *cpus,
				 struct perf_thread_map *threads);

#endif /* __LIBPERF_EVSEL_H */
