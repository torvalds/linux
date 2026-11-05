/* SPDX-License-Identifier: GPL-2.0 */
#ifndef NOMMU_H
#define NOMMU_H

#include <stdio.h>
#include <string.h>

/* Returns 1 for NOMMU, 0 for MMU, and -1 if /proc/meminfo is unavailable. */
static inline int ksft_is_nommu(void)
{
	FILE *fp;
	char line[256];
	int nommu = 0;

	fp = fopen("/proc/meminfo", "r");
	if (!fp)
		return -1;

	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "MmapCopy:", sizeof("MmapCopy:") - 1) == 0) {
			nommu = 1;
			break;
		}
	}

	if (ferror(fp))
		nommu = -1;
	fclose(fp);

	return nommu;
}

#endif
