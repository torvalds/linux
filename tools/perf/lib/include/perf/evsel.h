/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVSEL_H
#define __LIBPERF_EVSEL_H

#include <perf/core.h>

struct perf_evsel;

LIBPERF_API void perf_evsel__init(struct perf_evsel *evsel);

#endif /* __LIBPERF_EVSEL_H */
