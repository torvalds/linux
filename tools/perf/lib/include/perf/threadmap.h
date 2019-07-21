/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_THREADMAP_H
#define __LIBPERF_THREADMAP_H

#include <perf/core.h>
#include <sys/types.h>

struct perf_thread_map;

LIBPERF_API struct perf_thread_map *perf_thread_map__new_dummy(void);

LIBPERF_API void perf_thread_map__set_pid(struct perf_thread_map *map, int thread, pid_t pid);

#endif /* __LIBPERF_THREADMAP_H */
