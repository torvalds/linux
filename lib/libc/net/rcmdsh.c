/*	$OpenBSD: rcmdsh.c,v 1.7 2002/03/12 00:05:44 millert Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2001, MagniComp
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in 
 *    the documentation and/or other materials provided with the distribution. 
 * 3. Neither the name of the MagniComp nor the names of its contributors may
 *    be used to endorse or promote products derived from this software
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This is an rcmd() replacement originally by
 * Chris Siebenmann <cks@utcc.utoronto.ca>.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#include <errno.h>
#include <netdb.h>
#include <paths.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "un-namespace.h"

/*
 * This is a replacement rcmd() function that uses the rsh(1)
 * program in place of a direct rcmd(3) function call so as to
 * avoid having to be root.  Note that rport is ignored.
 */
int
rcmdsh(char **ahost, int rport, const char *locuser, const char *remuser,
    const char *cmd, const char *rshprog)
{
	struct addrinfo hints, *res;
	int sp[2], error;
	pid_t cpid;
	char *p;
	struct passwd *pw;
	char num[8];
	static char hbuf[NI_MAXHOST];

	/* What rsh/shell to use. */
	if (rshprog == NULL)
		rshprog = _PATH_RSH;

	/* locuser must exist on this host. */
	if ((pw = getpwnam(locuser)) == NULL) {
		(void)fprintf(stderr, "rcmdsh: unknown user: %s\n", locuser);
		return (-1);
	}

	/* Validate remote hostname. */
	if (strcmp(*ahost, "localhost") != 0) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_flags = AI_CANONNAME;
		hints.ai_family = PF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		(void)snprintf(num, sizeof(num), "%u",
		    (unsigned int)ntohs(rport));
		error = getaddrinfo(*ahost, num, &hints, &res);
		if (error) {
			fprintf(stderr, "rcmdsh: getaddrinfo: %s\n",
				gai_strerror(error));
			return (-1);
		}
		if (res->ai_canonname) {
			strncpy(hbuf, res->ai_canonname, sizeof(hbuf) - 1);
			hbuf[sizeof(hbuf) - 1] = '\0';
			*ahost = hbuf;
		}
		freeaddrinfo(res);
	}

	/* Get a socketpair we'll use for stdin and stdout. */
	if (_socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) == -1) {
		perror("rcmdsh: socketpair");
		return (-1);
	}

	cpid = fork();
	if (cpid == -1) {
		perror("rcmdsh: fork failed");
		return (-1);
	} else if (cpid == 0) {
		/*
		 * Child.  We use sp[1] to be stdin/stdout, and close sp[0].
		 */
		(void)_close(sp[0]);
		if (_dup2(sp[1], 0) == -1 || _dup2(0, 1) == -1) {
			perror("rcmdsh: dup2 failed");
			_exit(255);
		}
		/* Fork again to lose parent. */
		cpid = fork();
		if (cpid == -1) {
			perror("rcmdsh: fork to lose parent failed");
			_exit(255);
		}
		if (cpid > 0)
			_exit(0);

		/* In grandchild here.  Become local user for rshprog. */
		if (setuid(pw->pw_uid) == -1) {
			(void)fprintf(stderr, "rcmdsh: setuid(%u): %s\n",
			    pw->pw_uid, strerror(errno));
			_exit(255);
		}

		/*
		 * If remote host is "localhost" and local and remote users
		 * are the same, avoid running remote shell for efficiency.
		 */
		if (strcmp(*ahost, "localhost") == 0 &&
		    strcmp(locuser, remuser) == 0) {
			if (pw->pw_shell[0] == '\0')
				rshprog = _PATH_BSHELL;
			else
				rshprog = pw->pw_shell;
			p = strrchr(rshprog, '/');
			execlp(rshprog, p ? p + 1 : rshprog, "-c", cmd,
			    (char *)NULL);
		} else {
			p = strrchr(rshprog, '/');
			execlp(rshprog, p ? p + 1 : rshprog, *ahost, "-l",
			    remuser, cmd, (char *)NULL);
		}
		(void)fprintf(stderr, "rcmdsh: execlp %s failed: %s\n",
		    rshprog, strerror(errno));
		_exit(255);
	} else {
		/* Parent. close sp[1], return sp[0]. */
		(void)_close(sp[1]);
		/* Reap child. */
		(void)_waitpid(cpid, NULL, 0);
		return (sp[0]);
	}
	/* NOTREACHED */
}
