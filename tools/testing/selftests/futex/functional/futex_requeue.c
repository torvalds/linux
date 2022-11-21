// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright Collabora Ltd., 2021
 *
 * futex cmp requeue test by André Almeida <andrealmeid@collabora.com>
 */

#include <pthread.h>
#include <limits.h>
#include "logging.h"
#include "futextest.h"

#define TEST_NAME "futex-requeue"
#define timeout_ns  30000000
#define WAKE_WAIT_US 10000

volatile futex_t *f1;

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

	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	if (futex_wait(f1, *f1, &to, 0))
		printf("waiter failed errno %d\n", errno);

	return NULL;
}

int main(int argc, char *argv[])
{
	pthread_t waiter[10];
	int res, ret = RET_PASS;
	int c, i;
	volatile futex_t _f1 = 0;
	volatile futex_t f2 = 0;

	f1 = &_f1;

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
	ksft_set_plan(2);
	ksft_print_msg("%s: Test futex_requeue\n",
		       basename(argv[0]));

	/*
	 * Requeue a waiter from f1 to f2, and wake f2.
	 */
	if (pthread_create(&waiter[0], NULL, waiterfn, NULL))
		error("pthread_create failed\n", errno);

	usleep(WAKE_WAIT_US);

	info("Requeuing 1 futex from f1 to f2\n");
	res = futex_cmp_requeue(f1, 0, &f2, 0, 1, 0);
	if (res != 1) {
		ksft_test_result_fail("futex_requeue simple returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	}


	info("Waking 1 futex at f2\n");
	res = futex_wake(&f2, 1, 0);
	if (res != 1) {
		ksft_test_result_fail("futex_requeue simple returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_requeue simple succeeds\n");
	}


	/*
	 * Create 10 waiters at f1. At futex_requeue, wake 3 and requeue 7.
	 * At futex_wake, wake INT_MAX (should be exactly 7).
	 */
	for (i = 0; i < 10; i++) {
		if (pthread_create(&waiter[i], NULL, waiterfn, NULL))
			error("pthread_create failed\n", errno);
	}

	usleep(WAKE_WAIT_US);

	info("Waking 3 futexes at f1 and requeuing 7 futexes from f1 to f2\n");
	res = futex_cmp_requeue(f1, 0, &f2, 3, 7, 0);
	if (res != 10) {
		ksft_test_result_fail("futex_requeue many returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	}

	info("Waking INT_MAX futexes at f2\n");
	res = futex_wake(&f2, INT_MAX, 0);
	if (res != 7) {
		ksft_test_result_fail("futex_requeue many returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_requeue many succeeds\n");
	}

	ksft_print_cnts();
	return ret;
}
