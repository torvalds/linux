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
#include "logging.h"

#define TEST_NAME "futex-wait"
#define WAKE_WAIT_US 10000
#define NR_FUTEXES 30
static struct futex_waitv waitv[NR_FUTEXES];
u_int32_t futexes[NR_FUTEXES] = {0};

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

void *waiterfn(void *arg)
{
	struct timespec to;
	int res;

	/* setting absolute timeout for futex2 */
	if (clock_gettime(CLOCK_MONOTONIC, &to))
		error("gettime64 failed\n", errno);

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

int main(int argc, char *argv[])
{
	pthread_t waiter;
	int res, ret = RET_PASS;
	struct timespec to;
	int c, i;

	while ((c = getopt(argc, argv, "cht:v:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	ksft_print_header();
	ksft_set_plan(7);
	ksft_print_msg("%s: Test FUTEX_WAITV\n",
		       basename(argv[0]));

	for (i = 0; i < NR_FUTEXES; i++) {
		waitv[i].uaddr = (uintptr_t)&futexes[i];
		waitv[i].flags = FUTEX_32 | FUTEX_PRIVATE_FLAG;
		waitv[i].val = 0;
		waitv[i].__reserved = 0;
	}

	/* Private waitv */
	if (pthread_create(&waiter, NULL, waiterfn, NULL))
		error("pthread_create failed\n", errno);

	usleep(WAKE_WAIT_US);

	res = futex_wake(u64_to_ptr(waitv[NR_FUTEXES - 1].uaddr), 1, FUTEX_PRIVATE_FLAG);
	if (res != 1) {
		ksft_test_result_fail("futex_wake private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv private\n");
	}

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
		error("pthread_create failed\n", errno);

	usleep(WAKE_WAIT_US);

	res = futex_wake(u64_to_ptr(waitv[NR_FUTEXES - 1].uaddr), 1, 0);
	if (res != 1) {
		ksft_test_result_fail("futex_wake shared returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv shared\n");
	}

	for (i = 0; i < NR_FUTEXES; i++)
		shmdt(u64_to_ptr(waitv[i].uaddr));

	/* Testing a waiter without FUTEX_32 flag */
	waitv[0].flags = FUTEX_PRIVATE_FLAG;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		error("gettime64 failed\n", errno);

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv without FUTEX_32\n");
	}

	/* Testing a waiter with an unaligned address */
	waitv[0].flags = FUTEX_PRIVATE_FLAG | FUTEX_32;
	waitv[0].uaddr = 1;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		error("gettime64 failed\n", errno);

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_wake private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv with an unaligned address\n");
	}

	/* Testing a NULL address for waiters.uaddr */
	waitv[0].uaddr = 0x00000000;

	if (clock_gettime(CLOCK_MONOTONIC, &to))
		error("gettime64 failed\n", errno);

	to.tv_sec++;

	res = futex_waitv(waitv, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv NULL address in waitv.uaddr\n");
	}

	/* Testing a NULL address for *waiters */
	if (clock_gettime(CLOCK_MONOTONIC, &to))
		error("gettime64 failed\n", errno);

	to.tv_sec++;

	res = futex_waitv(NULL, NR_FUTEXES, 0, &to, CLOCK_MONOTONIC);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv NULL address in *waiters\n");
	}

	/* Testing an invalid clockid */
	if (clock_gettime(CLOCK_MONOTONIC, &to))
		error("gettime64 failed\n", errno);

	to.tv_sec++;

	res = futex_waitv(NULL, NR_FUTEXES, 0, &to, CLOCK_TAI);
	if (res == EINVAL) {
		ksft_test_result_fail("futex_waitv private returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv invalid clockid\n");
	}

	ksft_print_cnts();
	return ret;
}
