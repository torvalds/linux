/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_THREADMAP_H
#define __LIBPERF_THREADMAP_H

#include <perf/core.h>
#include <sys/types.h>

struct perf_thread_map;

LIBPERF_API struct perf_thread_map *perf_thread_map__new_dummy(void);
LIBPERF_API struct perf_thread_map *perf_thread_map__new_array(int nr_threads, pid_t *array);

LIBPERF_API void perf_thread_map__set_pid(struct perf_thread_map *map, int idx, pid_t pid);
LIBPERF_API char *perf_thread_map__comm(struct perf_thread_map *map, int idx);
LIBPERF_API int perf_thread_map__nr(struct perf_thread_map *threads);
LIBPERF_API pid_t perf_thread_map__pid(struct perf_thread_map *map, int idx);

LIBPERF_API struct perf_thread_map *perf_thread_map__get(struct perf_thread_map *map);
LIBPERF_API void perf_thread_map__put(struct perf_thread_map *map);

#endif /* __LIBPERF_THREADMAP_H */
