/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <vis.h>

#include "ctld.h"

static int log_level = 0;
static char *peer_name = NULL;
static char *peer_addr = NULL;

#define	MSGBUF_LEN	1024

void
log_init(int level)
{

	log_level = level;
	openlog(getprogname(), LOG_NDELAY | LOG_PID, LOG_DAEMON);
}

void
log_set_peer_name(const char *name)
{

	/*
	 * XXX: Turn it into assertion?
	 */
	if (peer_name != NULL)
		log_errx(1, "%s called twice", __func__);
	if (peer_addr == NULL)
		log_errx(1, "%s called before log_set_peer_addr", __func__);

	peer_name = checked_strdup(name);
}

void
log_set_peer_addr(const char *addr)
{

	/*
	 * XXX: Turn it into assertion?
	 */
	if (peer_addr != NULL)
		log_errx(1, "%s called twice", __func__);

	peer_addr = checked_strdup(addr);
}

static void
log_common(int priority, int log_errno, const char *fmt, va_list ap)
{
	static char msgbuf[MSGBUF_LEN];
	static char msgbuf_strvised[MSGBUF_LEN * 4 + 1];
	char *errstr;
	int ret;

	ret = vsnprintf(msgbuf, sizeof(msgbuf), fmt, ap);
	if (ret < 0) {
		fprintf(stderr, "%s: snprintf failed", getprogname());
		syslog(LOG_CRIT, "snprintf failed");
		exit(1);
	}

	ret = strnvis(msgbuf_strvised, sizeof(msgbuf_strvised), msgbuf, VIS_NL);
	if (ret < 0) {
		fprintf(stderr, "%s: strnvis failed", getprogname());
		syslog(LOG_CRIT, "strnvis failed");
		exit(1);
	}

	if (log_errno == -1) {
		if (peer_name != NULL) {
			fprintf(stderr, "%s: %s (%s): %s\n", getprogname(),
			    peer_addr, peer_name, msgbuf_strvised);
			syslog(priority, "%s (%s): %s",
			    peer_addr, peer_name, msgbuf_strvised);
		} else if (peer_addr != NULL) {
			fprintf(stderr, "%s: %s: %s\n", getprogname(),
			    peer_addr, msgbuf_strvised);
			syslog(priority, "%s: %s",
			    peer_addr, msgbuf_strvised);
		} else {
			fprintf(stderr, "%s: %s\n", getprogname(), msgbuf_strvised);
			syslog(priority, "%s", msgbuf_strvised);
		}

	} else {
		errstr = strerror(log_errno);

		if (peer_name != NULL) {
			fprintf(stderr, "%s: %s (%s): %s: %s\n", getprogname(),
			    peer_addr, peer_name, msgbuf_strvised, errstr);
			syslog(priority, "%s (%s): %s: %s",
			    peer_addr, peer_name, msgbuf_strvised, errstr);
		} else if (peer_addr != NULL) {
			fprintf(stderr, "%s: %s: %s: %s\n", getprogname(),
			    peer_addr, msgbuf_strvised, errstr);
			syslog(priority, "%s: %s: %s",
			    peer_addr, msgbuf_strvised, errstr);
		} else {
			fprintf(stderr, "%s: %s: %s\n", getprogname(),
			    msgbuf_strvised, errstr);
			syslog(priority, "%s: %s",
			    msgbuf_strvised, errstr);
		}
	}
}

void
log_err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_common(LOG_CRIT, errno, fmt, ap);
	va_end(ap);

	exit(eval);
}

void
log_errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_common(LOG_CRIT, -1, fmt, ap);
	va_end(ap);

	exit(eval);
}

void
log_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_common(LOG_WARNING, errno, fmt, ap);
	va_end(ap);
}

void
log_warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	log_common(LOG_WARNING, -1, fmt, ap);
	va_end(ap);
}

void
log_debugx(const char *fmt, ...)
{
	va_list ap;

	if (log_level == 0)
		return;

	va_start(ap, fmt);
	log_common(LOG_DEBUG, -1, fmt, ap);
	va_end(ap);
}
