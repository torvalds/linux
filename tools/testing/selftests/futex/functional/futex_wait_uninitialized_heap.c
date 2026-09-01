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

#include <errno.h>
#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <syscall.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "futextest.h"
#include "kselftest_harness.h"

#define WAIT_US 5000000

static int child_blocked = 1;
static bool child_ret;
void *buf;

void *wait_thread(void *arg)
{
	struct __test_metadata *_metadata = (struct __test_metadata *)arg;
	int res;

	child_ret = true;
	res = futex_wait(buf, 1, NULL, 0);
	child_blocked = 0;

	if (res != 0 && errno != EWOULDBLOCK) {
		EXPECT_EQ(res, 0)
			TH_LOG("futex failure: %s", strerror(errno));
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
	ASSERT_NE(buf, MAP_FAILED)
		TH_LOG("mmap failed: %s", strerror(errno));

	ret = pthread_create(&thr, NULL, wait_thread, _metadata);
	ASSERT_EQ(ret, 0)
		TH_LOG("pthread_create failed");

	TH_LOG("waiting %dus for child to return", WAIT_US);
	usleep(WAIT_US);

	EXPECT_EQ(child_blocked, 0)
		TH_LOG("child blocked in kernel");
	EXPECT_TRUE(child_ret)
		TH_LOG("child error");

	pthread_join(thr, NULL);
	munmap(buf, page_size);
}

TEST_HARNESS_MAIN
