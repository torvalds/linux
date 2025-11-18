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
#include "../../kselftest_harness.h"

#define WAKE_WAIT_US 10000
#define NR_FUTEXES 30
static struct futex_waitv waitv[NR_FUTEXES];
u_int32_t futexes[NR_FUTEXES] = {0};

void *waiterfn(void *arg)
{
	struct timespec to;
	int res;

	/* setting absolute timeout for futex2 */
	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("gettime64 failed\n");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res < 0) {
		ksft_test_result_fail("futex_waitv returned: %d %s\n",
				      errno, strerror(errno));
	} else if (res != NR_FUTEXES - 1) {
		ksft_test_result_fail("futex_waitv returned: %d, expecting %d\n",
				      res, NR_FUTEXES - 1);
	}

	return NULL;
}

TEST(private_waitv)
{
	pthread_t waiter;
	int res, i;

	for (i = 0; i < NR_FUTEXES; i++) {
		waitv[i].uaddr = (uintptr_t)&futexes[i];
		waitv[i].flags = FUTEX_32 | FUTEX_PRIVATE_FLAG;
		waitv[i].val = 0;
		waitv[i].__reserved = 0;
	}

	/* Private waitv */
	if (pthread_create(&waiter, NULL, waiterfn, NULL))
		ksft_exit_fail_msg("pthread_create failed\n");

	usleep(WAKE_WAIT_US);

	res = futex_wake(u64_to_ptr(waitv[NR_FUTEXES - 1].uaddr), 1, FUTEX_PRIVATE_FLAG);
	if (res != 1) {
		ksft_test_result_fail("futex_wake private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv private\n");
	}
}

TEST(shared_waitv)
{
	pthread_t waiter;
	int res, i;

	/* Shared waitv */
	for (i = 0; i < NR_FUTEXES; i++) {
		int shm_id = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);

		if (shm_id < 0) {
			perror("shmget");
			exit(1);
		}

		unsigned int *shared_data = shmat(shm_id, NULL, 0);

		*shared_data = 0;
		waitv[i].uaddr = (uintptr_t)shared_data;
		waitv[i].flags = FUTEX_32;
		waitv[i].val = 0;
		waitv[i].__reserved = 0;
	}

	if (pthread_create(&waiter, NULL, waiterfn, NULL))
		ksft_exit_fail_msg("pthread_create failed\n");

	usleep(WAKE_WAIT_US);

	res = futex_wake(u64_to_ptr(waitv[NR_FUTEXES - 1].uaddr), 1, 0);
	if (res != 1) {
		ksft_test_result_fail("futex_wake shared returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv shared\n");
	}

	for (i = 0; i < NR_FUTEXES; i++)
		shmdt(u64_to_ptr(waitv[i].uaddr));
}

TEST(invalid_flag)
{
	struct timespec to;
	int res;

	/* Testing a waiter without FUTEX_32 flag */
	waitv[0].flags = FUTEX_PRIVATE_FLAG;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("gettime64 failed\n");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv without FUTEX_32\n");
	}
}

TEST(unaligned_address)
{
	struct timespec to;
	int res;

	/* Testing a waiter with an unaligned address */
	waitv[0].flags = FUTEX_PRIVATE_FLAG | FUTEX_32;
	waitv[0].uaddr = 1;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("gettime64 failed\n");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_wake private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv with an unaligned address\n");
	}
}

TEST(null_address)
{
	struct timespec to;
	int res;

	/* Testing a NULL address for waiters.uaddr */
	waitv[0].uaddr = 0x00000000;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("gettime64 failed\n");

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv NULL address in waitv.uaddr\n");
	}

	/* Testing a NULL address for *waiters */
	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("gettime64 failed\n");

	to.tv_sec++;

	res = futex_waitv(NULL, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv NULL address in *waiters\n");
	}
}

TEST(invalid_clockid)
{
	struct timespec to;
	int res;

	/* Testing an invalid clockid */
	if (clock_gettime(CLOCK_MONOTONIC, &to))
		ksft_exit_fail_msg("gettime64 failed\n");

	to.tv_sec++;

	res = futex_waitv(NULL, NR_FUTEXES, 0, &to, CLOCK_TAI);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
	} else {
		ksft_test_result_pass("futex_waitv invalid clockid\n");
	}
}

TEST_HARNESS_MAIN
