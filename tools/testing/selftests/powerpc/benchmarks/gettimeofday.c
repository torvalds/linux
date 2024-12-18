// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright 2015, Anton Blanchard, IBM Corp.
 */

#include <sys/time.h>
#include <stdio.h>

#include "utils.h"

static int test_gettimeofday(void)
{
	int i;

	struct timeval tv_start, tv_end, tv_diff;

	gettimeofday(&tv_start, NULL);

	for(i = 0; i < 100000000; i++) {
		gettimeofday(&tv_end, NULL);
	}

	timersub(&tv_end, &tv_start, &tv_diff);

	printf("time = %.6f\n", tv_diff.tv_sec + (tv_diff.tv_usec) * 1e-6);

	return 0;
}

int main(void)
{
	return test_harness(test_gettimeofday, "gettimeofday");
}
