// SPDX-License-Identifier: GPL-2.0-only
/*
 * gpio-hammer - example swiss army knife to shake GPIO lines on a system
 *
 * Copyright (C) 2016 Linus Walleij
 *
 * Usage:
 *	gpio-hammer -n <device-name> -o <offset1> -o <offset2>
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

int hammer_device(const char *device_name, unsigned int *lines, int num_lines,
		  unsigned int loops)
{
	struct gpiohandle_data data;
	char swirr[] = "-\\|/";
	int fd;
	int ret;
	int i, j;
	unsigned int iteration = 0;

	memset(&data.values, 0, sizeof(data.values));
	ret = gpiotools_request_linehandle(device_name, lines, num_lines,
					   GPIOHANDLE_REQUEST_OUTPUT, &data,
					   "gpio-hammer");
	if (ret < 0)
		goto exit_error;
	else
		fd = ret;

	ret = gpiotools_get_values(fd, &data);
	if (ret < 0)
		goto exit_close_error;

	fprintf(stdout, "Hammer lines [");
	for (i = 0; i < num_lines; i++) {
		fprintf(stdout, "%d", lines[i]);
		if (i != (num_lines - 1))
			fprintf(stdout, ", ");
	}
	fprintf(stdout, "] on %s, initial states: [", device_name);
	for (i = 0; i < num_lines; i++) {
		fprintf(stdout, "%d", data.values[i]);
		if (i != (num_lines - 1))
			fprintf(stdout, ", ");
	}
	fprintf(stdout, "]\n");

	/* Hammertime! */
	j = 0;
	while (1) {
		/* Invert all lines so we blink */
		for (i = 0; i < num_lines; i++)
			data.values[i] = !data.values[i];

		ret = gpiotools_set_values(fd, &data);
		if (ret < 0)
			goto exit_close_error;

		/* Re-read values to get status */
		ret = gpiotools_get_values(fd, &data);
		if (ret < 0)
			goto exit_close_error;

		fprintf(stdout, "[%c] ", swirr[j]);
		j++;
		if (j == sizeof(swirr) - 1)
			j = 0;

		fprintf(stdout, "[");
		for (i = 0; i < num_lines; i++) {
			fprintf(stdout, "%d: %d", lines[i], data.values[i]);
			if (i != (num_lines - 1))
				fprintf(stdout, ", ");
		}
		fprintf(stdout, "]\r");
		fflush(stdout);
		sleep(1);
		iteration++;
		if (loops && iteration == loops)
			break;
	}
	fprintf(stdout, "\n");
	ret = 0;

exit_close_error:
	gpiotools_release_linehandle(fd);
exit_error:
	return ret;
}

void print_usage(void)
{
	fprintf(stderr, "Usage: gpio-hammer [options]...\n"
		"Hammer GPIO lines, 0->1->0->1...\n"
		"  -n <name>  Hammer GPIOs on a named device (must be stated)\n"
		"  -o <n>     Offset[s] to hammer, at least one, several can be stated\n"
		" [-c <n>]    Do <n> loops (optional, infinite loop if not stated)\n"
		"  -?         This helptext\n"
		"\n"
		"Example:\n"
		"gpio-hammer -n gpiochip0 -o 4\n"
	);
}

int main(int argc, char **argv)
{
	const char *device_name = NULL;
	unsigned int lines[GPIOHANDLES_MAX];
	unsigned int loops = 0;
	int num_lines;
	int c;
	int i;

	i = 0;
	while ((c = getopt(argc, argv, "c:n:o:?")) != -1) {
		switch (c) {
		case 'c':
			loops = strtoul(optarg, NULL, 10);
			break;
		case 'n':
			device_name = optarg;
			break;
		case 'o':
			/*
			 * Avoid overflow. Do not immediately error, we want to
			 * be able to accurately report on the amount of times
			 * '-o' was given to give an accurate error message
			 */
			if (i < GPIOHANDLES_MAX)
				lines[i] = strtoul(optarg, NULL, 10);

			i++;
			break;
		case '?':
			print_usage();
			return -1;
		}
	}

	if (i >= GPIOHANDLES_MAX) {
		fprintf(stderr,
			"Only %d occurrences of '-o' are allowed, %d were found\n",
			GPIOHANDLES_MAX, i + 1);
		return -1;
	}

	num_lines = i;

	if (!device_name || !num_lines) {
		print_usage();
		return -1;
	}
	return hammer_device(device_name, lines, num_lines, loops);
}
