/*-
 * Copyright (c) 2007 Robert N. M. Watson
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
 *
 * $FreeBSD$
 */

/*
 * Sockets serialize I/O in each direction in order to avoid interlacing of
 * I/O by multiple processes or threcvs recving or sending the socket.  This
 * is done using some form of kernel lock (varies by kernel version), called
 * "sblock" in FreeBSD.  However, to avoid unkillable processes waiting on
 * I/O that may be entirely controlled by a remote network endpoint, that
 * lock acquisition must be interruptible.
 *
 * To test this, set up a local domain stream socket pair and a set of three
 * processes.  Two processes block in recv(), the first on sbwait (wait for
 * I/O), and the second on the sblock waiting for the first to finish.  A
 * third process is responsible for signalling the second process, then
 * writing to the socket.  Depending on the error returned in the second
 * process, we can tell whether the sblock wait was interrupted, or if
 * instead the process only woke up when the write was performed.
 */

#include <sys/socket.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int interrupted;
static void
signal_handler(int signum __unused)
{

	interrupted++;
}

/*
 * Process that will perform a blocking recv on a UNIX domain socket.  This
 * should return one byte of data.
 */
static void
blocking_recver(int fd)
{
	ssize_t len;
	char ch;

	len = recv(fd, &ch, sizeof(ch), 0);
	if (len < 0)
		err(-1, "FAIL: blocking_recver: recv");
	if (len == 0)
		errx(-1, "FAIL: blocking_recver: recv: eof");
	if (len != 1)
		errx(-1, "FAIL: blocking_recver: recv: %zd bytes", len);
	if (interrupted)
		errx(-1, "FAIL: blocking_recver: interrupted wrong pid");
}

/*
 * Process that will perform a locking recv on a UNIX domain socket.
 *
 * This is where we figure out if the test worked or not.  If it has failed,
 * then recv() will return EOF, as the close() arrives before the signal,
 * meaning that the wait for the sblock was not interrupted; if it has
 * succeeded, we get EINTR as the signal interrupts the lock request.
 */
static void
locking_recver(int fd)
{
	ssize_t len;
	char ch;

	if (sleep(1) != 0)
		err(-1, "FAIL: locking_recver: sleep");
	len = recv(fd, &ch, sizeof(ch), 0);
	if (len < 0 && errno != EINTR)
		err(-1, "FAIL: locking_recver: recv");
	if (len < 0 && errno == EINTR) {
		fprintf(stderr, "PASS\n");
		exit(0);
	}
	if (len == 0)
		errx(-1, "FAIL: locking_recver: recv: eof");
	if (!interrupted)
		errx(-1, "FAIL: locking_recver: not interrupted");
}

static void
signaller(pid_t locking_recver_pid, int fd)
{
	ssize_t len;
	char ch;

	if (sleep(2) != 0) {
		warn("signaller sleep(2)");
		return;
	}
	if (kill(locking_recver_pid, SIGHUP) < 0) {
		warn("signaller kill(%d)", locking_recver_pid);
		return;
	}
	if (sleep(1) != 0) {
		warn("signaller sleep(1)");
		return;
	}
	len = send(fd, &ch, sizeof(ch), 0);
	if (len < 0) {
		warn("signaller send");
		return;
	}
	if (len != sizeof(ch)) {
		warnx("signaller send ret %zd", len);
		return;
	}
	if (close(fd) < 0) {
		warn("signaller close");
		return;
	}
	if (sleep(1) != 0) {
		warn("signaller sleep(1)");
		return;
	}
}

int
main(void)
{
	int error, fds[2], recver_fd, sender_fd;
	pid_t blocking_recver_pid;
	pid_t locking_recver_pid;
	struct sigaction sa;

	if (sigaction(SIGHUP, NULL, &sa) < 0)
		err(-1, "FAIL: sigaction(SIGHUP, NULL, &sa)");

	sa.sa_handler = signal_handler;
	if (sa.sa_flags & SA_RESTART)
		printf("SIGHUP restartable by default (cleared)\n");
	sa.sa_flags &= ~SA_RESTART;

	if (sigaction(SIGHUP, &sa, NULL) < 0)
		err(-1, "FAIL: sigaction(SIGHUP, &sa, NULL)");

#if 0
	if (signal(SIGHUP, signal_handler) == SIG_ERR)
		err(-1, "FAIL: signal(SIGHUP)");
#endif

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fds) < 0)
		err(-1, "FAIL: socketpair(PF_LOCAL, SOGK_STREAM, 0)");

	sender_fd = fds[0];
	recver_fd = fds[1];

	blocking_recver_pid = fork();
	if (blocking_recver_pid < 0)
		err(-1, "FAIL: fork");
	if (blocking_recver_pid == 0) {
		close(sender_fd);
		blocking_recver(recver_fd);
		exit(0);
	}

	locking_recver_pid = fork();
	if (locking_recver_pid < 0) {
		error = errno;
		kill(blocking_recver_pid, SIGKILL);
		errno = error;
		err(-1, "FAIL: fork");
	}
	if (locking_recver_pid == 0) {
		close(sender_fd);
		locking_recver(recver_fd);
		exit(0);
	}

	signaller(locking_recver_pid, sender_fd);

	kill(blocking_recver_pid, SIGKILL);
	kill(locking_recver_pid, SIGKILL);
	exit(0);
}
