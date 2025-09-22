/*	$OpenBSD: lockspool.c,v 1.23 2023/03/08 04:43:05 guenther Exp $	*/

/*
 * Copyright (c) 1998 Theo de Raadt <deraadt@theos.com>
 * Copyright (c) 1998 Todd C. Miller <millert@openbsd.org>
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
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <signal.h>
#include <pwd.h>
#include <syslog.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <paths.h>
#include <stdlib.h>
#include <poll.h>
#include <err.h>

#include "mail.local.h"

void unhold(int);
void usage(void);

extern char *__progname;

int
main(int argc, char *argv[])
{
	struct passwd *pw;
	struct pollfd pfd;
	ssize_t nread;
	char *from, c;
	int holdfd;

	if (unveil(_PATH_MAILDIR, "rwc") == -1)
		err(1, "unveil %s", _PATH_MAILDIR);
	if (pledge("stdio rpath wpath getpw cpath fattr", NULL) == -1)
		err(1, "pledge");

	openlog(__progname, LOG_PERROR, LOG_MAIL);

	if (argc != 1 && argc != 2)
		usage();
	if (argc == 2 && getuid() != 0)
		merr(1, "you must be root to lock someone else's spool");

	signal(SIGTERM, unhold);
	signal(SIGINT, unhold);
	signal(SIGHUP, unhold);
	signal(SIGPIPE, unhold);

	if (argc == 2)
		pw = getpwnam(argv[1]);
	else
		pw = getpwuid(getuid());
	if (pw == NULL)
		exit (1);
	from = pw->pw_name;

	holdfd = getlock(from, pw);
	if (holdfd == -1) {
		write(STDOUT_FILENO, "0\n", 2);
		exit (1);
	}
	write(STDOUT_FILENO, "1\n", 2);

	/* wait for the other end of the pipe to close, then release the lock */
	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	do {
		if (poll(&pfd, 1, INFTIM) == -1) {
			if (errno != EINTR)
				break;
		}
		do {
			nread = read(STDIN_FILENO, &c, 1);
		} while (nread == 1 || (nread == -1 && errno == EINTR));
	} while (nread == -1 && errno == EAGAIN);
	rellock();
	exit (0);
}

void
unhold(int signo)
{

	rellock();
	_exit(0);
}

void
usage(void)
{

	merr(1, "usage: %s [username]", __progname);
}
