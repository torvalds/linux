// SPDX-License-Identifier: LGPL-2.1+
// Copyright (C) 2022, Linaro Ltd - Daniel Lezcano <daniel.lezcano@linaro.org>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include "log.h"

static const char *__ident = "unknown";
static int __options;

static const char * const loglvl[] = {
	[LOG_DEBUG]	= "DEBUG",
	[LOG_INFO]	= "INFO",
	[LOG_NOTICE]	= "NOTICE",
	[LOG_WARNING]	= "WARN",
	[LOG_ERR]	= "ERROR",
	[LOG_CRIT]	= "CRITICAL",
	[LOG_ALERT]	= "ALERT",
	[LOG_EMERG]	= "EMERG",
};

int log_str2level(const char *lvl)
{
	int i;

	for (i = 0; i < sizeof(loglvl) / sizeof(loglvl[LOG_DEBUG]); i++)
		if (!strcmp(lvl, loglvl[i]))
			return i;

	return LOG_DEBUG;
}

extern void logit(int level, const char *format, ...)
{
	va_list args;

	va_start(args, format);

	if (__options & TO_SYSLOG)
		vsyslog(level, format, args);

	if (__options & TO_STDERR)
		vfprintf(stderr, format, args);

	if (__options & TO_STDOUT)
		vfprintf(stdout, format, args);

	va_end(args);
}

int log_init(int level, const char *ident, int options)
{
	if (!options)
		return -1;

	if (level > LOG_DEBUG)
		return -1;

	if (!ident)
		return -1;

	__ident = ident;
	__options = options;

	if (options & TO_SYSLOG) {
		openlog(__ident, options | LOG_NDELAY, LOG_USER);
		setlogmask(LOG_UPTO(level));
	}

	return 0;
}

void log_exit(void)
{
	closelog();
}
