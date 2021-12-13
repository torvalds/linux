// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO character device helper for reading chip information.
 *
 * Copyright (C) 2021 Bartosz Golaszewski <brgl@bgdev.pl>
 */

#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>

static void print_usage(void)
{
	printf("usage:\n");
	printf("  gpio-chip-info <chip path> [name|label|num-lines]\n");
}

int main(int argc, char **argv)
{
	struct gpiochip_info info;
	int fd, ret;

	if (argc != 3) {
		print_usage();
		return EXIT_FAILURE;
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		perror("unable to open the GPIO chip");
		return EXIT_FAILURE;
	}

	memset(&info, 0, sizeof(info));
	ret = ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info);
	if (ret) {
		perror("chip info ioctl failed");
		return EXIT_FAILURE;
	}

	if (strcmp(argv[2], "name") == 0) {
		printf("%s\n", info.name);
	} else if (strcmp(argv[2], "label") == 0) {
		printf("%s\n", info.label);
	} else if (strcmp(argv[2], "num-lines") == 0) {
		printf("%u\n", info.lines);
	} else {
		fprintf(stderr, "unknown command: %s\n", argv[2]);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
