/*
 * Copyright 2016, Anton Blanchard, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <linux/futex.h>

#include "utils.h"

#define ITERATIONS 100000000

#define futex(A, B, C, D, E, F)	 syscall(__NR_futex, A, B, C, D, E, F)

int test_futex(void)
{
	struct timespec ts_start, ts_end;
	unsigned long i = ITERATIONS;

	clock_gettime(CLOCK_MONOTONIC, &ts_start);

	while (i--) {
		unsigned int addr = 0;
		futex(&addr, FUTEX_WAKE, 1, NULL, NULL, 0);
	}

	clock_gettime(CLOCK_MONOTONIC, &ts_end);

	printf("time = %.6f\n", ts_end.tv_sec - ts_start.tv_sec + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9);

	return 0;
}

int main(void)
{
	return test_harness(test_futex, "futex_bench");
}
