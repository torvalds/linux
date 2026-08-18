// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * futex_waitv() test by André Almeida <andrealmeid@collabora.com>
 *
 * Copyright 2021 Collabora Ltd.
 */

#include <errno.h>
#include <error.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/shm.h>

#include "futextest.h"
#include "futex2test.h"
#include "kselftest_harness.h"

#define WAKE_WAIT_US 10000
#define NR_FUTEXES 30
static struct futex_waitv waitv[NR_FUTEXES];
u_int32_t futexes[NR_FUTEXES] = {0};


void *waiterfn(void *arg)
{
	struct __test_metadata *_metadata = (struct __test_metadata *)arg;
	struct timespec to;
	int res;

	/* setting absolute timeout for futex2 */
	ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &to), 0)
		TH_LOG("gettime64 failed");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res < 0) {
		EXPECT_EQ(res, NR_FUTEXES - 1)
			TH_LOG("futex_waitv failed: %s", strerror(errno));
	} else {
		EXPECT_EQ(res, NR_FUTEXES - 1)
			TH_LOG("futex_waitv returned %d, expected %d", res, NR_FUTEXES - 1);
	}

	return NULL;
}

TEST(private_waitv)
{
	pthread_t waiter;
	int res, i;

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	for (i = 0; i < NR_FUTEXES; i++) {
		waitv[i].uaddr = (uintptr_t)&futexes[i];
		waitv[i].flags = FUTEX_32 | FUTEX_PRIVATE_FLAG;
		waitv[i].val = 0;
		waitv[i].__reserved = 0;
	}

	/* Private waitv */
	ASSERT_EQ(pthread_create(&waiter, NULL, waiterfn, _metadata), 0)
		TH_LOG("pthread_create failed");

	usleep(WAKE_WAIT_US);

	res = futex_wake(u64_to_ptr(waitv[NR_FUTEXES - 1].uaddr), 1, FUTEX_PRIVATE_FLAG);
	EXPECT_EQ(res, 1)
		TH_LOG("futex_wake private returned: %d %s", res, res < 0 ? strerror(errno) : "");
}

TEST(shared_waitv)
{
	pthread_t waiter;
	int res, i;

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	/* Shared waitv */
	for (i = 0; i < NR_FUTEXES; i++) {
		int shm_id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);

		if (shm_id < 0) {
			if (errno == ENOSYS)
				SKIP(return, "shmget syscall not supported");
			ASSERT_GE(shm_id, 0)
				TH_LOG("shmget failed");
		}

		unsigned int *shared_data = shmat(shm_id, NULL, 0);

		*shared_data = 0;
		waitv[i].uaddr = (uintptr_t)shared_data;
		waitv[i].flags = FUTEX_32;
		waitv[i].val = 0;
		waitv[i].__reserved = 0;
	}

	ASSERT_EQ(pthread_create(&waiter, NULL, waiterfn, _metadata), 0)
		TH_LOG("pthread_create failed");

	usleep(WAKE_WAIT_US);

	res = futex_wake(u64_to_ptr(waitv[NR_FUTEXES - 1].uaddr), 1, 0);
	EXPECT_EQ(res, 1)
		TH_LOG("futex_wake shared returned: %d %s", res, res < 0 ? strerror(errno) : "");

	for (i = 0; i < NR_FUTEXES; i++)
		shmdt(u64_to_ptr(waitv[i].uaddr));
}

TEST(invalid_flag)
{
	struct timespec to;
	int res;

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	/* Testing a waiter without FUTEX_32 flag */
	waitv[0].flags = FUTEX_PRIVATE_FLAG;

	ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &to), 0)
		TH_LOG("gettime64 failed");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);

	EXPECT_EQ(res, -1)
		TH_LOG("futex_waitv returned unexpected result: %d", res);
	if (res == -1) {
		EXPECT_EQ(errno, EINVAL)
			TH_LOG("futex_waitv returned unexpected errno: %d", errno);
	}
}

TEST(unaligned_address)
{
	struct timespec to;
	int res;

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	/* Testing a waiter with an unaligned address */
	waitv[0].flags = FUTEX_PRIVATE_FLAG | FUTEX_32;
	waitv[0].uaddr = 1;

	ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &to), 0)
		TH_LOG("gettime64 failed");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);

	EXPECT_EQ(res, -1)
		TH_LOG("futex_waitv returned unexpected result: %d", res);
	if (res == -1) {
		EXPECT_EQ(errno, EINVAL)
			TH_LOG("futex_waitv returned unexpected errno: %d", errno);
	}
}

TEST(null_address)
{
	struct timespec to;
	int res;

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	/* Testing a NULL address for waiters.uaddr */
	waitv[0].uaddr = 0x00000000;

	ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &to), 0)
		TH_LOG("gettime64 failed");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);

	EXPECT_EQ(res, -1)
		TH_LOG("futex_waitv returned unexpected result: %d", res);
	if (res == -1) {
		EXPECT_EQ(errno, EINVAL)
			TH_LOG("futex_waitv returned unexpected errno: %d", errno);
	}

	/* Testing a NULL address for *waiters */
	ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &to), 0)
		TH_LOG("gettime64 failed");

	to.tv_sec++;

	res = futex_waitv(NULL, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);

	EXPECT_EQ(res, -1)
		TH_LOG("futex_waitv returned unexpected result: %d", res);
	if (res == -1) {
		EXPECT_EQ(errno, EINVAL)
			TH_LOG("futex_waitv returned unexpected errno: %d", errno);
	}
}

TEST(invalid_clockid)
{
	struct timespec to;
	int res;

	if (!is_futex_waitv_supported())
		SKIP(return, "futex_waitv syscall not supported");

	/* Testing an invalid clockid */
	ASSERT_EQ(clock_gettime(CLOCK_MONOTONIC, &to), 0)
		TH_LOG("gettime64 failed");

	to.tv_sec++;

	res = futex_waitv(NULL, NR_FUTEXES, 0, &to, CLOCK_TAI);

	EXPECT_EQ(res, -1)
		TH_LOG("futex_waitv returned unexpected result: %d", res);
	if (res == -1) {
		EXPECT_EQ(errno, EINVAL)
			TH_LOG("futex_waitv returned unexpected errno: %d", errno);
	}
}

TEST_HARNESS_MAIN
