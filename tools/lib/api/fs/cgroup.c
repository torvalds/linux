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
	char path_v1[PATH_MAX + 1], path_v2[PATH_MAX + 2], *path;
	char *token, *saved_ptr = NULL;

	fp = fopen("/proc/mounts", "r");
	if (!fp)
		return -1;

	/*
	 * in order to handle split hierarchy, we need to scan /proc/mounts
	 * and inspect every cgroupfs mount point to find one that has
	 * perf_event subsystem
	 */
	path_v1[0] = '\0';
	path_v2[0] = '\0';

	while (fscanf(fp, "%*s %"__stringify(PATH_MAX)"s %"__stringify(PATH_MAX)"s %"
				__stringify(PATH_MAX)"s %*d %*d\n",
				mountpoint, type, tokens) == 3) {

		if (!path_v1[0] && !strcmp(type, "cgroup")) {

			token = strtok_r(tokens, ",", &saved_ptr);

			while (token != NULL) {
				if (subsys && !strcmp(token, subsys)) {
					strcpy(path_v1, mountpoint);
					break;
				}
				token = strtok_r(NULL, ",", &saved_ptr);
			}
		}

		if (!path_v2[0] && !strcmp(type, "cgroup2"))
			strcpy(path_v2, mountpoint);

		if (path_v1[0] && path_v2[0])
			break;
	}
	fclose(fp);

	if (path_v1[0])
		path = path_v1;
	else if (path_v2[0])
		path = path_v2;
	else
		return -1;

	if (strlen(path) < maxlen) {
		strcpy(buf, path);
		return 0;
	}
	return -1;
}
