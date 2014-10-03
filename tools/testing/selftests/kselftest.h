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

/* counters */
struct ksft_count {
	unsigned int ksft_pass;
	unsigned int ksft_fail;
	unsigned int ksft_xfail;
	unsigned int ksft_xpass;
	unsigned int ksft_xskip;
};

static struct ksft_count ksft_cnt;

static inline void ksft_inc_pass_cnt(void) { ksft_cnt.ksft_pass++; }
static inline void ksft_inc_fail_cnt(void) { ksft_cnt.ksft_fail++; }
static inline void ksft_inc_xfail_cnt(void) { ksft_cnt.ksft_xfail++; }
static inline void ksft_inc_xpass_cnt(void) { ksft_cnt.ksft_xpass++; }
static inline void ksft_inc_xskip_cnt(void) { ksft_cnt.ksft_xskip++; }

static inline void ksft_print_cnts(void)
{
	printf("Pass: %d Fail: %d Xfail: %d Xpass: %d, Xskip: %d\n",
		ksft_cnt.ksft_pass, ksft_cnt.ksft_fail,
		ksft_cnt.ksft_xfail, ksft_cnt.ksft_xpass,
		ksft_cnt.ksft_xskip);
}

static inline int ksft_exit_pass(void)
{
	exit(0);
}
static inline int ksft_exit_fail(void)
{
	exit(1);
}
static inline int ksft_exit_xfail(void)
{
	exit(2);
}
static inline int ksft_exit_xpass(void)
{
	exit(3);
}
static inline int ksft_exit_skip(void)
{
	exit(4);
}

#endif /* __KSELFTEST_H */
