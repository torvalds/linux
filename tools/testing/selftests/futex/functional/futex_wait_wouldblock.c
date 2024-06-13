// SPDX-License-Identifier: GPL-2.0-or-later
/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 * DESCRIPTION
 *      Test if FUTEX_WAIT op returns -EWOULDBLOCK if the futex value differs
 *      from the expected one.
 *
 * AUTHOR
 *      Gowrishankar <gowrishankar.m@in.ibm.com>
 *
 * HISTORY
 *      2009-Nov-14: Initial version by Gowrishankar <gowrishankar.m@in.ibm.com>
 *
 *****************************************************************************/

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "futextest.h"
#include "futex2test.h"
#include "logging.h"

#define TEST_NAME "futex-wait-wouldblock"
#define timeout_ns 100000

void usage(char *prog)
{
	printf("Usage: %s\n", prog);
	printf("  -c	Use color\n");
	printf("  -h	Display this help message\n");
	printf("  -v L	Verbosity level: %d=QUIET %d=CRITICAL %d=INFO\n",
	       VQUIET, VCRITICAL, VINFO);
}

int main(int argc, char *argv[])
{
	struct timespec to = {.tv_sec = 0, .tv_nsec = timeout_ns};
	futex_t f1 = FUTEX_INITIALIZER;
	int res, ret = RET_PASS;
	int c;
	struct futex_waitv waitv = {
			.uaddr = (uintptr_t)&f1,
			.val = f1+1,
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
	ksft_print_msg("%s: Test the unexpected futex value in FUTEX_WAIT\n",
	       basename(argv[0]));

	info("Calling futex_wait on f1: %u @ %p with val=%u\n", f1, &f1, f1+1);
	res = futex_wait(&f1, f1+1, &to, FUTEX_PRIVATE_FLAG);
	if (!res || errno != EWOULDBLOCK) {
		ksft_test_result_fail("futex_wait returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_wait\n");
	}

	if (clock_gettime(CLOCK_MONOTONIC, &to)) {
		error("clock_gettime failed\n", errno);
		return errno;
	}

	to.tv_nsec += timeout_ns;

	if (to.tv_nsec >= 1000000000) {
		to.tv_sec++;
		to.tv_nsec -= 1000000000;
	}

	info("Calling futex_waitv on f1: %u @ %p with val=%u\n", f1, &f1, f1+1);
	res = futex_waitv(&waitv, 1, 0, &to, CLOCK_MONOTONIC);
	if (!res || errno != EWOULDBLOCK) {
		ksft_test_result_pass("futex_waitv returned: %d %s\n",
				      res ? errno : res,
				      res ? strerror(errno) : "");
		ret = RET_FAIL;
	} else {
		ksft_test_result_pass("futex_waitv\n");
	}

	ksft_print_cnts();
	return ret;
}
