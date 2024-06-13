// SPDX-License-Identifier: GPL-2.0
static int find_map(void **start, void **end, const char *name)
{
	FILE *maps;
	char line[128];
	int found = 0;

	maps = fopen("/proc/self/maps", "r");
	if (!maps) {
		fprintf(stderr, "cannot open maps\n");
		return -1;
	}

	while (!found && fgets(line, sizeof(line), maps)) {
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

	fclose(maps);
	return !found;
}
