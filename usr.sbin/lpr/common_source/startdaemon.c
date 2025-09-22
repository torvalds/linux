/*	$OpenBSD: startdaemon.c,v 1.18 2019/07/03 03:24:03 deraadt Exp $	*/
/*	$NetBSD: startdaemon.c,v 1.10 1998/07/18 05:04:39 lukem Exp $	*/

/*
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

#include <sys/socket.h>
#include <sys/un.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>
#include <signal.h>

#include "lp.h"
#include "pathnames.h"

/*
 * Tell the printer daemon that there are new files in the spool directory.
 */
int
startdaemon(char *printer)
{
	struct sockaddr_un un;
	int s;
	size_t n;
	char buf[BUFSIZ];

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if (s < 0) {
		warn("socket");
		return(0);
	}
	memset(&un, 0, sizeof(un));
	un.sun_family = AF_UNIX;
	strlcpy(un.sun_path, _PATH_SOCKETNAME, sizeof(un.sun_path));
	siginterrupt(SIGINT, 1);
	PRIV_START;
	if (connect(s, (struct sockaddr *)&un, sizeof(un)) < 0) {
		int saved_errno = errno;
		if (errno == EINTR && gotintr) {
			PRIV_END;
			siginterrupt(SIGINT, 0);
			close(s);
			return(0);
		}
		PRIV_END;
		siginterrupt(SIGINT, 0);
		warnc(saved_errno, "connect");
		(void)close(s);
		return(0);
	}
	PRIV_END;
	siginterrupt(SIGINT, 0);
	if ((n = snprintf(buf, sizeof(buf), "\1%s\n", printer)) < 0 ||
	    n >= sizeof(buf)) {
		close(s);
		return (0);
	}

	/* XXX atomicio inside siginterrupt? */
	if (write(s, buf, n) != n) {
		warn("write");
		(void)close(s);
		return(0);
	}
	if (read(s, buf, 1) == 1) {
		if (buf[0] == '\0') {		/* everything is OK */
			(void)close(s);
			return(1);
		}
		putchar(buf[0]);
	}
	while ((n = read(s, buf, sizeof(buf))) > 0)
		fwrite(buf, 1, n, stdout);
	(void)close(s);
	return(0);
}
