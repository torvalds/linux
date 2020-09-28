// SPDX-License-Identifier: GPL-2.0-only
/*
 * gpio-event-mon - monitor GPIO line events from userspace
 *
 * Copyright (C) 2016 Linus Walleij
 *
 * Usage:
 *	gpio-event-mon -n <device-name> -o <offset>
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <poll.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <linux/gpio.h>
#include "gpio-utils.h"

int monitor_device(const char *device_name,
		   unsigned int *lines,
		   unsigned int num_lines,
		   struct gpio_v2_line_config *config,
		   unsigned int loops)
{
	struct gpio_v2_line_values values;
	char *chrdev_name;
	int cfd, lfd;
	int ret;
	int i = 0;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	cfd = open(chrdev_name, 0);
	if (cfd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", chrdev_name);
		goto exit_free_name;
	}

	ret = gpiotools_request_line(device_name, lines, num_lines, config,
				     "gpio-event-mon");
	if (ret < 0)
		goto exit_device_close;
	else
		lfd = ret;

	/* Read initial states */
	values.mask = 0;
	values.bits = 0;
	for (i = 0; i < num_lines; i++)
		gpiotools_set_bit(&values.mask, i);
	ret = gpiotools_get_values(lfd, &values);
	if (ret < 0) {
		fprintf(stderr,
			"Failed to issue GPIO LINE GET VALUES IOCTL (%d)\n",
			ret);
		goto exit_line_close;
	}

	if (num_lines == 1) {
		fprintf(stdout, "Monitoring line %d on %s\n", lines[0], device_name);
		fprintf(stdout, "Initial line value: %d\n",
			gpiotools_test_bit(values.bits, 0));
	} else {
		fprintf(stdout, "Monitoring lines %d", lines[0]);
		for (i = 1; i < num_lines - 1; i++)
			fprintf(stdout, ", %d", lines[i]);
		fprintf(stdout, " and %d on %s\n", lines[i], device_name);
		fprintf(stdout, "Initial line values: %d",
			gpiotools_test_bit(values.bits, 0));
		for (i = 1; i < num_lines - 1; i++)
			fprintf(stdout, ", %d",
				gpiotools_test_bit(values.bits, i));
		fprintf(stdout, " and %d\n",
			gpiotools_test_bit(values.bits, i));
	}

	while (1) {
		struct gpio_v2_line_event event;

		ret = read(lfd, &event, sizeof(event));
		if (ret == -1) {
			if (errno == -EAGAIN) {
				fprintf(stderr, "nothing available\n");
				continue;
			} else {
				ret = -errno;
				fprintf(stderr, "Failed to read event (%d)\n",
					ret);
				break;
			}
		}

		if (ret != sizeof(event)) {
			fprintf(stderr, "Reading event failed\n");
			ret = -EIO;
			break;
		}
		fprintf(stdout, "GPIO EVENT at %llu on line %d (%d|%d) ",
			event.timestamp_ns, event.offset, event.line_seqno,
			event.seqno);
		switch (event.id) {
		case GPIO_V2_LINE_EVENT_RISING_EDGE:
			fprintf(stdout, "rising edge");
			break;
		case GPIO_V2_LINE_EVENT_FALLING_EDGE:
			fprintf(stdout, "falling edge");
			break;
		default:
			fprintf(stdout, "unknown event");
		}
		fprintf(stdout, "\n");

		i++;
		if (i == loops)
			break;
	}

exit_line_close:
	if (close(lfd) == -1)
		perror("Failed to close line file");
exit_device_close:
	if (close(cfd) == -1)
		perror("Failed to close GPIO character device file");
exit_free_name:
	free(chrdev_name);
	return ret;
}

void print_usage(void)
{
	fprintf(stderr, "Usage: gpio-event-mon [options]...\n"
		"Listen to events on GPIO lines, 0->1 1->0\n"
		"  -n <name>  Listen on GPIOs on a named device (must be stated)\n"
		"  -o <n>     Offset of line to monitor (may be repeated)\n"
		"  -d         Set line as open drain\n"
		"  -s         Set line as open source\n"
		"  -r         Listen for rising edges\n"
		"  -f         Listen for falling edges\n"
		" [-c <n>]    Do <n> loops (optional, infinite loop if not stated)\n"
		"  -?         This helptext\n"
		"\n"
		"Example:\n"
		"gpio-event-mon -n gpiochip0 -o 4 -r -f\n"
	);
}

#define EDGE_FLAGS \
	(GPIO_V2_LINE_FLAG_EDGE_RISING | \
	 GPIO_V2_LINE_FLAG_EDGE_FALLING)

int main(int argc, char **argv)
{
	const char *device_name = NULL;
	unsigned int lines[GPIO_V2_LINES_MAX];
	unsigned int num_lines = 0;
	unsigned int loops = 0;
	struct gpio_v2_line_config config;
	int c;

	memset(&config, 0, sizeof(config));
	config.flags = GPIO_V2_LINE_FLAG_INPUT;
	while ((c = getopt(argc, argv, "c:n:o:dsrf?")) != -1) {
		switch (c) {
		case 'c':
			loops = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			device_name = optarg;
			break;
		case 'o':
			if (num_lines >= GPIO_V2_LINES_MAX) {
				print_usage();
				return -1;
			}
			lines[num_lines] = strtoul(optarg, NULL, 10);
			num_lines++;
			break;
		case 'd':
			config.flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
			break;
		case 's':
			config.flags |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;
			break;
		case 'r':
			config.flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
			break;
		case 'f':
			config.flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
			break;
		case '?':
			print_usage();
			return -1;
		}
	}

	if (!device_name || num_lines == 0) {
		print_usage();
		return -1;
	}
	if (!(config.flags & EDGE_FLAGS)) {
		printf("No flags specified, listening on both rising and "
		       "falling edges\n");
		config.flags |= EDGE_FLAGS;
	}
	return monitor_device(device_name, lines, num_lines, &config, loops);
}
