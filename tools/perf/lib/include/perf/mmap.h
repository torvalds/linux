/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_MMAP_H
#define __LIBPERF_MMAP_H

#include <perf/core.h>

struct perf_mmap;

LIBPERF_API void perf_mmap__consume(struct perf_mmap *map);

#endif /* __LIBPERF_MMAP_H */
