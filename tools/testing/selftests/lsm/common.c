// SPDX-License-Identifier: GPL-2.0
/*
 * Linux Security Module infrastructure tests
 *
 * Copyright © 2023 Casey Schaufler <casey@schaufler-ca.com>
 */

#define _GNU_SOURCE
#include <linux/lsm.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include "common.h"

#define PROCATTR "/proc/self/attr/"

int read_proc_attr(const char *attr, char *value, size_t size)
{
	int fd;
	int len;
	char *path;

	len = strlen(PROCATTR) + strlen(attr) + 1;
	path = calloc(len, 1);
	if (path == NULL)
		return -1;
	sprintf(path, "%s%s", PROCATTR, attr);

	fd = open(path, O_RDONLY);
	free(path);

	if (fd < 0)
		return -1;
	len = read(fd, value, size);

	close(fd);

	/* Ensure value is terminated */
	if (len <= 0 || len == size)
		return -1;
	value[len] = '\0';

	path = strchr(value, '\n');
	if (path)
		*path = '\0';

	return 0;
}

int read_sysfs_lsms(char *lsms, size_t size)
{
	FILE *fp;
	size_t red;

	fp = fopen("/sys/kernel/security/lsm", "r");
	if (fp == NULL)
		return -1;
	red = fread(lsms, 1, size, fp);
	fclose(fp);

	if (red <= 0 || red == size)
		return -1;
	lsms[red] = '\0';
	return 0;
}

int attr_lsm_count(void)
{
	char *names = calloc(sysconf(_SC_PAGESIZE), 1);
	int count = 0;

	if (!names)
		return 0;

	if (read_sysfs_lsms(names, sysconf(_SC_PAGESIZE)))
		return 0;

	if (strstr(names, "selinux"))
		count++;
	if (strstr(names, "smack"))
		count++;
	if (strstr(names, "apparmor"))
		count++;

	return count;
}
