/*
 * lsgpio - example on how to list the GPIO lines on a system
 *
 * Copyright (C) 2015 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Usage:
 *	lsgpio <-n device-name>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>

#include "gpio-utils.h"

int list_device(const char *device_name)
{
	struct gpiochip_info cinfo;
	char *chrdev_name;
	int fd;
	int ret;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	fd = open(chrdev_name, 0);
	if (fd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", chrdev_name);
		goto free_chrdev_name;
	}

	/* Inspect this GPIO chip */
	ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &cinfo);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to retrieve GPIO fd\n");
		if (close(fd) == -1)
			perror("Failed to close GPIO character device file");

		goto free_chrdev_name;
	}
	fprintf(stdout, "GPIO chip: %s, \"%s\", %u GPIO lines\n",
		cinfo.name, cinfo.label, cinfo.lines);

	if (close(fd) == -1)  {
		ret = -errno;
		goto free_chrdev_name;
	}

free_chrdev_name:
	free(chrdev_name);

	return ret;

}

void print_usage(void)
{
	fprintf(stderr, "Usage: lsgpio [options]...\n"
		"List GPIO chips, lines and states\n"
		"  -n <name>  List GPIOs on a named device\n"
		"  -?         This helptext\n"
	);
}

int main(int argc, char **argv)
{
	const char *device_name;
	int ret;
	int c;

	while ((c = getopt(argc, argv, "n:")) != -1) {
		switch (c) {
		case 'n':
			device_name = optarg;
			break;
		case '?':
			print_usage();
			return -1;
		}
	}

	if (device_name)
		ret = list_device(device_name);
	else {
		const struct dirent *ent;
		DIR *dp;

		/* List all GPIO devices one at a time */
		dp = opendir("/dev");
		if (!dp) {
			ret = -errno;
			goto error_out;
		}

		ret = -ENOENT;
		while (ent = readdir(dp), ent) {
			if (check_prefix(ent->d_name, "gpiochip")) {
				ret = list_device(ent->d_name);
				if (ret)
					break;
			}
		}

		ret = 0;
		if (closedir(dp) == -1) {
			perror("scanning devices: Failed to close directory");
			ret = -errno;
		}
	}
error_out:
	return ret;
}
