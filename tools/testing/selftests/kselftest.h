/* SPDX-License-Identifier: GPL-2.0 */
/*
 * kselftest.h:	kselftest framework return codes to include from
 *		selftests.
 *
 * Copyright (c) 2014 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 */
#ifndef __KSELFTEST_H
#define __KSELFTEST_H

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>

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
	printf("# Pass %d Fail %d Xfail %d Xpass %d Skip %d Error %d\n",
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

static inline void ksft_test_result_skip(const char *msg, ...)
{
	int saved_errno = errno;
	va_list args;

	ksft_cnt.ksft_xskip++;

	va_start(args, msg);
	printf("not ok %d # SKIP ", ksft_test_num());
	errno = saved_errno;
	vprintf(msg, args);
	va_end(args);
}

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
	printf("Bail out!\n");
	ksft_print_cnts();
	exit(KSFT_FAIL);
}

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
	if (msg) {
		int saved_errno = errno;
		va_list args;

		va_start(args, msg);
		printf("not ok %d # SKIP ", 1 + ksft_test_num());
		errno = saved_errno;
		vprintf(msg, args);
		va_end(args);
	} else {
		ksft_print_cnts();
	}
	exit(KSFT_SKIP);
}

#endif /* __KSELFTEST_H */
