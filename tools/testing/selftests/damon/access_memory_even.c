// SPDX-License-Identifier: GPL-2.0
/*
 * Artificial memory access program for testing DAMON.
 *
 * Receives number of regions and size of each region from user.  Allocate the
 * regions and repeatedly access even numbered (starting from zero) regions.
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

	if (argc != 3) {
		printf("Usage: %s <number> <size (bytes)>\n", argv[0]);
		return -1;
	}

	nr_regions = atoi(argv[1]);
	sz_region = atoi(argv[2]);

	regions = malloc(sizeof(*regions) * nr_regions);
	for (i = 0; i < nr_regions; i++)
		regions[i] = malloc(sz_region);

	while (1) {
		for (i = 0; i < nr_regions; i++) {
			if (i % 2 == 0)
				memset(regions[i], i, sz_region);
		}
	}
	return 0;
}
