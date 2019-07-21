/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVLIST_H
#define __LIBPERF_EVLIST_H

#include <perf/core.h>

struct perf_evlist;
struct perf_evsel;

LIBPERF_API void perf_evlist__init(struct perf_evlist *evlist);
LIBPERF_API void perf_evlist__add(struct perf_evlist *evlist,
				  struct perf_evsel *evsel);

#endif /* __LIBPERF_EVLIST_H */
