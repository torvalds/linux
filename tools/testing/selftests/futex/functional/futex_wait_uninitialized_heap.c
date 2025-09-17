// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 * Copyright FUJITSU LIMITED 2010
 * Copyright KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 * DESCRIPTION
 *      Wait on uninitialized heap. It shold be zero and FUTEX_WAIT should
 *      return immediately. This test is intent to test zero page handling in
 *      futex.
 *
 * AUTHOR
 *      KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 * HISTORY
 *      2010-Jan-6: Initial version by KOSAKI Motohiro <kosaki.motohiro@jp.fujitsu.com>
 *
 *****************************************************************************/

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <syscall.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <linux/futex.h>
#include <libgen.h>

#include "futextest.h"
#include "../../kselftest_harness.h"

#define WAIT_US 5000000

static int child_blocked = 1;
static bool child_ret;
void *buf;

void *wait_thread(void *arg)
{
	int res;

	child_ret = true;
	res = futex_wait(buf, 1, NULL, 0);
	child_blocked = 0;

	if (res != 0 && errno != EWOULDBLOCK) {
		ksft_exit_fail_msg("futex failure\n");
		child_ret = false;
	}
	pthread_exit(NULL);
}

TEST(futex_wait_uninitialized_heap)
{
	long page_size;
	pthread_t thr;
	int ret;

	page_size = sysconf(_SC_PAGESIZE);

	buf = mmap(NULL, page_size, PROT_READ|PROT_WRITE,
		   MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
	if (buf == (void *)-1)
		ksft_exit_fail_msg("mmap\n");

	ret = pthread_create(&thr, NULL, wait_thread, NULL);
	if (ret)
		ksft_exit_fail_msg("pthread_create\n");

	ksft_print_dbg_msg("waiting %dus for child to return\n", WAIT_US);
	usleep(WAIT_US);

	if (child_blocked)
		ksft_test_result_fail("child blocked in kernel\n");

	if (!child_ret)
		ksft_test_result_fail("child error\n");
}

TEST_HARNESS_MAIN
