/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_EVLIST_H
#define __LIBPERF_EVLIST_H

#include <perf/core.h>

struct perf_evlist;
struct perf_evsel;
struct perf_cpu_map;
struct perf_thread_map;

LIBPERF_API void perf_evlist__init(struct perf_evlist *evlist);
LIBPERF_API void perf_evlist__add(struct perf_evlist *evlist,
				  struct perf_evsel *evsel);
LIBPERF_API void perf_evlist__remove(struct perf_evlist *evlist,
				     struct perf_evsel *evsel);
LIBPERF_API struct perf_evlist *perf_evlist__new(void);
LIBPERF_API void perf_evlist__delete(struct perf_evlist *evlist);
LIBPERF_API struct perf_evsel* perf_evlist__next(struct perf_evlist *evlist,
						 struct perf_evsel *evsel);
LIBPERF_API int perf_evlist__open(struct perf_evlist *evlist);
LIBPERF_API void perf_evlist__close(struct perf_evlist *evlist);
LIBPERF_API void perf_evlist__enable(struct perf_evlist *evlist);
LIBPERF_API void perf_evlist__disable(struct perf_evlist *evlist);

#define perf_evlist__for_each_evsel(evlist, pos)	\
	for ((pos) = perf_evlist__next((evlist), NULL);	\
	     (pos) != NULL;				\
	     (pos) = perf_evlist__next((evlist), (pos)))

LIBPERF_API void perf_evlist__set_maps(struct perf_evlist *evlist,
				       struct perf_cpu_map *cpus,
				       struct perf_thread_map *threads);
LIBPERF_API int perf_evlist__poll(struct perf_evlist *evlist, int timeout);

#endif /* __LIBPERF_EVLIST_H */
