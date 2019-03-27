/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1983, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *
 *	From: @(#)common.c	8.5 (Berkeley) 4/28/95
 */

#include "lp.cdefs.h"		/* A cross-platform version of <sys/cdefs.h> */
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <dirent.h>		/* required for lp.h, not used here */
#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lp.h"
#include "lp.local.h"
#include "pathnames.h"

/*
 * 'local_host' is always the hostname of the machine which is running
 * lpr (lpd, whatever), while 'from_host' either points at 'local_host'
 * or points at a different buffer when receiving a job from a remote
 * machine (and that buffer has the hostname of that remote machine).
 */
char		 local_host[MAXHOSTNAMELEN];	/* host running lpd/lpr */
const char	*from_host = local_host;	/* client's machine name */
const char	*from_ip = "";		/* client machine's IP address */

#ifdef INET6
u_char	family = PF_UNSPEC;
#else
u_char	family = PF_INET;
#endif

/*
 * Create a TCP connection to host "rhost" at port "rport".
 * If rport == 0, then use the printer service port.
 * Most of this code comes from rcmd.c.
 */
int
getport(const struct printer *pp, const char *rhost, int rport)
{
	struct addrinfo hints, *res, *ai;
	int s, timo = 1, lport = IPPORT_RESERVED - 1;
	int error, refused = 0;

	/*
	 * Get the host address and port number to connect to.
	 */
	if (rhost == NULL)
		fatal(pp, "no remote host to connect to");
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;
	error = getaddrinfo(rhost, (rport == 0 ? "printer" : NULL),
			  &hints, &res);
	if (error)
		fatal(pp, "%s\n", gai_strerror(error));
	if (rport != 0)
		((struct sockaddr_in *) res->ai_addr)->sin_port = htons(rport);

	/*
	 * Try connecting to the server.
	 */
	ai = res;
retry:
	PRIV_START
	s = rresvport_af(&lport, ai->ai_family);
	PRIV_END
	if (s < 0) {
		if (errno != EAGAIN) {
			if (ai->ai_next) {
				ai = ai->ai_next;
				goto retry;
			}
			if (refused && timo <= 16) {
				sleep(timo);
				timo *= 2;
				refused = 0;
				ai = res;
				goto retry;
			}
		}
		freeaddrinfo(res);
		return(-1);
	}
	if (connect(s, ai->ai_addr, ai->ai_addrlen) < 0) {
		error = errno;
		(void) close(s);
		errno = error;
		/*
		 * This used to decrement lport, but the current semantics
		 * of rresvport do not provide such a function (in fact,
		 * rresvport should guarantee that the chosen port will
		 * never result in an EADDRINUSE).
		 */
		if (errno == EADDRINUSE) {
			goto retry;
		}

		if (errno == ECONNREFUSED)
			refused++;

		if (ai->ai_next != NULL) {
			ai = ai->ai_next;
			goto retry;
		}
		if (refused && timo <= 16) {
			sleep(timo);
			timo *= 2;
			refused = 0;
			ai = res;
			goto retry;
		}
		freeaddrinfo(res);
		return(-1);
	}
	freeaddrinfo(res);
	return(s);
}

/*
 * Figure out whether the local machine is the same
 * as the remote machine (RM) entry (if it exists).
 * We do this by counting the intersection of our
 * address list and theirs.  This is better than the
 * old method (comparing the canonical names), as it
 * allows load-sharing between multiple print servers.
 * The return value is an error message which must be
 * free()d.
 */
char *
checkremote(struct printer *pp)
{
	char lclhost[MAXHOSTNAMELEN];
	struct addrinfo hints, *local_res, *remote_res, *lr, *rr;
	char *error;
	int ncommonaddrs, errno;
	char h1[NI_MAXHOST], h2[NI_MAXHOST];

	if (!pp->rp_matches_local) { /* Remote printer doesn't match local */
		pp->remote = 1;
		return NULL;
	}

	pp->remote = 0;	/* assume printer is local */
	if (pp->remote_host == NULL)
		return NULL;

	/* get the addresses of the local host */
	gethostname(lclhost, sizeof(lclhost));
	lclhost[sizeof(lclhost) - 1] = '\0';

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((errno = getaddrinfo(lclhost, NULL, &hints, &local_res)) != 0) {
		asprintf(&error, "unable to get official name "
			 "for local machine %s: %s",
			 lclhost, gai_strerror(errno));
		return error;
	}

	/* get the official name of RM */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	if ((errno = getaddrinfo(pp->remote_host, NULL,
				 &hints, &remote_res)) != 0) {
		asprintf(&error, "unable to get address list for "
			 "remote machine %s: %s",
			 pp->remote_host, gai_strerror(errno));
		freeaddrinfo(local_res);
		return error;
	}

	ncommonaddrs = 0;
	for (lr = local_res; lr; lr = lr->ai_next) {
		h1[0] = '\0';
		if (getnameinfo(lr->ai_addr, lr->ai_addrlen, h1, sizeof(h1),
				NULL, 0, NI_NUMERICHOST) != 0)
			continue;
		for (rr = remote_res; rr; rr = rr->ai_next) {
			h2[0] = '\0';
			if (getnameinfo(rr->ai_addr, rr->ai_addrlen,
					h2, sizeof(h2), NULL, 0,
					NI_NUMERICHOST) != 0)
				continue;
			if (strcmp(h1, h2) == 0)
				ncommonaddrs++;
		}
	}
			
	/*
	 * if the two hosts do not share at least one IP address
	 * then the printer must be remote.
	 */
	if (ncommonaddrs == 0)
		pp->remote = 1;
	freeaddrinfo(local_res);
	freeaddrinfo(remote_res);
	return NULL;
}

/*
 * This isn't really network-related, but it's used here to write
 * multi-part strings onto sockets without using stdio.  Return
 * values are as for writev(2).
 */
ssize_t
writel(int strm, ...)
{
	va_list ap;
	int i, n;
	const char *cp;
#define NIOV 12
	struct iovec iov[NIOV], *iovp = iov;
	ssize_t retval;

	/* first count them */
	va_start(ap, strm);
	n = 0;
	do {
		cp = va_arg(ap, char *);
		n++;
	} while (cp);
	va_end(ap);
	n--;			/* correct for count of trailing null */

	if (n > NIOV) {
		iovp = malloc(n * sizeof *iovp);
		if (iovp == NULL)
			return -1;
	}

	/* now make up iovec and send */
	va_start(ap, strm);
	for (i = 0; i < n; i++) {
		iovp[i].iov_base = va_arg(ap, char *);
		iovp[i].iov_len = strlen(iovp[i].iov_base);
	}
	va_end(ap);
	retval = writev(strm, iovp, n);
	if (iovp != iov)
		free(iovp);
	return retval;
}
