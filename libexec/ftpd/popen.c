/*	$OpenBSD: popen.c,v 1.31 2023/02/17 17:59:36 miod Exp $	*/
/*	$NetBSD: popen.c,v 1.5 1995/04/11 02:45:00 cgd Exp $	*/

/*
 * Copyright (c) 1988, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
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
 *
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <netinet/in.h>

#include "monitor.h"
#include "extern.h"

/*
 * Special version of popen which avoids call to shell.  This ensures no one
 * may create a pipe to a hidden program as a side effect of a list or dir
 * command.
 */

FILE *
ftpd_ls(const char *path, pid_t *pidptr)
{
	FILE *iop;
	int argc = 0, pdes[2];
	pid_t pid;
	char **pop, *argv[_POSIX_ARG_MAX];

	if (pipe(pdes) == -1)
		return (NULL);

	/* break up string into pieces */
	argv[argc++] = "/bin/ls";
	argv[argc++] = "-lgA";
	argv[argc++] = "--";

	/* glob that path */
	if (path != NULL) {
		glob_t gl;

		memset(&gl, 0, sizeof(gl));
		if (glob(path,
		    GLOB_BRACE|GLOB_NOCHECK|GLOB_QUOTE|GLOB_TILDE|GLOB_LIMIT,
		    NULL, &gl)) {
			fatal ("Glob error.");
		} else if (gl.gl_pathc > 0) {
			for (pop = gl.gl_pathv; *pop && argc < _POSIX_ARG_MAX-1;
			    pop++) {
				argv[argc++] = strdup(*pop);
				if (argv[argc - 1] == NULL)
					fatal ("Out of memory.");
			}
		}
		globfree(&gl);
	}
	argv[argc] = NULL;

	iop = NULL;

	switch (pid = fork()) {
	case -1:			/* error */
		(void)close(pdes[0]);
		(void)close(pdes[1]);
		goto pfree;
		/* NOTREACHED */
	case 0:				/* child */
		if (pdes[1] != STDOUT_FILENO) {
			dup2(pdes[1], STDOUT_FILENO);
			(void)close(pdes[1]);
		}
		dup2(STDOUT_FILENO, STDERR_FILENO); /* stderr too! */
		(void)close(pdes[0]);
		closelog();

		extern int ls_main(int, char **);

		/* reset getopt for ls_main */
		optreset = optind = 1;
		exit(ls_main(argc, argv));
	}
	/* parent; assume fdopen can't fail...  */
	iop = fdopen(pdes[0], "r");
	(void)close(pdes[1]);
	*pidptr = pid;

 pfree:
	for (argc = 3; argv[argc] != NULL; argc++)
		free(argv[argc]);

	return (iop);
}

int
ftpd_pclose(FILE *iop, pid_t pid)
{
	int status;
	pid_t rv;
	sigset_t sigset, osigset;

	(void)fclose(iop);
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	sigaddset(&sigset, SIGQUIT);
	sigaddset(&sigset, SIGHUP);
	sigprocmask(SIG_BLOCK, &sigset, &osigset);
	while ((rv = waitpid(pid, &status, 0)) == -1 && errno == EINTR)
		continue;
	sigprocmask(SIG_SETMASK, &osigset, NULL);
	if (rv == -1)
		return (-1);
	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	return (1);
}
