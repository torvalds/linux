// SPDX-License-Identifier: GPL-2.0
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int find_map(void **start, void **end, const char *name)
{
	FILE *maps;
	char *line = NULL;
	size_t len = 0;
	int found = 0;

	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		fprintf(stderr, "cannot open maps\n");
		return -1;
	}

	while (!found && getline(&line, &len, maps) != -1) {
		int m = -1;

		/* We care only about private r-x mappings. */
		if (2 != sscanf(line, "%p-%p r-xp %*x %*x:%*x %*u %n",
				start, end, &m))
			continue;
		if (m < 0)
			continue;

		if (!strncmp(&line[m], name, strlen(name)))
			found = 1;
	}

	free(line);
	fclose(maps);
	return !found;
}
