/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1983, 1993, 1994
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

#if 0
#ifndef lint
static char sccsid[] = "@(#)startdaemon.c	8.2 (Berkeley) 4/17/94";
#endif /* not lint */
#endif

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "lp.h"
#include "pathnames.h"

/*
 * Tell the printer daemon that there are new files in the spool directory.
 */

int
startdaemon(const struct printer *pp)
{
	struct sockaddr_un un;
	register int s, n;
	int connectres;
	char c;

	s = socket(PF_LOCAL, SOCK_STREAM, 0);
	if (s < 0) {
		warn("socket");
		return(0);
	}
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_LOCAL;
	strcpy(un.sun_path, _PATH_SOCKETNAME);
#ifndef SUN_LEN
#define SUN_LEN(unp) (strlen((unp)->sun_path) + 2)
#endif
	PRIV_START
	connectres = connect(s, (struct sockaddr *)&un, SUN_LEN(&un));
	PRIV_END
	if (connectres < 0) {
		warn("Unable to connect to %s", _PATH_SOCKETNAME);
		warnx("Check to see if the master 'lpd' process is running.");
		(void) close(s);
		return(0);
	}

	/*
	 * Avoid overruns without putting artificial limitations on 
	 * the length.
	 */
	if (writel(s, "\1", pp->printer, "\n", (char *)0) <= 0) {
		warn("write");
		(void) close(s);
		return(0);
	}
	if (read(s, &c, 1) == 1) {
		if (c == '\0') {		/* everything is OK */
			(void) close(s);
			return(1);
		}
		putchar(c);
	}
	while ((n = read(s, &c, 1)) > 0)
		putchar(c);
	(void) close(s);
	return(0);
}
