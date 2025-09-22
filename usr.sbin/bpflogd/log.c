/*	$OpenBSD: log.c,v 1.2 2025/05/15 12:49:05 kn Exp $	*/

/*
 * Copyright (c) 2008 David Gwynne <loki@animata.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>
#include <time.h>
#include <syslog.h>
#include <stdarg.h>

#include "log.h"

static const struct __logger conslogger = {
	err,
	errx,
	warn,
	warnx,
	warnx, /* info */
	warnx /* debug */
};

__dead static void	syslog_err(int, const char *, ...)
			    __attribute__((__format__ (printf, 2, 3)));
__dead static void	syslog_errx(int, const char *, ...)
			    __attribute__((__format__ (printf, 2, 3)));
static void		syslog_warn(const char *, ...)
			    __attribute__((__format__ (printf, 1, 2)));
static void		syslog_warnx(const char *, ...)
			    __attribute__((__format__ (printf, 1, 2)));
static void		syslog_info(const char *, ...)
			    __attribute__((__format__ (printf, 1, 2)));
static void		syslog_debug(const char *, ...)
			    __attribute__((__format__ (printf, 1, 2)));
static void		syslog_vstrerror(int, int, const char *, va_list)
			    __attribute__((__format__ (printf, 3, 0)));

static const struct __logger syslogger = {
	syslog_err,
	syslog_errx,
	syslog_warn,
	syslog_warnx,
	syslog_info,
	syslog_debug
};

const struct __logger *__logger = &conslogger;

void
logger_syslog(const char *progname)
{
	openlog(progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);
	tzset();

	__logger = &syslogger;
}

static void
syslog_vstrerror(int e, int priority, const char *fmt, va_list ap)
{
	char *s;

	if (vasprintf(&s, fmt, ap) == -1) {
		syslog(LOG_EMERG, "unable to alloc in syslog_vstrerror");
		exit(1);
	}

	syslog(priority, "%s: %s", s, strerror(e));

	free(s);
}

static void
syslog_err(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_CRIT, fmt, ap);
	va_end(ap);

	exit(ecode);
}

static void
syslog_errx(int ecode, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_CRIT, fmt, ap);
	va_end(ap);

	exit(ecode);
}

static void
syslog_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	syslog_vstrerror(errno, LOG_ERR, fmt, ap);
	va_end(ap);
}

static void
syslog_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_ERR, fmt, ap);
	va_end(ap);
}

static void
syslog_info(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_INFO, fmt, ap);
	va_end(ap);
}

static void
syslog_debug(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsyslog(LOG_DEBUG, fmt, ap);
	va_end(ap);
}
