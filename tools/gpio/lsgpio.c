// SPDX-License-Identifier: GPL-2.0-only
/*
 * lsgpio - example on how to list the GPIO lines on a system
 *
 * Copyright (C) 2015 Linus Walleij
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

struct gpio_flag {
	char *name;
	unsigned long mask;
};

struct gpio_flag flagnames[] = {
	{
		.name = "kernel",
		.mask = GPIOLINE_FLAG_KERNEL,
	},
	{
		.name = "output",
		.mask = GPIOLINE_FLAG_IS_OUT,
	},
	{
		.name = "active-low",
		.mask = GPIOLINE_FLAG_ACTIVE_LOW,
	},
	{
		.name = "open-drain",
		.mask = GPIOLINE_FLAG_OPEN_DRAIN,
	},
	{
		.name = "open-source",
		.mask = GPIOLINE_FLAG_OPEN_SOURCE,
	},
	{
		.name = "pull-up",
		.mask = GPIOLINE_FLAG_BIAS_PULL_UP,
	},
	{
		.name = "pull-down",
		.mask = GPIOLINE_FLAG_BIAS_PULL_DOWN,
	},
	{
		.name = "bias-disabled",
		.mask = GPIOLINE_FLAG_BIAS_DISABLE,
	},
};

void print_flags(unsigned long flags)
{
	int i;
	int printed = 0;

	for (i = 0; i < ARRAY_SIZE(flagnames); i++) {
		if (flags & flagnames[i].mask) {
			if (printed)
				fprintf(stdout, " ");
			fprintf(stdout, "%s", flagnames[i].name);
			printed++;
		}
	}
}

int list_device(const char *device_name)
{
	struct gpiochip_info cinfo;
	char *chrdev_name;
	int fd;
	int ret;
	int i;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	fd = open(chrdev_name, 0);
	if (fd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", chrdev_name);
		goto exit_close_error;
	}

	/* Inspect this GPIO chip */
	ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &cinfo);
	if (ret == -1) {
		ret = -errno;
		perror("Failed to issue CHIPINFO IOCTL\n");
		goto exit_close_error;
	}
	fprintf(stdout, "GPIO chip: %s, \"%s\", %u GPIO lines\n",
		cinfo.name, cinfo.label, cinfo.lines);

	/* Loop over the lines and print info */
	for (i = 0; i < cinfo.lines; i++) {
		struct gpioline_info linfo;

		memset(&linfo, 0, sizeof(linfo));
		linfo.line_offset = i;

		ret = ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &linfo);
		if (ret == -1) {
			ret = -errno;
			perror("Failed to issue LINEINFO IOCTL\n");
			goto exit_close_error;
		}
		fprintf(stdout, "\tline %2d:", linfo.line_offset);
		if (linfo.name[0])
			fprintf(stdout, " \"%s\"", linfo.name);
		else
			fprintf(stdout, " unnamed");
		if (linfo.consumer[0])
			fprintf(stdout, " \"%s\"", linfo.consumer);
		else
			fprintf(stdout, " unused");
		if (linfo.flags) {
			fprintf(stdout, " [");
			print_flags(linfo.flags);
			fprintf(stdout, "]");
		}
		fprintf(stdout, "\n");

	}

exit_close_error:
	if (close(fd) == -1)
		perror("Failed to close GPIO character device file");
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
	const char *device_name = NULL;
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
