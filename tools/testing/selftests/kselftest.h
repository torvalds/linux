/*
 * kselftest.h:	kselftest framework return codes to include from
 *		selftests.
 *
 * Copyright (c) 2014 Shuah Khan <shuahkh@osg.samsung.com>
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *
 * This file is released under the GPLv2.
 */
#ifndef __KSELFTEST_H
#define __KSELFTEST_H

#include <stdlib.h>
#include <unistd.h>

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
};

static struct ksft_count ksft_cnt;

static inline int ksft_test_num(void)
{
	return ksft_cnt.ksft_pass + ksft_cnt.ksft_fail +
		ksft_cnt.ksft_xfail + ksft_cnt.ksft_xpass +
		ksft_cnt.ksft_xskip;
}

static inline void ksft_inc_pass_cnt(void) { ksft_cnt.ksft_pass++; }
static inline void ksft_inc_fail_cnt(void) { ksft_cnt.ksft_fail++; }
static inline void ksft_inc_xfail_cnt(void) { ksft_cnt.ksft_xfail++; }
static inline void ksft_inc_xpass_cnt(void) { ksft_cnt.ksft_xpass++; }
static inline void ksft_inc_xskip_cnt(void) { ksft_cnt.ksft_xskip++; }

static inline void ksft_print_header(void)
{
	printf("TAP version 13\n");
}

static inline void ksft_print_cnts(void)
{
	printf("1..%d\n", ksft_test_num());
}

static inline void ksft_test_result_pass(const char *msg)
{
	ksft_cnt.ksft_pass++;
	printf("ok %d %s\n", ksft_test_num(), msg);
}

static inline void ksft_test_result_fail(const char *msg)
{
	ksft_cnt.ksft_fail++;
	printf("not ok %d %s\n", ksft_test_num(), msg);
}

static inline void ksft_test_result_skip(const char *msg)
{
	ksft_cnt.ksft_xskip++;
	printf("ok %d # skip %s\n", ksft_test_num(), msg);
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

static inline int ksft_exit_fail_msg(const char *msg)
{
	printf("Bail out! %s\n", msg);
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

static inline int ksft_exit_skip(void)
{
	ksft_print_cnts();
	exit(KSFT_SKIP);
}

#endif /* __KSELFTEST_H */
