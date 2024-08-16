// SPDX-License-Identifier: GPL-2.0
/*
 * Artificial memory access program for testing DAMON.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int main(int argc, char *argv[])
{
	char **regions;
	clock_t start_clock;
	int nr_regions;
	int sz_region;
	int access_time_ms;
	int i;

	if (argc != 4) {
		printf("Usage: %s <number> <size (bytes)> <time (ms)>\n",
				argv[0]);
		return -1;
	}

	nr_regions = atoi(argv[1]);
	sz_region = atoi(argv[2]);
	access_time_ms = atoi(argv[3]);

	regions = malloc(sizeof(*regions) * nr_regions);
	for (i = 0; i < nr_regions; i++)
		regions[i] = malloc(sz_region);

	for (i = 0; i < nr_regions; i++) {
		start_clock = clock();
		while ((clock() - start_clock) * 1000 / CLOCKS_PER_SEC <
				access_time_ms)
			memset(regions[i], i, sz_region);
	}
	return 0;
}
