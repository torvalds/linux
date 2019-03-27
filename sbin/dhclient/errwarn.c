/*	$OpenBSD: errwarn.c,v 1.7 2004/05/04 22:23:01 mickey Exp $	*/

/* Errors and warnings... */

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1996 The Internet Software Consortium.
 * All Rights Reserved.
 * Copyright (c) 1995 RadioMail Corporation.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of RadioMail Corporation, the Internet Software
 *    Consortium nor the names of its contributors may be used to endorse
 *    or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY RADIOMAIL CORPORATION, THE INTERNET
 * SOFTWARE CONSORTIUM AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL RADIOMAIL CORPORATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This software was written for RadioMail Corporation by Ted Lemon
 * under a contract with Vixie Enterprises.   Further modifications have
 * been made for the Internet Software Consortium under a contract
 * with Vixie Laboratories.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <errno.h>

#include "dhcpd.h"

static void do_percentm(char *obuf, size_t size, const char *ibuf);

static char mbuf[1024];
static char fbuf[1024];

int warnings_occurred;

/*
 * Log an error message, then exit.
 */
void
error(const char *fmt, ...)
{
	va_list list;

	do_percentm(fbuf, sizeof(fbuf), fmt);

	va_start(list, fmt);
	vsnprintf(mbuf, sizeof(mbuf), fbuf, list);
	va_end(list);

#ifndef DEBUG
	cap_syslog(capsyslog, log_priority | LOG_ERR, "%s", mbuf);
#endif

	/* Also log it to stderr? */
	if (log_perror) {
		write(2, mbuf, strlen(mbuf));
		write(2, "\n", 1);
	}

	cap_syslog(capsyslog, LOG_CRIT, "exiting.");
	if (log_perror) {
		fprintf(stderr, "exiting.\n");
		fflush(stderr);
	}
	if (pidfile != NULL)
		pidfile_remove(pidfile);
	exit(1);
}

/*
 * Log a warning message...
 */
int
warning(const char *fmt, ...)
{
	va_list list;

	do_percentm(fbuf, sizeof(fbuf), fmt);

	va_start(list, fmt);
	vsnprintf(mbuf, sizeof(mbuf), fbuf, list);
	va_end(list);

#ifndef DEBUG
	cap_syslog(capsyslog, log_priority | LOG_ERR, "%s", mbuf);
#endif

	if (log_perror) {
		write(2, mbuf, strlen(mbuf));
		write(2, "\n", 1);
	}

	return (0);
}

/*
 * Log a note...
 */
int
note(const char *fmt, ...)
{
	va_list list;

	do_percentm(fbuf, sizeof(fbuf), fmt);

	va_start(list, fmt);
	vsnprintf(mbuf, sizeof(mbuf), fbuf, list);
	va_end(list);

#ifndef DEBUG
	cap_syslog(capsyslog, log_priority | LOG_INFO, "%s", mbuf);
#endif

	if (log_perror) {
		write(2, mbuf, strlen(mbuf));
		write(2, "\n", 1);
	}

	return (0);
}

/*
 * Log a debug message...
 */
int
debug(const char *fmt, ...)
{
	va_list list;

	do_percentm(fbuf, sizeof(fbuf), fmt);

	va_start(list, fmt);
	vsnprintf(mbuf, sizeof(mbuf), fbuf, list);
	va_end(list);

#ifndef DEBUG
	cap_syslog(capsyslog, log_priority | LOG_DEBUG, "%s", mbuf);
#endif

	if (log_perror) {
		write(2, mbuf, strlen(mbuf));
		write(2, "\n", 1);
	}

	return (0);
}

/*
 * Find %m in the input string and substitute an error message string.
 */
static void
do_percentm(char *obuf, size_t size, const char *ibuf)
{
	char ch;
	const char *s = ibuf;
	char *t = obuf;
	size_t prlen;
	size_t fmt_left;
	int saved_errno = errno;

	/*
	 * We wouldn't need this mess if printf handled %m, or if
	 * strerror() had been invented before syslog().
	 */
	for (fmt_left = size; (ch = *s); ++s) {
		if (ch == '%' && s[1] == 'm') {
			++s;
			prlen = snprintf(t, fmt_left, "%s",
			    strerror(saved_errno));
			if (prlen >= fmt_left)
				prlen = fmt_left - 1;
			t += prlen;
			fmt_left -= prlen;
		} else {
			if (fmt_left > 1) {
				*t++ = ch;
				fmt_left--;
			}
		}
	}
	*t = '\0';
}

int
parse_warn(const char *fmt, ...)
{
	va_list list;
	static char spaces[] =
	    "                                        "
	    "                                        "; /* 80 spaces */

	do_percentm(mbuf, sizeof(mbuf), fmt);
	snprintf(fbuf, sizeof(fbuf), "%s line %d: %s", tlname, lexline, mbuf);
	va_start(list, fmt);
	vsnprintf(mbuf, sizeof(mbuf), fbuf, list);
	va_end(list);

#ifndef DEBUG
	cap_syslog(capsyslog, log_priority | LOG_ERR, "%s", mbuf);
	cap_syslog(capsyslog, log_priority | LOG_ERR, "%s", token_line);
	if (lexline < 81)
		cap_syslog(capsyslog, log_priority | LOG_ERR,
		    "%s^", &spaces[sizeof(spaces) - lexchar]);
#endif

	if (log_perror) {
		write(2, mbuf, strlen(mbuf));
		write(2, "\n", 1);
		write(2, token_line, strlen(token_line));
		write(2, "\n", 1);
		write(2, spaces, lexchar - 1);
		write(2, "^\n", 2);
	}

	warnings_occurred = 1;

	return (0);
}
