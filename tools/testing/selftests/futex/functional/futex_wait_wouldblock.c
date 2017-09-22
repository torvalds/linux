/******************************************************************************
 *
 *   Copyright © International Business Machines  Corp., 2009
 *
 *   This program is free software;  you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
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
	ksft_print_msg("%s: Test the unexpected futex value in FUTEX_WAIT\n",
	       basename(argv[0]));

	info("Calling futex_wait on f1: %u @ %p with val=%u\n", f1, &f1, f1+1);
	res = futex_wait(&f1, f1+1, &to, FUTEX_PRIVATE_FLAG);
	if (!res || errno != EWOULDBLOCK) {
		fail("futex_wait returned: %d %s\n",
		     res ? errno : res, res ? strerror(errno) : "");
		ret = RET_FAIL;
	}

	print_result(TEST_NAME, ret);
	return ret;
}
