/*	$OpenBSD: rcmdsh.c,v 1.20 2019/06/28 13:32:42 deraadt Exp $	*/ 

/*
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

#include      <sys/types.h>
#include      <sys/socket.h>
#include      <sys/wait.h>
#include      <signal.h>
#include      <errno.h>
#include      <limits.h>
#include      <netdb.h>
#include      <stdio.h>
#include      <stdlib.h>
#include      <string.h>
#include      <pwd.h>
#include      <paths.h>
#include      <unistd.h>

/*
 * This is a replacement rcmd() function that uses the ssh(1)
 * program in place of a direct rcmd(3) function call so as to
 * avoid having to be root.  Note that rport is ignored.
 */
int
rcmdsh(char **ahost, int rport, const char *locuser, const char *remuser,
    const char *cmd, char *rshprog)
{
	static char hbuf[HOST_NAME_MAX+1];
	struct addrinfo hint, *res;
	int sp[2];
	pid_t cpid;
	char *p, pwbuf[_PW_BUF_LEN];
	struct passwd pwstore, *pw = NULL;

	/* What rsh/shell to use. */
	if (rshprog == NULL)
		rshprog = _PATH_RSH;

	/* locuser must exist on this host. */
	getpwnam_r(locuser, &pwstore, pwbuf, sizeof(pwbuf), &pw);
	if (pw == NULL) {
		(void) fprintf(stderr, "rcmdsh: unknown user: %s\n", locuser);
		return(-1);
	}

	/* Validate remote hostname. */
	if (strcmp(*ahost, "localhost") != 0) {
		memset(&hint, 0, sizeof(hint));
		hint.ai_family = PF_UNSPEC;
		hint.ai_flags = AI_CANONNAME;
		if (getaddrinfo(*ahost, NULL, &hint, &res) == 0) {
			if (res->ai_canonname) {
				strlcpy(hbuf, res->ai_canonname, sizeof(hbuf));
				*ahost = hbuf;
			}
			freeaddrinfo(res);
		}
	}

	/* Get a socketpair we'll use for stdin and stdout. */
	if (socketpair(AF_UNIX, SOCK_STREAM, PF_UNSPEC, sp) == -1) {
		perror("rcmdsh: socketpair");
		return(-1);
	}

	cpid = fork();
	if (cpid == -1) {
		perror("rcmdsh: fork failed");
		return(-1);
	} else if (cpid == 0) {
		/*
		 * Child.  We use sp[1] to be stdin/stdout, and close sp[0].
		 */
		(void) close(sp[0]);
		if (dup2(sp[1], 0) == -1 || dup2(0, 1) == -1) {
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
		if (setuid(pw->pw_uid)) {
			(void) fprintf(stderr, "rcmdsh: setuid(%u): %s\n",
				       pw->pw_uid, strerror(errno));
			_exit(255);
		}

		/*
		 * If remote host is "localhost" and local and remote user
		 * are the same, avoid running remote shell for efficiency.
		 */
		if (!strcmp(*ahost, "localhost") && !strcmp(locuser, remuser)) {
			char *argv[4];
			if (pw->pw_shell[0] == '\0')
				rshprog = _PATH_BSHELL;
			else
				rshprog = pw->pw_shell;
			p = strrchr(rshprog, '/');
			argv[0] = p ? p + 1 : rshprog;
			argv[1] = "-c";
			argv[2] = (char *)cmd;
			argv[3] = NULL;
			execvp(rshprog, argv);
		} else if ((p = strchr(rshprog, ' ')) == NULL) {
			/* simple case */
			char *argv[6];
			p = strrchr(rshprog, '/');
			argv[0] = p ? p + 1 : rshprog;
			argv[1] = "-l";
			argv[2] = (char *)remuser;
			argv[3] = *ahost;
			argv[4] = (char *)cmd;
			argv[5] = NULL;
			execvp(rshprog, argv);
		} else {
			/* must pull args out of rshprog and dyn alloc argv */
			char **argv, **ap;
			int n;
			for (n = 7; (p = strchr(++p, ' ')) != NULL; n++)
				continue;
			rshprog = strdup(rshprog);
			ap = argv = calloc(sizeof(char *), n);
			if (rshprog == NULL || argv == NULL) {
				perror("rcmdsh");
				_exit(255);
			}
			while ((p = strsep(&rshprog, " ")) != NULL) {
				if (*p == '\0')
					continue;
				*ap++ = p;
			}
			if (ap != argv)		/* all spaces?!? */
				rshprog = argv[0];
			if ((p = strrchr(argv[0], '/')) != NULL)
				argv[0] = p + 1;
			*ap++ = "-l";
			*ap++ = (char *)remuser;
			*ap++ = *ahost;
			*ap++ = (char *)cmd;
			*ap++ = NULL;
			execvp(rshprog, argv);
		}
		(void) fprintf(stderr, "rcmdsh: execvp %s failed: %s\n",
			       rshprog, strerror(errno));
		_exit(255);
	} else {
		/* Parent. close sp[1], return sp[0]. */
		(void) close(sp[1]);
		/* Reap child. */
		while (waitpid(cpid, NULL, 0) == -1 && errno == EINTR)
			;
		return(sp[0]);
	}
	/* NOTREACHED */
}
DEF_WEAK(rcmdsh);
