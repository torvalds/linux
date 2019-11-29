/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_CORE_H
#define __LIBPERF_CORE_H

#include <stdarg.h>

#ifndef LIBPERF_API
#define LIBPERF_API __attribute__((visibility("default")))
#endif

enum libperf_print_level {
	LIBPERF_WARN,
	LIBPERF_INFO,
	LIBPERF_DEBUG,
};

typedef int (*libperf_print_fn_t)(enum libperf_print_level level,
				  const char *, va_list ap);

LIBPERF_API void libperf_init(libperf_print_fn_t fn);

#endif /* __LIBPERF_CORE_H */
