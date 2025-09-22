/*	$OpenBSD: kqueue-exec.c,v 1.1 2023/08/20 15:19:34 visa Exp $	*/

/*
 * Copyright (c) 2023 Visa Hankala
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

static void	do_exec_child(void);
static void	do_exec_parent(const char *, int);

int
do_exec(const char *argv0)
{
	do_exec_parent(argv0, 0);
	do_exec_parent(argv0, 1);
	return 0;
}

static void
do_exec_parent(const char *argv0, int cloexec)
{
	char *args[] = {
		(char *)argv0,
		"-e",
		NULL
	};
	char fdbuf[12];
	pid_t pid;
	int kq, status;

	if (getenv("REGRESS_KQUEUE_FD") != NULL) {
		do_exec_child();
		_exit(0);
	}

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		kq = kqueue1(cloexec ? O_CLOEXEC : 0);
		if (kq == -1)
			err(1, "kqueue1");
		snprintf(fdbuf, sizeof(fdbuf), "%d", kq);
		if (setenv("REGRESS_KQUEUE_FD", fdbuf, 1) == -1)
			err(1, "setenv");
		if (setenv("REGRESS_KQUEUE_CLOEXEC",
		    cloexec ? "1" : "0", 1) == -1)
			err(1, "setenv 2");
		execv(argv0, args);
		err(1, "execve");
	}
	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (status != 0)
		errx(1, "child failed");
}

static void
do_exec_child(void)
{
	char *arg;
	int cloexec, fd;

	arg = getenv("REGRESS_KQUEUE_FD");
	if (arg == NULL)
		errx(1, "fd arg is missing");
	fd = atoi(arg);

	arg = getenv("REGRESS_KQUEUE_CLOEXEC");
	if (arg != NULL && strcmp(arg, "1") == 0)
		cloexec = 1;
	else
		cloexec = 0;

	if (cloexec) {
		if (kevent(fd, NULL, 0, NULL, 0, 0) == -1) {
			if (errno != EBADF)
				err(1, "child after exec: kevent cloexec");
		} else {
			errx(1, "child after exec: "
			    "kqueue cloexec fd is not closed");
		}
	} else {
		if (kevent(fd, NULL, 0, NULL, 0, 0) == -1) {
			err(1, "child after exec: kevent");
		}
	}
}
