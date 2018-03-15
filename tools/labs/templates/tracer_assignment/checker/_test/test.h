/*
 * generic test suite
 *
 * test macros and headers
 */

#ifndef TEST_H_
#define TEST_H_		1

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* to be defined by calling program */
extern int max_points;

/*
 * uncommend EXIT_IF_FAIL macro in order to stop test execution
 * at first failed test
 */

/*#define EXIT_IF_FAIL	1*/

#if defined(EXIT_IF_FAIL)
#define test_do_fail(points)		\
	do {				\
		printf("failed\n");	\
		exit(EXIT_FAILURE);	\
	} while (0)
#else
#define test_do_fail(points)		\
	printf("failed  [  0/%3d]\n", max_points)
#endif

#define test_do_pass(points)		\
	printf("passed  [%3d/%3d]\n", points, max_points)

#define test(message, test, points)				\
	do {							\
		size_t _i;					\
		int t = (test);					\
								\
		printf("%s", message);				\
		fflush(stdout);					\
								\
		for (_i = 0; _i < 60 - strlen(message); _i++)	\
			putchar('.');				\
								\
		if (!t)						\
			test_do_fail(points);			\
		else						\
			test_do_pass(points);			\
								\
		fflush(stdout);					\
	} while (0)

#ifdef __cplusplus
}
#endif

#endif
