// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GPIO character device helper for reading line names.
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
	printf("  gpio-line-name <chip path> <line offset>\n");
}

int main(int argc, char **argv)
{
	struct gpio_v2_line_info info;
	int fd, ret;
	char *endp;

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
	info.offset = strtoul(argv[2], &endp, 10);
	if (*endp != '\0') {
		print_usage();
		return EXIT_FAILURE;
	}

	ret = ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &info);
	if (ret) {
		perror("line info ioctl failed");
		return EXIT_FAILURE;
	}

	printf("%s\n", info.name);

	return EXIT_SUCCESS;
}
