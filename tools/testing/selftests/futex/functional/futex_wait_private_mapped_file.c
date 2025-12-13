// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 * Copyright FUJITSU LIMITED 2010
 * Copyright KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 * DESCRIPTION
 *      Internally, Futex has two handling mode, anon and file. The private file
 *      mapping is special. At first it behave as file, but after write anything
 *      it behave as anon. This test is intent to test such case.
 *
 * AUTHOR
 *      KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 * HISTORY
 *      2010-Jan-6: Initial version by KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 *****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <syscall.h>
#include <unistd.h>
#include <errno.h>
#include <linux/futex.h>
#include <pthread.h>
#include <libgen.h>
#include <signal.h>

#include "futextest.h"
#include "../../kselftest_harness.h"

#define PAGE_SZ 4096

char pad[PAGE_SZ] = {1};
futex_t val = 1;
char pad2[PAGE_SZ] = {1};

#define WAKE_WAIT_US 3000000
struct timespec wait_timeout = { .tv_sec = 5, .tv_nsec = 0};

void *thr_futex_wait(void *arg)
{
	int ret;

	ksft_print_dbg_msg("futex wait\n");
	ret = futex_wait(&val, 1, &wait_timeout, 0);
	if (ret && errno != EWOULDBLOCK && errno != ETIMEDOUT)
		ksft_exit_fail_msg("futex error.\n");

	if (ret && errno == ETIMEDOUT)
		ksft_exit_fail_msg("waiter timedout\n");

	ksft_print_dbg_msg("futex_wait: ret = %d, errno = %d\n", ret, errno);

	return NULL;
}

TEST(wait_private_mapped_file)
{
	pthread_t thr;
	int res;

	res = pthread_create(&thr, NULL, thr_futex_wait, NULL);
	if (res < 0)
		ksft_exit_fail_msg("pthread_create error\n");

	ksft_print_dbg_msg("wait a while\n");
	usleep(WAKE_WAIT_US);
	val = 2;
	res = futex_wake(&val, 1, 0);
	ksft_print_dbg_msg("futex_wake %d\n", res);
	if (res != 1)
		ksft_exit_fail_msg("FUTEX_WAKE didn't find the waiting thread.\n");

	ksft_print_dbg_msg("join\n");
	pthread_join(thr, NULL);

	ksft_test_result_pass("wait_private_mapped_file");
}

TEST_HARNESS_MAIN
