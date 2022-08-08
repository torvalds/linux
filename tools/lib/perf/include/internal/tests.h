/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LIBPERF_INTERNAL_TESTS_H
#define __LIBPERF_INTERNAL_TESTS_H

#include <stdio.h>
#include <unistd.h>

int tests_failed;
int tests_verbose;

static inline int get_verbose(char **argv, int argc)
{
	int c;
	int verbose = 0;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c)
		{
		case 'v':
			verbose = 1;
			break;
		default:
			break;
		}
	}
	return verbose;
}

#define __T_START					\
do {							\
	tests_verbose = get_verbose(argv, argc);	\
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

#define __T_VERBOSE(...)						\
do {									\
	if (tests_verbose) {						\
		if (tests_verbose == 1) {				\
			fputc('\n', stderr);				\
			tests_verbose++;				\
		}							\
		fprintf(stderr, ##__VA_ARGS__);				\
	}								\
} while (0)

#endif /* __LIBPERF_INTERNAL_TESTS_H */
