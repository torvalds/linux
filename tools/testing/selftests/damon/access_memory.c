// SPDX-License-Identifier: GPL-2.0
/*
 * Artificial memory access program for testing DAMON.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum access_mode {
	ACCESS_MODE_ONCE,
	ACCESS_MODE_REPEAT,
};

int main(int argc, char *argv[])
{
	char **regions;
	clock_t start_clock;
	int nr_regions;
	int sz_region;
	int access_time_ms;
	enum access_mode mode = ACCESS_MODE_ONCE;

	int i;

	if (argc < 4) {
		printf("Usage: %s <number> <size (bytes)> <time (ms)> [mode]\n",
				argv[0]);
		return -1;
	}

	nr_regions = atoi(argv[1]);
	sz_region = atoi(argv[2]);
	access_time_ms = atoi(argv[3]);

	if (argc > 4 && !strcmp(argv[4], "repeat"))
		mode = ACCESS_MODE_REPEAT;

	regions = malloc(sizeof(*regions) * nr_regions);
	for (i = 0; i < nr_regions; i++)
		regions[i] = malloc(sz_region);

	do {
		for (i = 0; i < nr_regions; i++) {
			start_clock = clock();
			while ((clock() - start_clock) * 1000 / CLOCKS_PER_SEC
					< access_time_ms)
				memset(regions[i], i, sz_region);
		}
	} while (mode == ACCESS_MODE_REPEAT);

	return 0;
}
