/*	$OpenBSD: log.c,v 1.4 2017/07/28 13:05:21 florian Exp $	*/

/*
 * Copyright (c) 2003, 2004 Henning Brauer <henning@openbsd.org>
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

#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "log.h"

static int debug;

static char	*logqueue;

#define LOGQUEUE_SIZE 1024

void	logit(int, const char *, ...);
void	vlog(int pri, const char *fmt, va_list ap);

void
log_init(int n_debug)
{
	extern char	*__progname;

	if (logqueue == NULL) {
		logqueue = calloc(LOGQUEUE_SIZE, 1);
		if (logqueue == NULL)
			err(1, NULL);
	}

	debug = n_debug;

	if (!debug)
		openlog(__progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);

	tzset();
}

void
logit(int pri, const char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vlog(pri, fmt, ap);
	va_end(ap);
}

void
log_enqueue(const char *fmt, ...)
{
	char tmpbuf[LOGQUEUE_SIZE];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(tmpbuf, sizeof(tmpbuf), fmt, ap);
	va_end(ap);
	(void)strlcat(logqueue, tmpbuf, LOGQUEUE_SIZE);
}


void
vlog(int pri, const char *fmt, va_list ap)
{
	char	tmpbuf[1024];
	char	logbuf[LOGQUEUE_SIZE + sizeof(tmpbuf)];

	if (pri == LOG_DEBUG && !debug) {
		logqueue[0] = '\0';
		return;
	}

	(void)vsnprintf(tmpbuf, sizeof(tmpbuf), fmt, ap);
	(void)strlcpy(logbuf, logqueue, sizeof(logbuf));
	(void)strlcat(logbuf, tmpbuf, sizeof(logbuf));

	logqueue[0] = '\0';

	if (debug) {
		fprintf(stderr, "%s\n", logbuf);
		fflush(stderr);
	} else
		syslog(pri, "%s", logbuf);
}

void
log_warn(const char *emsg, ...)
{
	char	*nfmt;
	va_list	 ap;

	if (emsg == NULL)
		logit(LOG_ERR, "%s", strerror(errno));
	else {
		va_start(ap, emsg);

		/* best effort to even work in out of memory situations */
		if (asprintf(&nfmt, "%s: %s", emsg, strerror(errno)) == -1) {
			/* we tried it... */
			vlog(LOG_ERR, emsg, ap);
			logit(LOG_ERR, "%s", strerror(errno));
		} else {
			vlog(LOG_ERR, nfmt, ap);
			free(nfmt);
		}
		va_end(ap);
	}
}

void
log_warnx(const char *emsg, ...)
{
	va_list	 ap;

	va_start(ap, emsg);
	vlog(LOG_ERR, emsg, ap);
	va_end(ap);
}

void
log_info(const char *emsg, ...)
{
	va_list	 ap;

	va_start(ap, emsg);
	vlog(LOG_INFO, emsg, ap);
	va_end(ap);
}

void
log_debug(const char *emsg, ...)
{
	va_list	 ap;

	va_start(ap, emsg);
	vlog(LOG_DEBUG, emsg, ap);
	va_end(ap);
}

void
fatal(const char *emsg)
{
	extern char	*__progname;
	extern __dead void rtdexit(void);

	if (emsg == NULL)
		logit(LOG_CRIT, "fatal in %s: %s", __progname,
		    strerror(errno));
	else
		if (errno)
			logit(LOG_CRIT, "fatal in %s: %s: %s",
			    __progname, emsg, strerror(errno));
		else
			logit(LOG_CRIT, "fatal in %s: %s",
			    __progname, emsg);

	rtdexit();
}

void
fatalx(const char *emsg)
{
	errno = 0;
	fatal(emsg);
}
