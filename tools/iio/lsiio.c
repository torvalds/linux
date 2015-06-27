/*
 * Industrial I/O utilities - lsiio.c
 *
 * Copyright (c) 2010 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <string.h>
#include <dirent.h>
#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/dir.h>
#include "iio_utils.h"


static enum verbosity {
	VERBLEVEL_DEFAULT,	/* 0 gives lspci behaviour */
	VERBLEVEL_SENSORS,	/* 1 lists sensors */
} verblevel = VERBLEVEL_DEFAULT;

const char *type_device = "iio:device";
const char *type_trigger = "trigger";


static inline int check_prefix(const char *str, const char *prefix)
{
	return strlen(str) > strlen(prefix) &&
		strncmp(str, prefix, strlen(prefix)) == 0;
}

static inline int check_postfix(const char *str, const char *postfix)
{
	return strlen(str) > strlen(postfix) &&
		strcmp(str + strlen(str) - strlen(postfix), postfix) == 0;
}

static int dump_channels(const char *dev_dir_name)
{
	DIR *dp;
	const struct dirent *ent;

	dp = opendir(dev_dir_name);
	if (dp == NULL)
		return -errno;
	while (ent = readdir(dp), ent != NULL)
		if (check_prefix(ent->d_name, "in_") &&
		    check_postfix(ent->d_name, "_raw")) {
			printf("   %-10s\n", ent->d_name);
		}

	return (closedir(dp) == -1) ? -errno : 0;
}

static int dump_one_device(const char *dev_dir_name)
{
	char name[IIO_MAX_NAME_LENGTH];
	int dev_idx;
	int retval;

	retval = sscanf(dev_dir_name + strlen(iio_dir) + strlen(type_device),
			"%i", &dev_idx);
	if (retval != 1)
		return -EINVAL;
	retval = read_sysfs_string("name", dev_dir_name, name);
	if (retval)
		return retval;

	printf("Device %03d: %s\n", dev_idx, name);

	if (verblevel >= VERBLEVEL_SENSORS)
		return dump_channels(dev_dir_name);
	return 0;
}

static int dump_one_trigger(const char *dev_dir_name)
{
	char name[IIO_MAX_NAME_LENGTH];
	int dev_idx;
	int retval;

	retval = sscanf(dev_dir_name + strlen(iio_dir) + strlen(type_trigger),
			"%i", &dev_idx);
	if (retval != 1)
		return -EINVAL;
	retval = read_sysfs_string("name", dev_dir_name, name);
	if (retval)
		return retval;

	printf("Trigger %03d: %s\n", dev_idx, name);
	return 0;
}

static int dump_devices(void)
{
	const struct dirent *ent;
	int ret;
	DIR *dp;

	dp = opendir(iio_dir);
	if (dp == NULL) {
		printf("No industrial I/O devices available\n");
		return -ENODEV;
	}

	while (ent = readdir(dp), ent != NULL) {
		if (check_prefix(ent->d_name, type_device)) {
			char *dev_dir_name;

			if (asprintf(&dev_dir_name, "%s%s", iio_dir,
				     ent->d_name) < 0) {
				ret = -ENOMEM;
				goto error_close_dir;
			}

			ret = dump_one_device(dev_dir_name);
			if (ret) {
				free(dev_dir_name);
				goto error_close_dir;
			}

			free(dev_dir_name);
			if (verblevel >= VERBLEVEL_SENSORS)
				printf("\n");
		}
	}
	rewinddir(dp);
	while (ent = readdir(dp), ent != NULL) {
		if (check_prefix(ent->d_name, type_trigger)) {
			char *dev_dir_name;

			if (asprintf(&dev_dir_name, "%s%s", iio_dir,
				     ent->d_name) < 0) {
				ret = -ENOMEM;
				goto error_close_dir;
			}

			ret = dump_one_trigger(dev_dir_name);
			if (ret) {
				free(dev_dir_name);
				goto error_close_dir;
			}

			free(dev_dir_name);
		}
	}
	return (closedir(dp) == -1) ? -errno : 0;

error_close_dir:
	if (closedir(dp) == -1)
		perror("dump_devices(): Failed to close directory");

	return ret;
}

int main(int argc, char **argv)
{
	int c, err = 0;

	while ((c = getopt(argc, argv, "v")) != EOF) {
		switch (c) {
		case 'v':
			verblevel++;
			break;

		case '?':
		default:
			err++;
			break;
		}
	}
	if (err || argc > optind) {
		fprintf(stderr, "Usage: lsiio [options]...\n"
			"List industrial I/O devices\n"
			"  -v  Increase verbosity (may be given multiple times)\n");
		exit(1);
	}

	return dump_devices();
}
