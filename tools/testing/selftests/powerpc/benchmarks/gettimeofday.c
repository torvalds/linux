/*
 * Copyright 2015, Anton Blanchard, IBM Corp.
 * Licensed under GPLv2.
 */

#include <sys/time.h>
#include <stdio.h>

#include "utils.h"

static int test_gettimeofday(void)
{
	int i;

	struct timeval tv_start, tv_end;

	gettimeofday(&tv_start, NULL);

	for(i = 0; i < 100000000; i++) {
		gettimeofday(&tv_end, NULL);
	}

	printf("time = %.6f\n", tv_end.tv_sec - tv_start.tv_sec + (tv_end.tv_usec - tv_start.tv_usec) * 1e-6);

	return 0;
}

int main(void)
{
	return test_harness(test_gettimeofday, "gettimeofday");
}
