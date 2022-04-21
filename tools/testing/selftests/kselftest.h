/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kselftest.h:	low-level kselftest framework to include from
 *		selftest programs. When possible, please use
 *		kselftest_harness.h instead.
 *
 * Copyright (c) 2014 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * Using this API consists of first counting how many tests your code
 * has to run, and then starting up the reporting:
 *
 *     ksft_print_header();
 *     ksft_set_plan(total_number_of_tests);
 *
 * For each test, report any progress, debugging, etc with:
 *
 *     ksft_print_msg(fmt, ...);
 *
 * and finally report the pass/fail/skip/xfail state of the test with one of:
 *
 *     ksft_test_result(condition, fmt, ...);
 *     ksft_test_result_pass(fmt, ...);
 *     ksft_test_result_fail(fmt, ...);
 *     ksft_test_result_skip(fmt, ...);
 *     ksft_test_result_xfail(fmt, ...);
 *     ksft_test_result_error(fmt, ...);
 *
 * When all tests are finished, clean up and exit the program with one of:
 *
 *    ksft_finished();
 *    ksft_exit(condition);
 *    ksft_exit_pass();
 *    ksft_exit_fail();
 *
 * If the program wants to report details on why the entire program has
 * failed, it can instead exit with a message (this is usually done when
 * the program is aborting before finishing all tests):
 *
 *    ksft_exit_fail_msg(fmt, ...);
 *
 */
#ifndef __KSELFTEST_H
#define __KSELFTEST_H

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/* define kselftest exit codes */
#define KSFT_PASS  0
#define KSFT_FAIL  1
#define KSFT_XFAIL 2
#define KSFT_XPASS 3
#define KSFT_SKIP  4

/* counters */
struct ksft_count {
	unsigned int ksft_pass;
	unsigned int ksft_fail;
	unsigned int ksft_xfail;
	unsigned int ksft_xpass;
	unsigned int ksft_xskip;
	unsigned int ksft_error;
};

static struct ksft_count ksft_cnt;
static unsigned int ksft_plan;

static inline unsigned int ksft_test_num(void)
{
	return ksft_cnt.ksft_pass + ksft_cnt.ksft_fail +
		ksft_cnt.ksft_xfail + ksft_cnt.ksft_xpass +
		ksft_cnt.ksft_xskip + ksft_cnt.ksft_error;
}

static inline void ksft_inc_pass_cnt(void) { ksft_cnt.ksft_pass++; }
static inline void ksft_inc_fail_cnt(void) { ksft_cnt.ksft_fail++; }
static inline void ksft_inc_xfail_cnt(void) { ksft_cnt.ksft_xfail++; }
static inline void ksft_inc_xpass_cnt(void) { ksft_cnt.ksft_xpass++; }
static inline void ksft_inc_xskip_cnt(void) { ksft_cnt.ksft_xskip++; }
static inline void ksft_inc_error_cnt(void) { ksft_cnt.ksft_error++; }

static inline int ksft_get_pass_cnt(void) { return ksft_cnt.ksft_pass; }
static inline int ksft_get_fail_cnt(void) { return ksft_cnt.ksft_fail; }
static inline int ksft_get_xfail_cnt(void) { return ksft_cnt.ksft_xfail; }
static inline int ksft_get_xpass_cnt(void) { return ksft_cnt.ksft_xpass; }
static inline int ksft_get_xskip_cnt(void) { return ksft_cnt.ksft_xskip; }
static inline int ksft_get_error_cnt(void) { return ksft_cnt.ksft_error; }

static inline void ksft_print_header(void)
{
	if (!(getenv("KSFT_TAP_LEVEL")))
		printf("TAP version 13\n");
}

static inline void ksft_set_plan(unsigned int plan)
{
	ksft_plan = plan;
	printf("1..%d\n", ksft_plan);
}

static inline void ksft_print_cnts(void)
{
	if (ksft_plan != ksft_test_num())
		printf("# Planned tests != run tests (%u != %u)\n",
			ksft_plan, ksft_test_num());
	printf("# Totals: pass:%d fail:%d xfail:%d xpass:%d skip:%d error:%d\n",
		ksft_cnt.ksft_pass, ksft_cnt.ksft_fail,
		ksft_cnt.ksft_xfail, ksft_cnt.ksft_xpass,
		ksft_cnt.ksft_xskip, ksft_cnt.ksft_error);
}

static inline void ksft_print_msg(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	va_start(args, msg);
	printf("# ");
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

static inline void ksft_test_result_pass(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	ksft_cnt.ksft_pass++;

	va_start(args, msg);
	printf("ok %d ", ksft_test_num());
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

static inline void ksft_test_result_fail(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	ksft_cnt.ksft_fail++;

	va_start(args, msg);
	printf("not ok %d ", ksft_test_num());
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

/**
 * ksft_test_result() - Report test success based on truth of condition
 *
 * @condition: if true, report test success, otherwise failure.
 */
#define ksft_test_result(condition, fmt, ...) do {	\
	if (!!(condition))				\
		ksft_test_result_pass(fmt, ##__VA_ARGS__);\
	else						\
		ksft_test_result_fail(fmt, ##__VA_ARGS__);\
	} while (0)

static inline void ksft_test_result_xfail(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	ksft_cnt.ksft_xfail++;

	va_start(args, msg);
	printf("ok %d # XFAIL ", ksft_test_num());
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

static inline void ksft_test_result_skip(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	ksft_cnt.ksft_xskip++;

	va_start(args, msg);
	printf("ok %d # SKIP ", ksft_test_num());
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

/* TODO: how does "error" differ from "fail" or "skip"? */
static inline void ksft_test_result_error(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	ksft_cnt.ksft_error++;

	va_start(args, msg);
	printf("not ok %d # error ", ksft_test_num());
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

static inline int ksft_exit_pass(void)
{
	ksft_print_cnts();
	exit(KSFT_PASS);
}

static inline int ksft_exit_fail(void)
{
	ksft_print_cnts();
	exit(KSFT_FAIL);
}

/**
 * ksft_exit() - Exit selftest based on truth of condition
 *
 * @condition: if true, exit self test with success, otherwise fail.
 */
#define ksft_exit(condition) do {	\
	if (!!(condition))		\
		ksft_exit_pass();	\
	else				\
		ksft_exit_fail();	\
	} while (0)

/**
 * ksft_finished() - Exit selftest with success if all tests passed
 */
#define ksft_finished()			\
	ksft_exit(ksft_plan ==		\
		  ksft_cnt.ksft_pass +	\
		  ksft_cnt.ksft_xfail +	\
		  ksft_cnt.ksft_xskip)

static inline int ksft_exit_fail_msg(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	va_start(args, msg);
	printf("Bail out! ");
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);

	ksft_print_cnts();
	exit(KSFT_FAIL);
}

static inline int ksft_exit_xfail(void)
{
	ksft_print_cnts();
	exit(KSFT_XFAIL);
}

static inline int ksft_exit_xpass(void)
{
	ksft_print_cnts();
	exit(KSFT_XPASS);
}

static inline int ksft_exit_skip(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	va_start(args, msg);

	/*
	 * FIXME: several tests misuse ksft_exit_skip so produce
	 * something sensible if some tests have already been run
	 * or a plan has been printed.  Those tests should use
	 * ksft_test_result_skip or ksft_exit_fail_msg instead.
	 */
	if (ksft_plan || ksft_test_num()) {
		ksft_cnt.ksft_xskip++;
		printf("ok %d # SKIP ", 1 + ksft_test_num());
	} else {
		printf("1..0 # SKIP ");
	}
	if (msg) {
		errno = saved_errno;
		vprintf(msg, args);
		va_end(args);
	}
	if (ksft_test_num())
		ksft_print_cnts();
	exit(KSFT_SKIP);
}

#endif /* __KSELFTEST_H */
