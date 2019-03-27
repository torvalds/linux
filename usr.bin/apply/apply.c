/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry.
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
static char sccsid[] = "@(#)apply.c	8.4 (Berkeley) 4/4/94";
#endif
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/types.h>
#include <sys/sbuf.h>
#include <sys/wait.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ISMAGICNO(p) \
	    (p)[0] == magic && isdigit((unsigned char)(p)[1]) && (p)[1] != '0'

static int	exec_shell(const char *, const char *, const char *);
static void	usage(void);

int
main(int argc, char *argv[])
{
	struct sbuf *cmdbuf;
	long arg_max;
	int ch, debug, i, magic, n, nargs, rval;
	size_t cmdsize;
	char buf[4];
	char *cmd, *name, *p, *shell, *slashp, *tmpshell;

	debug = 0;
	magic = '%';		/* Default magic char is `%'. */
	nargs = -1;
	while ((ch = getopt(argc, argv, "a:d0123456789")) != -1)
		switch (ch) {
		case 'a':
			if (optarg[0] == '\0' || optarg[1] != '\0')
				errx(1,
				    "illegal magic character specification");
			magic = optarg[0];
			break;
		case 'd':
			debug = 1;
			break;
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (nargs != -1)
				errx(1,
				    "only one -# argument may be specified");
			nargs = optopt - '0';
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;

	if (argc < 2)
		usage();

	/*
	 * The command to run is argv[0], and the args are argv[1..].
	 * Look for %digit references in the command, remembering the
	 * largest one.
	 */
	for (n = 0, p = argv[0]; *p != '\0'; ++p)
		if (ISMAGICNO(p)) {
			++p;
			if (p[0] - '0' > n)
				n = p[0] - '0';
		}

	/*
	 * Figure out the shell and name arguments to pass to execl()
	 * in exec_shell().  Always malloc() shell and just set name
	 * to point at the last part of shell if there are any backslashes,
	 * otherwise just set it to point at the space malloc()'d.  If
	 * SHELL environment variable exists, replace contents of
	 * shell with it.
	 */
	shell = name = NULL;
	tmpshell = getenv("SHELL");
	shell = (tmpshell != NULL) ? strdup(tmpshell) : strdup(_PATH_BSHELL);
	if (shell == NULL)
		err(1, "strdup() failed");
	slashp = strrchr(shell, '/');
	name = (slashp != NULL) ? slashp + 1 : shell;

	/*
	 * If there were any %digit references, then use those, otherwise
	 * build a new command string with sufficient %digit references at
	 * the end to consume (nargs) arguments each time round the loop.
	 * Allocate enough space to hold the maximum command.  Save the
	 * size to pass to snprintf().
	 */
	if (n == 0) {
		cmdsize = strlen(argv[0]) + 9 * (sizeof(" %1") - 1) + 1;
		if ((cmd = malloc(cmdsize)) == NULL)
			err(1, NULL);
		strlcpy(cmd, argv[0], cmdsize);

		/* If nargs not set, default to a single argument. */
		if (nargs == -1)
			nargs = 1;

		for (i = 1; i <= nargs; i++) {
			snprintf(buf, sizeof(buf), " %c%d", magic, i);
			strlcat(cmd, buf, cmdsize);
		}

		/*
		 * If nargs set to the special value 0, eat a single
		 * argument for each command execution.
		 */
		if (nargs == 0)
			nargs = 1;
	} else {
		if ((cmd = strdup(argv[0])) == NULL)
			err(1, NULL);
		nargs = n;
	}

	cmdbuf = sbuf_new(NULL, NULL, 1024, SBUF_AUTOEXTEND);
	if (cmdbuf == NULL)
		err(1, NULL);

	arg_max = sysconf(_SC_ARG_MAX);

	/*
	 * (argc) and (argv) are still offset by one to make it simpler to
	 * expand %digit references.  At the end of the loop check for (argc)
	 * equals 1 means that all the (argv) has been consumed.
	 */
	for (rval = 0; argc > nargs; argc -= nargs, argv += nargs) {
		sbuf_clear(cmdbuf);
		sbuf_cat(cmdbuf, "exec ");
		/* Expand command argv references. */
		for (p = cmd; *p != '\0'; ++p) {
			if (ISMAGICNO(p)) {
				if (sbuf_cat(cmdbuf, argv[(++p)[0] - '0'])
				    == -1)
					errc(1, ENOMEM, "sbuf");
			} else {
				if (sbuf_putc(cmdbuf, *p) == -1)
					errc(1, ENOMEM, "sbuf");
			}
			if (sbuf_len(cmdbuf) > arg_max)
				errc(1, E2BIG, NULL);
		}

		/* Terminate the command string. */
		sbuf_finish(cmdbuf);

		/* Run the command. */
		if (debug)
			(void)printf("%s\n", sbuf_data(cmdbuf));
		else
			if (exec_shell(sbuf_data(cmdbuf), shell, name))
				rval = 1;
	}

	if (argc != 1)
		errx(1, "expecting additional argument%s after \"%s\"",
		    (nargs - argc) ? "s" : "", argv[argc - 1]);
	free(cmd);
	sbuf_delete(cmdbuf);
	free(shell);
	exit(rval);
}

/*
 * exec_shell --
 * 	Execute a shell command using passed use_shell and use_name
 * 	arguments.
 */
static int
exec_shell(const char *command, const char *use_shell, const char *use_name)
{
	pid_t pid;
	int omask, pstat;
	sig_t intsave, quitsave;

	if (!command)		/* just checking... */
		return(1);

	omask = sigblock(sigmask(SIGCHLD));
	switch(pid = vfork()) {
	case -1:			/* error */
		err(1, "vfork");
	case 0:				/* child */
		(void)sigsetmask(omask);
		execl(use_shell, use_name, "-c", command, (char *)NULL);
		warn("%s", use_shell);
		_exit(1);
	}
	intsave = signal(SIGINT, SIG_IGN);
	quitsave = signal(SIGQUIT, SIG_IGN);
	pid = waitpid(pid, &pstat, 0);
	(void)sigsetmask(omask);
	(void)signal(SIGINT, intsave);
	(void)signal(SIGQUIT, quitsave);
	return(pid == -1 ? -1 : pstat);
}

static void
usage(void)
{

	(void)fprintf(stderr,
	"usage: apply [-a magic] [-d] [-0123456789] command arguments ...\n");
	exit(1);
}
