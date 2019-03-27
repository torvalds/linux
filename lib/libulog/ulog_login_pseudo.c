/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2009 Ed Schouten <ed@FreeBSD.org>
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/wait.h>

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include "ulog.h"

#define	_PATH_ULOG_HELPER	"/usr/libexec/ulog-helper"

/*
 * Registering login sessions.
 */

static void
ulog_exec_helper(int fd, char const * const argv[])
{
	sigset_t oblock, nblock;
	pid_t pid, wpid;
	int status;

	/* Block SIGCHLD. */
	sigemptyset(&nblock);
	sigaddset(&nblock, SIGCHLD);
	sigprocmask(SIG_BLOCK, &nblock, &oblock);

	switch (pid = fork()) {
	case -1:
		break;
	case 0:
		/* Execute helper program. */
		if (dup2(fd, STDIN_FILENO) == -1)
			exit(EX_UNAVAILABLE);
		sigprocmask(SIG_SETMASK, &oblock, NULL);
		execv(_PATH_ULOG_HELPER, __DECONST(char * const *, argv));
		exit(EX_UNAVAILABLE);
	default:
		/* Wait for helper to finish. */
		do {
			wpid = waitpid(pid, &status, 0);
		} while (wpid == -1 && errno == EINTR);
		break;
	}

	sigprocmask(SIG_SETMASK, &oblock, NULL);
}

void
ulog_login_pseudo(int fd, const char *host)
{
	char const * const args[4] = { "ulog-helper", "login", host, NULL };

	ulog_exec_helper(fd, args);
}

void
ulog_logout_pseudo(int fd)
{
	char const * const args[3] = { "ulog-helper", "logout", NULL };

	ulog_exec_helper(fd, args);
}
