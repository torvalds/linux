// SPDX-License-Identifier: GPL-2.0
/*
 * util functions.
 *
 * Copyright (C) 2022 Red Hat Inc, Daniel Bristot de Oliveira <bristot@kernel.org>
 */

#include <stdarg.h>
#include <stdio.h>
#include <utils.h>

int config_debug;

#define MAX_MSG_LENGTH	1024

/**
 * err_msg - print an error message to the stderr
 */
void err_msg(const char *fmt, ...)
{
	char message[MAX_MSG_LENGTH];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s", message);
}

/**
 * debug_msg - print a debug message to stderr if debug is set
 */
void debug_msg(const char *fmt, ...)
{
	char message[MAX_MSG_LENGTH];
	va_list ap;

	if (!config_debug)
		return;

	va_start(ap, fmt);
	vsnprintf(message, sizeof(message), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s", message);
}
