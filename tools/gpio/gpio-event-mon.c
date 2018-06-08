/*
 * gpio-event-mon - monitor GPIO line events from userspace
 *
 * Copyright (C) 2016 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
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

int monitor_device(const char *device_name,
		   unsigned int line,
		   uint32_t handleflags,
		   uint32_t eventflags,
		   unsigned int loops)
{
	struct gpioevent_request req;
	struct gpiohandle_data data;
	char *chrdev_name;
	int fd;
	int ret;
	int i = 0;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	fd = open(chrdev_name, 0);
	if (fd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s\n", chrdev_name);
		goto exit_close_error;
	}

	req.lineoffset = line;
	req.handleflags = handleflags;
	req.eventflags = eventflags;
	strcpy(req.consumer_label, "gpio-event-mon");

	ret = ioctl(fd, GPIO_GET_LINEEVENT_IOCTL, &req);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue GET EVENT "
			"IOCTL (%d)\n",
			ret);
		goto exit_close_error;
	}

	/* Read initial states */
	ret = ioctl(req.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue GPIOHANDLE GET LINE "
			"VALUES IOCTL (%d)\n",
			ret);
		goto exit_close_error;
	}

	fprintf(stdout, "Monitoring line %d on %s\n", line, device_name);
	fprintf(stdout, "Initial line value: %d\n", data.values[0]);

	while (1) {
		struct gpioevent_data event;

		ret = read(req.fd, &event, sizeof(event));
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
		fprintf(stdout, "GPIO EVENT %llu: ", event.timestamp);
		switch (event.id) {
		case GPIOEVENT_EVENT_RISING_EDGE:
			fprintf(stdout, "rising edge");
			break;
		case GPIOEVENT_EVENT_FALLING_EDGE:
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

exit_close_error:
	if (close(fd) == -1)
		perror("Failed to close GPIO character device file");
	free(chrdev_name);
	return ret;
}

void print_usage(void)
{
	fprintf(stderr, "Usage: gpio-event-mon [options]...\n"
		"Listen to events on GPIO lines, 0->1 1->0\n"
		"  -n <name>  Listen on GPIOs on a named device (must be stated)\n"
		"  -o <n>     Offset to monitor\n"
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

int main(int argc, char **argv)
{
	const char *device_name = NULL;
	unsigned int line = -1;
	unsigned int loops = 0;
	uint32_t handleflags = GPIOHANDLE_REQUEST_INPUT;
	uint32_t eventflags = 0;
	int c;

	while ((c = getopt(argc, argv, "c:n:o:dsrf?")) != -1) {
		switch (c) {
		case 'c':
			loops = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			device_name = optarg;
			break;
		case 'o':
			line = strtoul(optarg, NULL, 10);
			break;
		case 'd':
			handleflags |= GPIOHANDLE_REQUEST_OPEN_DRAIN;
			break;
		case 's':
			handleflags |= GPIOHANDLE_REQUEST_OPEN_SOURCE;
			break;
		case 'r':
			eventflags |= GPIOEVENT_REQUEST_RISING_EDGE;
			break;
		case 'f':
			eventflags |= GPIOEVENT_REQUEST_FALLING_EDGE;
			break;
		case '?':
			print_usage();
			return -1;
		}
	}

	if (!device_name || line == -1) {
		print_usage();
		return -1;
	}
	if (!eventflags) {
		printf("No flags specified, listening on both rising and "
		       "falling edges\n");
		eventflags = GPIOEVENT_REQUEST_BOTH_EDGES;
	}
	return monitor_device(device_name, line, handleflags,
			      eventflags, loops);
}
