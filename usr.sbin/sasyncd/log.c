/*	$OpenBSD: log.c,v 1.7 2016/08/27 04:21:08 guenther Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Multicom Security AB.
 */

#include <sys/types.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>

#include "sasyncd.h"

static char logbuf[2048];

void
log_init(char *pname)
{
	tzset();
	openlog(pname, LOG_CONS | LOG_PID, LOG_DAEMON);
}

static void
log_output(char *msg)
{
	if (cfgstate.debug)
		fprintf(stderr, "%s\n", msg);
	else
		syslog(LOG_CRIT, "%s", msg);
}

void
log_err(const char *fmt, ...)
{
	extern char	*__progname;
	int		off = 0;
	va_list		ap;

	if (cfgstate.debug) {
		snprintf(logbuf, sizeof logbuf, "%s: ", __progname);
		off = strlen(logbuf);
	}

	va_start(ap, fmt);
	(void)vsnprintf(logbuf + off, sizeof logbuf - off, fmt, ap);
	va_end(ap);

	strlcat(logbuf, ": ", sizeof logbuf);
	strlcat(logbuf, strerror(errno), sizeof logbuf);

	log_output(logbuf);
	return;
}

void
log_msg(int minlevel, const char *fmt, ...)
{
	va_list ap;

	if (cfgstate.verboselevel < minlevel)
		return;

	va_start(ap, fmt);
	(void)vsnprintf(logbuf, sizeof logbuf, fmt, ap);
	va_end(ap);

	log_output(logbuf);
	return;
}
