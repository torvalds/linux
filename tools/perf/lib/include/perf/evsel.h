/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVSEL_H
#define __LIBPERF_EVSEL_H

#include <perf/core.h>

struct perf_evsel;
struct perf_event_attr;

LIBPERF_API void perf_evsel__init(struct perf_evsel *evsel,
				  struct perf_event_attr *attr);
LIBPERF_API struct perf_evsel *perf_evsel__new(struct perf_event_attr *attr);

#endif /* __LIBPERF_EVSEL_H */
