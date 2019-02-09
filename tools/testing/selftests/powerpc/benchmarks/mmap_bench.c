/*
 * Copyright 2016, Anton Blanchard, Michael Ellerman, IBM Corp.
 * Licensed under GPLv2.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <time.h>
#include <getopt.h>

#include "utils.h"

#define ITERATIONS 5000000

#define MEMSIZE (1UL << 27)
#define PAGE_SIZE (1UL << 16)
#define CHUNK_COUNT (MEMSIZE/PAGE_SIZE)

static int pg_fault;
static int iterations = ITERATIONS;

static struct option options[] = {
	{ "pgfault", no_argument, &pg_fault, 1 },
	{ "iterations", required_argument, 0, 'i' },
	{ 0, },
};

static void usage(void)
{
	printf("mmap_bench <--pgfault> <--iterations count>\n");
}

int test_mmap(void)
{
	struct timespec ts_start, ts_end;
	unsigned long i = iterations;

	clock_gettime(CLOCK_MONOTONIC, &ts_start);

	while (i--) {
		char *c = mmap(NULL, MEMSIZE, PROT_READ|PROT_WRITE,
			       MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
		FAIL_IF(c == MAP_FAILED);
		if (pg_fault) {
			int count;
			for (count = 0; count < CHUNK_COUNT; count++)
				c[count << 16] = 'c';
		}
		munmap(c, MEMSIZE);
	}

	clock_gettime(CLOCK_MONOTONIC, &ts_end);

	printf("time = %.6f\n", ts_end.tv_sec - ts_start.tv_sec + (ts_end.tv_nsec - ts_start.tv_nsec) / 1e9);

	return 0;
}

int main(int argc, char *argv[])
{
	signed char c;
	while (1) {
		int option_index = 0;

		c = getopt_long(argc, argv, "", options, &option_index);

		if (c == -1)
			break;

		switch (c) {
		case 0:
			if (options[option_index].flag != 0)
				break;

			usage();
			exit(1);
			break;
		case 'i':
			iterations = atoi(optarg);
			break;
		default:
			usage();
			exit(1);
		}
	}

	test_harness_set_timeout(300);
	return test_harness(test_mmap, "mmap_bench");
}
