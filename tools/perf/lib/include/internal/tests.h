/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_TESTS_H
#define __LIBPERF_INTERNAL_TESTS_H

#include <stdio.h>

#define __T_START fprintf(stdout, "- running %s...", __FILE__)
#define __T_OK    fprintf(stdout, "OK\n")
#define __T_FAIL  fprintf(stdout, "FAIL\n")

#define __T(text, cond)                                                          \
do {                                                                             \
	if (!(cond)) {                                                           \
		fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, text);  \
		return -1;                                                       \
	}                                                                        \
} while (0)

#endif /* __LIBPERF_INTERNAL_TESTS_H */
