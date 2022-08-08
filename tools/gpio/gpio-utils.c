// SPDX-License-Identifier: GPL-2.0-only
/*
 * GPIO tools - helpers library for the GPIO tools
 *
 * Copyright (C) 2015 Linus Walleij
 * Copyright (C) 2016 Bamvor Jian Zhang
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <linux/gpio.h>
#include "gpio-utils.h"

#define CONSUMER "gpio-utils"

/**
 * doc: Operation of gpio
 *
 * Provide the api of gpiochip for chardev interface. There are two
 * types of api.  The first one provide as same function as each
 * ioctl, including request and release for lines of gpio, read/write
 * the value of gpio. If the user want to do lots of read and write of
 * lines of gpio, user should use this type of api.
 *
 * The second one provide the easy to use api for user. Each of the
 * following api will request gpio lines, do the operation and then
 * release these lines.
 */

/**
 * gpiotools_request_line() - request gpio lines in a gpiochip
 * @device_name:	The name of gpiochip without prefix "/dev/",
 *			such as "gpiochip0"
 * @lines:		An array desired lines, specified by offset
 *			index for the associated GPIO device.
 * @num_lines:		The number of lines to request.
 * @config:		The new config for requested gpio. Reference
 *			"linux/gpio.h" for config details.
 * @consumer:		The name of consumer, such as "sysfs",
 *			"powerkey". This is useful for other users to
 *			know who is using.
 *
 * Request gpio lines through the ioctl provided by chardev. User
 * could call gpiotools_set_values() and gpiotools_get_values() to
 * read and write respectively through the returned fd. Call
 * gpiotools_release_line() to release these lines after that.
 *
 * Return:		On success return the fd;
 *			On failure return the errno.
 */
int gpiotools_request_line(const char *device_name, unsigned int *lines,
			   unsigned int num_lines,
			   struct gpio_v2_line_config *config,
			   const char *consumer)
{
	struct gpio_v2_line_request req;
	char *chrdev_name;
	int fd;
	int i;
	int ret;

	ret = asprintf(&chrdev_name, "/dev/%s", device_name);
	if (ret < 0)
		return -ENOMEM;

	fd = open(chrdev_name, 0);
	if (fd == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to open %s, %s\n",
			chrdev_name, strerror(errno));
		goto exit_free_name;
	}

	memset(&req, 0, sizeof(req));
	for (i = 0; i < num_lines; i++)
		req.offsets[i] = lines[i];

	req.config = *config;
	strcpy(req.consumer, consumer);
	req.num_lines = num_lines;

	ret = ioctl(fd, GPIO_V2_GET_LINE_IOCTL, &req);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue %s (%d), %s\n",
			"GPIO_GET_LINE_IOCTL", ret, strerror(errno));
	}

	if (close(fd) == -1)
		perror("Failed to close GPIO character device file");
exit_free_name:
	free(chrdev_name);
	return ret < 0 ? ret : req.fd;
}

/**
 * gpiotools_set_values(): Set the value of gpio(s)
 * @fd:			The fd returned by
 *			gpiotools_request_line().
 * @values:		The array of values want to set.
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_set_values(const int fd, struct gpio_v2_line_values *values)
{
	int ret;

	ret = ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, values);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue %s (%d), %s\n",
			"GPIOHANDLE_SET_LINE_VALUES_IOCTL", ret,
			strerror(errno));
	}

	return ret;
}

/**
 * gpiotools_get_values(): Get the value of gpio(s)
 * @fd:			The fd returned by
 *			gpiotools_request_line().
 * @values:		The array of values get from hardware.
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_get_values(const int fd, struct gpio_v2_line_values *values)
{
	int ret;

	ret = ioctl(fd, GPIO_V2_LINE_GET_VALUES_IOCTL, values);
	if (ret == -1) {
		ret = -errno;
		fprintf(stderr, "Failed to issue %s (%d), %s\n",
			"GPIOHANDLE_GET_LINE_VALUES_IOCTL", ret,
			strerror(errno));
	}

	return ret;
}

/**
 * gpiotools_release_line(): Release the line(s) of gpiochip
 * @fd:			The fd returned by
 *			gpiotools_request_line().
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_release_line(const int fd)
{
	int ret;

	ret = close(fd);
	if (ret == -1) {
		perror("Failed to close GPIO LINE device file");
		ret = -errno;
	}

	return ret;
}

/**
 * gpiotools_get(): Get value from specific line
 * @device_name:	The name of gpiochip without prefix "/dev/",
 *			such as "gpiochip0"
 * @line:		number of line, such as 2.
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_get(const char *device_name, unsigned int line)
{
	int ret;
	unsigned int value;
	unsigned int lines[] = {line};

	ret = gpiotools_gets(device_name, lines, 1, &value);
	if (ret)
		return ret;
	return value;
}


/**
 * gpiotools_gets(): Get values from specific lines.
 * @device_name:	The name of gpiochip without prefix "/dev/",
 *			such as "gpiochip0".
 * @lines:		An array desired lines, specified by offset
 *			index for the associated GPIO device.
 * @num_lines:		The number of lines to request.
 * @values:		The array of values get from gpiochip.
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_gets(const char *device_name, unsigned int *lines,
		   unsigned int num_lines, unsigned int *values)
{
	int fd, i;
	int ret;
	int ret_close;
	struct gpio_v2_line_config config;
	struct gpio_v2_line_values lv;

	memset(&config, 0, sizeof(config));
	config.flags = GPIO_V2_LINE_FLAG_INPUT;
	ret = gpiotools_request_line(device_name, lines, num_lines,
				     &config, CONSUMER);
	if (ret < 0)
		return ret;

	fd = ret;
	for (i = 0; i < num_lines; i++)
		gpiotools_set_bit(&lv.mask, i);
	ret = gpiotools_get_values(fd, &lv);
	if (!ret)
		for (i = 0; i < num_lines; i++)
			values[i] = gpiotools_test_bit(lv.bits, i);
	ret_close = gpiotools_release_line(fd);
	return ret < 0 ? ret : ret_close;
}

/**
 * gpiotools_set(): Set value to specific line
 * @device_name:	The name of gpiochip without prefix "/dev/",
 *			such as "gpiochip0"
 * @line:		number of line, such as 2.
 * @value:		The value of gpio, must be 0(low) or 1(high).
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_set(const char *device_name, unsigned int line,
		  unsigned int value)
{
	unsigned int lines[] = {line};

	return gpiotools_sets(device_name, lines, 1, &value);
}

/**
 * gpiotools_sets(): Set values to specific lines.
 * @device_name:	The name of gpiochip without prefix "/dev/",
 *			such as "gpiochip0".
 * @lines:		An array desired lines, specified by offset
 *			index for the associated GPIO device.
 * @num_lines:		The number of lines to request.
 * @value:		The array of values set to gpiochip, must be
 *			0(low) or 1(high).
 *
 * Return:		On success return 0;
 *			On failure return the errno.
 */
int gpiotools_sets(const char *device_name, unsigned int *lines,
		   unsigned int num_lines, unsigned int *values)
{
	int ret, i;
	struct gpio_v2_line_config config;

	memset(&config, 0, sizeof(config));
	config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
	config.num_attrs = 1;
	config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
	for (i = 0; i < num_lines; i++) {
		gpiotools_set_bit(&config.attrs[0].mask, i);
		gpiotools_assign_bit(&config.attrs[0].attr.values,
				     i, values[i]);
	}
	ret = gpiotools_request_line(device_name, lines, num_lines,
				     &config, CONSUMER);
	if (ret < 0)
		return ret;

	return gpiotools_release_line(ret);
}
