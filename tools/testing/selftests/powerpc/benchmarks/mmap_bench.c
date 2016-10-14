/*
 * Copyright 2016, Anton Blanchard, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>

#include "utils.h"

#define ITERATIONS 5000000

#define MEMSIZE (128 * 1024 * 1024)

int test_mmap(void)
{
	struct timespec ts_start, ts_end;
	unsigned long i = ITERATIONS;

	clock_gettime(CLOCK_MONOTONIC, &ts_start);

	while (i--) {
		char *c = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		FAIL_IF(c == MAP_FAILED);
		munmap(c, MEMSIZE);
	}

	clock_gettime(CLOCK_MONOTONIC, &ts_end);

	printf("time = %.6f\n", ts_end.tv_sec - ts_start.tv_sec + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9);

	return 0;
}

int main(void)
{
	return test_harness(test_mmap, "mmap_bench");
}
