// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * Intel MIC User Space Tools.
 */

#include "mpssd.h"

#define PAGE_SIZE 4096

char *
readsysfs(char *dir, char *entry)
{
	char filename[PATH_MAX];
	char value[PAGE_SIZE];
	char *string = NULL;
	int fd;
	int len;

	if (dir == NULL)
		snprintf(filename, PATH_MAX, "%s/%s", MICSYSFSDIR, entry);
	else
		snprintf(filename, PATH_MAX,
			 "%s/%s/%s", MICSYSFSDIR, dir, entry);

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		mpsslog("Failed to open sysfs entry '%s': %s\n",
			filename, strerror(errno));
		return NULL;
	}

	len = read(fd, value, sizeof(value));
	if (len < 0) {
		mpsslog("Failed to read sysfs entry '%s': %s\n",
			filename, strerror(errno));
		goto readsys_ret;
	}
	if (len == 0)
		goto readsys_ret;

	value[len - 1] = '\0';

	string = malloc(strlen(value) + 1);
	if (string)
		strcpy(string, value);

readsys_ret:
	close(fd);
	return string;
}

int
setsysfs(char *dir, char *entry, char *value)
{
	char filename[PATH_MAX];
	char *oldvalue;
	int fd, ret = 0;

	if (dir == NULL)
		snprintf(filename, PATH_MAX, "%s/%s", MICSYSFSDIR, entry);
	else
		snprintf(filename, PATH_MAX, "%s/%s/%s",
			 MICSYSFSDIR, dir, entry);

	oldvalue = readsysfs(dir, entry);

	fd = open(filename, O_RDWR);
	if (fd < 0) {
		ret = errno;
		mpsslog("Failed to open sysfs entry '%s': %s\n",
			filename, strerror(errno));
		goto done;
	}

	if (!oldvalue || strcmp(value, oldvalue)) {
		if (write(fd, value, strlen(value)) < 0) {
			ret = errno;
			mpsslog("Failed to write new sysfs entry '%s': %s\n",
				filename, strerror(errno));
		}
	}
	close(fd);
done:
	if (oldvalue)
		free(oldvalue);
	return ret;
}
