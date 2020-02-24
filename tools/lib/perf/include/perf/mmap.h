/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_MMAP_H
#define __LIBPERF_MMAP_H

#include <perf/core.h>

struct perf_mmap;
union perf_event;

LIBPERF_API void perf_mmap__consume(struct perf_mmap *map);
LIBPERF_API int perf_mmap__read_init(struct perf_mmap *map);
LIBPERF_API void perf_mmap__read_done(struct perf_mmap *map);
LIBPERF_API union perf_event *perf_mmap__read_event(struct perf_mmap *map);

#endif /* __LIBPERF_MMAP_H */
