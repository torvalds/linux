/*	$OpenBSD: util.c,v 1.1 2020/09/16 14:02:23 mpi Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
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

#include <sys/socket.h>
#include <sys/wait.h>
#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

static void
signal_handler(int signum)
{
}

void
expect_signal_impl(int signum, const char *signame, const char *file, int line)
{
	sigset_t sigmask;
	struct timespec tmo;
	int ret;

	tmo.tv_sec = 5;
	tmo.tv_nsec = 0;
	sigprocmask(0, NULL, &sigmask);
	sigdelset(&sigmask, signum);
	ret = ppoll(NULL, 0, &tmo, &sigmask);
	if (ret == 0) {
		fprintf(stderr, "%s:%d: signal %s timeout\n",
		    file, line, signame);
		_exit(1);
	}
	if (ret != -1) {
		fprintf(stderr, "%s: poll: unexpected return value: %d\n",
		    __func__, ret);
		_exit(1);
	}
}

void
reject_signal_impl(int signum, const char *signame, const char *file, int line)
{
	sigset_t sigmask;

	if (sigpending(&sigmask) != 0) {
		fprintf(stderr, "%s: sigpending: %s\n", __func__,
		    strerror(errno));
		_exit(1);
	}
	if (sigismember(&sigmask, signum)) {
		fprintf(stderr, "%s:%d: signal %s not expected\n", file, line,
		    signame);
		_exit(1);
	}
}

void
test_init(void)
{
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGIO);
	sigaddset(&mask, SIGURG);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	signal(SIGCHLD, signal_handler);
	signal(SIGIO, signal_handler);
	signal(SIGURG, signal_handler);

	alarm(10);
}

void
test_barrier(int sfd)
{
	char b = 0;

	assert(write(sfd, &b, 1) == 1);
	assert(read(sfd, &b, 1) == 1);
}

int
test_fork(pid_t *ppid, int *psfd)
{
	pid_t pid;
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);

	pid = fork();
	assert(pid != -1);
	*ppid = pid;
	if (pid == 0) {
		close(fds[0]);
		*psfd = fds[1];
		return CHILD;
	} else {
		close(fds[1]);
		*psfd = fds[0];
		return PARENT;
	}
}

int
test_wait(pid_t pid, int sfd)
{
	int status;

	close(sfd);
	if (pid == 0)
		_exit(0);
	assert(waitpid(pid, &status, 0) == pid);
	return 0;
}
