/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_TESTS_H
#define __LIBPERF_INTERNAL_TESTS_H

#include <stdio.h>

int tests_failed;

#define __T_START					\
do {							\
	fprintf(stdout, "- running %s...", __FILE__);	\
	fflush(NULL);					\
	tests_failed = 0;				\
} while (0)

#define __T_END								\
do {									\
	if (tests_failed)						\
		fprintf(stdout, "  FAILED (%d)\n", tests_failed);	\
	else								\
		fprintf(stdout, "OK\n");				\
} while (0)

#define __T(text, cond)                                                          \
do {                                                                             \
	if (!(cond)) {                                                           \
		fprintf(stderr, "FAILED %s:%d %s\n", __FILE__, __LINE__, text);  \
		tests_failed++;                                                  \
		return -1;                                                       \
	}                                                                        \
} while (0)

#endif /* __LIBPERF_INTERNAL_TESTS_H */
