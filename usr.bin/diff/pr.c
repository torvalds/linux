/*-
 * Copyright (c) 2017 Baptiste Daroussin <bapt@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/procdesc.h>
#include <sys/wait.h>

#include <err.h>
#include <paths.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "pr.h"
#include "diff.h"
#include "xmalloc.h"

#define _PATH_PR "/usr/bin/pr"

struct pr *
start_pr(char *file1, char *file2)
{
	int pfd[2];
	int pr_pd;
	pid_t pid;
	char *header;
	struct pr *pr;

	pr = xcalloc(1, sizeof(*pr));

	xasprintf(&header, "%s %s %s", diffargs, file1, file2);
	signal(SIGPIPE, SIG_IGN);
	fflush(stdout);
	rewind(stdout);
	if (pipe(pfd) == -1)
		err(2, "pipe");
	switch ((pid = pdfork(&pr_pd, PD_CLOEXEC))) {
	case -1:
		status |= 2;
		free(header);
		err(2, "No more processes");
	case 0:
		/* child */
		if (pfd[0] != STDIN_FILENO) {
			dup2(pfd[0], STDIN_FILENO);
			close(pfd[0]);
		}
		close(pfd[1]);
		execl(_PATH_PR, _PATH_PR, "-h", header, (char *)0);
		_exit(127);
	default:

		/* parent */
		if (pfd[1] != STDOUT_FILENO) {
			pr->ostdout = dup(STDOUT_FILENO);
			dup2(pfd[1], STDOUT_FILENO);
			close(pfd[1]);
			close(pfd[1]);
			}
			close(pfd[0]);
			rewind(stdout);
			free(header);
			pr->kq = kqueue();
			if (pr->kq == -1)
				err(2, "kqueue");
			pr->e = xmalloc(sizeof(struct kevent));
			EV_SET(pr->e, pr_pd, EVFILT_PROCDESC, EV_ADD, NOTE_EXIT, 0,
			    NULL);
			if (kevent(pr->kq, pr->e, 1, NULL, 0, NULL) == -1)
				err(2, "kevent");
	}
	return (pr);
}

/* close the pipe to pr and restore stdout */
void
stop_pr(struct pr *pr)
{
	int wstatus;

	if (pr == NULL)
		return;

	fflush(stdout);
	if (pr->ostdout != STDOUT_FILENO) {
		close(STDOUT_FILENO);
		dup2(pr->ostdout, STDOUT_FILENO);
		close(pr->ostdout);
	}
	if (kevent(pr->kq, NULL, 0, pr->e, 1, NULL) == -1)
		err(2, "kevent");
	wstatus = pr->e[0].data;
	close(pr->kq);
	free(pr);
	if (WIFEXITED(wstatus) && WEXITSTATUS(wstatus) != 0)
		errx(2, "pr exited abnormally");
	else if (WIFSIGNALED(wstatus))
		errx(2, "pr killed by signal %d",
		    WTERMSIG(wstatus));
}
