/*	$OpenBSD: log.c,v 1.12 2015/12/11 18:49:39 nicm Exp $	*/

/*
 * log.c
 *
 * Based on err.c, which was adapted from OpenBSD libc *err* *warn* code.
 *
 * Copyright (c) 2005 Nick Mathewson <nickm@freehaven.net>
 *
 * Copyright (c) 2000 Dug Song <dugsong@monkey.org>
 *
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>

#include "event.h"
#include "log.h"

static void	_warn_helper(int severity, int log_errno, const char *fmt,
		    va_list ap);
static void	event_log(int severity, const char *msg);

void
event_err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_warn_helper(_EVENT_LOG_ERR, errno, fmt, ap);
	va_end(ap);
	exit(eval);
}

void
event_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_warn_helper(_EVENT_LOG_WARN, errno, fmt, ap);
	va_end(ap);
}

void
event_errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_warn_helper(_EVENT_LOG_ERR, -1, fmt, ap);
	va_end(ap);
	exit(eval);
}

void
event_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_warn_helper(_EVENT_LOG_WARN, -1, fmt, ap);
	va_end(ap);
}

void
event_msgx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_warn_helper(_EVENT_LOG_MSG, -1, fmt, ap);
	va_end(ap);
}

void
_event_debugx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	_warn_helper(_EVENT_LOG_DEBUG, -1, fmt, ap);
	va_end(ap);
}

static void
_warn_helper(int severity, int log_errno, const char *fmt, va_list ap)
{
	char buf[1024];
	size_t len;

	if (fmt != NULL)
		vsnprintf(buf, sizeof(buf), fmt, ap);
	else
		buf[0] = '\0';

	if (log_errno >= 0) {
		len = strlen(buf);
		if (len < sizeof(buf) - 3) {
			snprintf(buf + len, sizeof(buf) - len, ": %s",
			    strerror(log_errno));
		}
	}

	event_log(severity, buf);
}

static event_log_cb log_fn = NULL;

void
event_set_log_callback(event_log_cb cb)
{
	log_fn = cb;
}

static void
event_log(int severity, const char *msg)
{
	if (log_fn)
		log_fn(severity, msg);
}
