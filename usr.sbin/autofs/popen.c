/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This code is derived from software written by Ken Arnold and
 * published in UNIX Review, Vol. 6, No. 8.
 *
 * Portions of this software were developed by Edward Tomasz Napierala
 * under sponsorship from the FreeBSD Foundation.
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/queue.h>
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <paths.h>

#include "common.h"

extern char **environ;

struct pid {
	SLIST_ENTRY(pid) next;
	FILE *outfp;
	pid_t pid;
	char *command;
};
static SLIST_HEAD(, pid) pidlist = SLIST_HEAD_INITIALIZER(pidlist);

#define	ARGV_LEN	42

/*
 * Replacement for popen(3), without stdin (which we do not use), but with
 * stderr, proper logging, and improved command line arguments passing.
 * Error handling is built in - if it returns, then it succeeded.
 */
FILE *
auto_popen(const char *argv0, ...)
{
	va_list ap;
	struct pid *cur, *p;
	pid_t pid;
	int error, i, nullfd, outfds[2];
	char *arg, *argv[ARGV_LEN], *command;

	nullfd = open(_PATH_DEVNULL, O_RDWR, 0);
	if (nullfd < 0)
		log_err(1, "cannot open %s", _PATH_DEVNULL);

	error = pipe(outfds);
	if (error != 0)
		log_err(1, "pipe");

	cur = malloc(sizeof(struct pid));
	if (cur == NULL)
		log_err(1, "malloc");

	argv[0] = checked_strdup(argv0);
	command = argv[0];

	va_start(ap, argv0);
	for (i = 1;; i++) {
		if (i >= ARGV_LEN)
			log_errx(1, "too many arguments to auto_popen");
		arg = va_arg(ap, char *);
		argv[i] = arg;
		if (arg == NULL)
			break;

		command = concat(command, ' ', arg);
	}
	va_end(ap);

	cur->command = checked_strdup(command);

	switch (pid = fork()) {
	case -1:			/* Error. */
		log_err(1, "fork");
		/* NOTREACHED */
	case 0:				/* Child. */
		dup2(nullfd, STDIN_FILENO);
		dup2(outfds[1], STDOUT_FILENO);

		close(nullfd);
		close(outfds[0]);
		close(outfds[1]);

		SLIST_FOREACH(p, &pidlist, next)
			close(fileno(p->outfp));
		execvp(argv[0], argv);
		log_err(1, "failed to execute %s", argv[0]);
		/* NOTREACHED */
	}

	log_debugx("executing \"%s\" as pid %d", command, pid);

	/* Parent; assume fdopen cannot fail. */
	cur->outfp = fdopen(outfds[0], "r");
	close(nullfd);
	close(outfds[1]);

	/* Link into list of file descriptors. */
	cur->pid = pid;
	SLIST_INSERT_HEAD(&pidlist, cur, next);

	return (cur->outfp);
}

int
auto_pclose(FILE *iop)
{
	struct pid *cur, *last = NULL;
	int status;
	pid_t pid;

	/*
	 * Find the appropriate file pointer and remove it from the list.
	 */
	SLIST_FOREACH(cur, &pidlist, next) {
		if (cur->outfp == iop)
			break;
		last = cur;
	}
	if (cur == NULL) {
		return (-1);
	}
	if (last == NULL)
		SLIST_REMOVE_HEAD(&pidlist, next);
	else
		SLIST_REMOVE_AFTER(last, next);

	fclose(cur->outfp);

	do {
		pid = wait4(cur->pid, &status, 0, NULL);
	} while (pid == -1 && errno == EINTR);

	if (WIFSIGNALED(status)) {
		log_warnx("\"%s\", pid %d, terminated with signal %d",
		    cur->command, pid, WTERMSIG(status));
		return (status);
	}

	if (WEXITSTATUS(status) != 0) {
		log_warnx("\"%s\", pid %d, terminated with exit status %d",
		    cur->command, pid, WEXITSTATUS(status));
		return (status);
	}

	log_debugx("\"%s\", pid %d, terminated gracefully", cur->command, pid);

	free(cur->command);
	free(cur);

	return (pid == -1 ? -1 : status);
}
