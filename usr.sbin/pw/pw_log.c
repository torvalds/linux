/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 1996
 *	David L. Nugent.  All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY DAVID L. NUGENT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL DAVID L. NUGENT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
static const char rcsid[] =
  "$FreeBSD$";
#endif /* not lint */

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>

#include "pw.h"

static FILE	*logfile = NULL;

void
pw_log(struct userconf * cnf, int mode, int which, char const * fmt,...)
{
	va_list		argp;
	time_t		now;
	const char	*cp, *name;
	struct tm	*t;
	int		fd, i, rlen;
	char		nfmt[256], sname[32];

	if (cnf->logfile == NULL || cnf->logfile[0] == '\0') {
		return;
	}

	if (logfile == NULL) {
		/* With umask==0 we need to control file access modes on create */
		fd = open(cnf->logfile, O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (fd == -1) {
			return;
		}
		logfile = fdopen(fd, "a");
		if (logfile == NULL) {
			return;
		}
	}

	if ((name = getenv("LOGNAME")) == NULL &&
	    (name = getenv("USER")) == NULL) {
		strcpy(sname, "unknown");
	} else {
		/*
		 * Since "name" will be embedded in a printf-like format,
		 * we must sanitize it:
		 *
		 *    Limit its length so other information in the message
		 *    is not truncated
		 *
		 *    Squeeze out embedded whitespace for the benefit of
		 *    log file parsers
		 *
		 *    Escape embedded % characters with another %
		 */
		for (i = 0, cp = name;
		    *cp != '\0' && i < (int)sizeof(sname) - 1; cp++) {
			if (*cp == '%') {
				if (i < (int)sizeof(sname) - 2) {
					sname[i++] = '%';
					sname[i++] = '%';
				} else {
					break;
				}
			} else if (!isspace(*cp)) {
				sname[i++] = *cp;
			} /* else do nothing */
		}
		if (i == 0) {
			strcpy(sname, "unknown");
		} else {
			sname[i] = '\0';
		}
	}
	now = time(NULL);
	t = localtime(&now);
	/* ISO 8601 International Standard Date format */
	strftime(nfmt, sizeof nfmt, "%Y-%m-%d %T ", t);
	rlen = sizeof(nfmt) - strlen(nfmt);
	if (rlen <= 0 || snprintf(nfmt + strlen(nfmt), rlen,
	    "[%s:%s%s] %s\n", sname, Which[which], Modes[mode],
	    fmt) >= rlen) {
		warnx("log format overflow, user name=%s", sname);
	} else {
		va_start(argp, fmt);
		vfprintf(logfile, nfmt, argp);
		va_end(argp);
		fflush(logfile);
	}
}
