// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      Block on a futex and wait for timeout.
 *
 * AUTHOR
 *      Darren Hart <dvhart@linux.intel.com>
 *
 * HISTORY
 *      2009-Nov-6: Initial version by Darren Hart <dvhart@linux.intel.com>
 *      2021-Apr-26: More test cases by André Almeida <andrealmeid@collabora.com>
 *
 *****************************************************************************/

#include <pthread.h>
#include "futextest.h"
#include "futex2test.h"
#include "logging.h"

#define TEST_NAME "futex-wait-timeout"

static long timeout_ns = 100000;	/* 100us default timeout */
static futex_t futex_pi;

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -t N	Timeout in nanoseconds (default: 100,000)\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

/*
 * Get a PI lock and hold it forever, so the main thread lock_pi will block
 * and we can test the timeout
 */
void *get_pi_lock(void *arg)
{
	int ret;
	volatile futex_t lock = 0;

	ret = futex_lock_pi(&futex_pi, NULL, 0, 0);
	if (ret != 0)
		error("futex_lock_pi failed\n", ret);

	/* Blocks forever */
	ret = futex_wait(&lock, 0, NULL, 0);
	error("futex_wait failed\n", ret);

	return NULL;
}

/*
 * Check if the function returned the expected error
 */
static void test_timeout(int res, int *ret, char *test_name, int err)
{
	if (!res || errno != err) {
		ksft_test_result_fail("%s returned %d\n", test_name,
				      res < 0 ? errno : res);
		*ret = RET_FAIL;
	} else {
		ksft_test_result_pass("%s succeeds\n", test_name);
	}
}

/*
 * Calculate absolute timeout and correct overflow
 */
static int futex_get_abs_timeout(clockid_t clockid, struct timespec *to,
				 long timeout_ns)
{
	if (clock_gettime(clockid, to)) {
		error("clock_gettime failed\n", errno);
		return errno;
	}

	to->tv_nsec += timeout_ns;

	if (to->tv_nsec >= 1000000000) {
		to->tv_sec++;
		to->tv_nsec -= 1000000000;
	}

	return 0;
}

int main(int argc, char *argv[])
{
	futex_t f1 = FUTEX_INITIALIZER;
	int res, ret = RET_PASS;
	struct timespec to;
	pthread_t thread;
	int c;
	struct futex_waitv waitv = {
			.uaddr = (uintptr_t)&f1,
			.val = f1,
			.flags = FUTEX_32,
			.__reserved = 0
		};

	while ((c = getopt(argc, argv, "cht:v:")) != -1) {
		switch (c) {
		case 'c':
			log_color(1);
			break;
		case 'h':
			usage(basename(argv[0]));
			exit(0);
		case 't':
			timeout_ns = atoi(optarg);
			break;
		case 'v':
			log_verbosity(atoi(optarg));
			break;
		default:
			usage(basename(argv[0]));
			exit(1);
		}
	}

	ksft_print_header();
	ksft_set_plan(9);
	ksft_print_msg("%s: Block on a futex and wait for timeout\n",
	       basename(argv[0]));
	ksft_print_msg("\tArguments: timeout=%ldns\n", timeout_ns);

	pthread_create(&thread, NULL, get_pi_lock, NULL);

	/* initialize relative timeout */
	to.tv_sec = 0;
	to.tv_nsec = timeout_ns;

	res = futex_wait(&f1, f1, &to, 0);
	test_timeout(res, &ret, "futex_wait relative", ETIMEDOUT);

	/* FUTEX_WAIT_BITSET with CLOCK_REALTIME */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		return RET_FAIL;
	res = futex_wait_bitset(&f1, f1, &to, 1, FUTEX_CLOCK_REALTIME);
	test_timeout(res, &ret, "futex_wait_bitset realtime", ETIMEDOUT);

	/* FUTEX_WAIT_BITSET with CLOCK_MONOTONIC */
	if (futex_get_abs_timeout(CLOCK_MONOTONIC, &to, timeout_ns))
		return RET_FAIL;
	res = futex_wait_bitset(&f1, f1, &to, 1, 0);
	test_timeout(res, &ret, "futex_wait_bitset monotonic", ETIMEDOUT);

	/* FUTEX_WAIT_REQUEUE_PI with CLOCK_REALTIME */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		return RET_FAIL;
	res = futex_wait_requeue_pi(&f1, f1, &futex_pi, &to, FUTEX_CLOCK_REALTIME);
	test_timeout(res, &ret, "futex_wait_requeue_pi realtime", ETIMEDOUT);

	/* FUTEX_WAIT_REQUEUE_PI with CLOCK_MONOTONIC */
	if (futex_get_abs_timeout(CLOCK_MONOTONIC, &to, timeout_ns))
		return RET_FAIL;
	res = futex_wait_requeue_pi(&f1, f1, &futex_pi, &to, 0);
	test_timeout(res, &ret, "futex_wait_requeue_pi monotonic", ETIMEDOUT);

	/*
	 * FUTEX_LOCK_PI with CLOCK_REALTIME
	 * Due to historical reasons, FUTEX_LOCK_PI supports only realtime
	 * clock, but requires the caller to not set CLOCK_REALTIME flag.
	 *
	 * If you call FUTEX_LOCK_PI with a monotonic clock, it'll be
	 * interpreted as a realtime clock, and (unless you mess your machine's
	 * time or your time machine) the monotonic clock value is always
	 * smaller than realtime and the syscall will timeout immediately.
	 */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		return RET_FAIL;
	res = futex_lock_pi(&futex_pi, &to, 0, 0);
	test_timeout(res, &ret, "futex_lock_pi realtime", ETIMEDOUT);

	/* Test operations that don't support FUTEX_CLOCK_REALTIME */
	res = futex_lock_pi(&futex_pi, NULL, 0, FUTEX_CLOCK_REALTIME);
	test_timeout(res, &ret, "futex_lock_pi invalid timeout flag", ENOSYS);

	/* futex_waitv with CLOCK_MONOTONIC */
	if (futex_get_abs_timeout(CLOCK_MONOTONIC, &to, timeout_ns))
		return RET_FAIL;
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_MONOTONIC);
	test_timeout(res, &ret, "futex_waitv monotonic", ETIMEDOUT);

	/* futex_waitv with CLOCK_REALTIME */
	if (futex_get_abs_timeout(CLOCK_REALTIME, &to, timeout_ns))
		return RET_FAIL;
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_REALTIME);
	test_timeout(res, &ret, "futex_waitv realtime", ETIMEDOUT);

	ksft_print_cnts();
	return ret;
}
