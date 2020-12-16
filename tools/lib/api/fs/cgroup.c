// SPDX-License-Identifier: GPL-2.0
#include <linux/stringify.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fs.h"

int cgroupfs_find_mountpoint(char *buf, size_t maxlen, const char *subsys)
{
	FILE *fp;
	char mountpoint[PATH_MAX + 1], tokens[PATH_MAX + 1], type[PATH_MAX + 1];
	char path_v2[PATH_MAX + 1];
	char *token, *saved_ptr = NULL;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return -1;

	/*
	 * in order to handle split hierarchy, we need to scan /proc/mounts
	 * and inspect every cgroupfs mount point to find one that has
	 * the given subsystem.  If we found v1, just use it.  If not we can
	 * use v2 path as a fallback.
	 */
	path_v2[0] = '\0';

	while (fscanf(fp, "%*s %"__stringify(PATH_MAX)"s %"__stringify(PATH_MAX)"s %"
				__stringify(PATH_MAX)"s %*d %*d\n",
				mountpoint, type, tokens) == 3) {

		if (!strcmp(type, "cgroup")) {

			token = strtok_r(tokens, ",", &saved_ptr);

			while (token != NULL) {
				if (subsys && !strcmp(token, subsys)) {
					/* found */
					fclose(fp);

					if (strlen(mountpoint) < maxlen) {
						strcpy(buf, mountpoint);
						return 0;
					}
					return -1;
				}
				token = strtok_r(NULL, ",", &saved_ptr);
			}
		}

		if (!strcmp(type, "cgroup2"))
			strcpy(path_v2, mountpoint);
	}
	fclose(fp);

	if (path_v2[0] && strlen(path_v2) < maxlen) {
		strcpy(buf, path_v2);
		return 0;
	}
	return -1;
}
