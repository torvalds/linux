/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GPIO tools - utility helpers library for the GPIO tools
 *
 * Copyright (C) 2015 Linus Walleij
 *
 * Portions copied from iio_utils and lssio:
 * Copyright (c) 2010 Manuel Stahl <manuel.stahl@iis.fraunhofer.de>
 * Copyright (c) 2008 Jonathan Cameron
 * *
 */
#ifndef _GPIO_UTILS_H_
#define _GPIO_UTILS_H_

#include <string.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline int check_prefix(const char *str, const char *prefix)
{
	return strlen(str) > strlen(prefix) &&
		strncmp(str, prefix, strlen(prefix)) == 0;
}

int gpiotools_request_linehandle(const char *device_name, unsigned int *lines,
				 unsigned int num_lines, unsigned int flag,
				 struct gpiohandle_data *data,
				 const char *consumer_label);
int gpiotools_set_values(const int fd, struct gpiohandle_data *data);
int gpiotools_get_values(const int fd, struct gpiohandle_data *data);
int gpiotools_release_linehandle(const int fd);

int gpiotools_get(const char *device_name, unsigned int line);
int gpiotools_gets(const char *device_name, unsigned int *lines,
		   unsigned int num_lines, struct gpiohandle_data *data);
int gpiotools_set(const char *device_name, unsigned int line,
		  unsigned int value);
int gpiotools_sets(const char *device_name, unsigned int *lines,
		   unsigned int num_lines, struct gpiohandle_data *data);

#endif /* _GPIO_UTILS_H_ */
