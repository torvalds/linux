/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_H
#define __LIBPERF_INTERNAL_H

#include <perf/core.h>

void libperf_print(enum libperf_print_level level,
		   const char *format, ...)
	__attribute__((format(printf, 2, 3)));

#define __pr(level, fmt, ...)   \
do {                            \
	libperf_print(level, "libperf: " fmt, ##__VA_ARGS__);     \
} while (0)

#define pr_err(fmt, ...)        __pr(LIBPERF_ERR, fmt, ##__VA_ARGS__)
#define pr_warning(fmt, ...)    __pr(LIBPERF_WARN, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)       __pr(LIBPERF_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)      __pr(LIBPERF_DEBUG, fmt, ##__VA_ARGS__)
#define pr_debug2(fmt, ...)     __pr(LIBPERF_DEBUG2, fmt, ##__VA_ARGS__)
#define pr_debug3(fmt, ...)     __pr(LIBPERF_DEBUG3, fmt, ##__VA_ARGS__)

#endif /* __LIBPERF_INTERNAL_H */
