/*-
 * Copyright (c) 2007-2009 Dag-Erling Coïdan Smørgrav
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

#include <sys/param.h>
#include <sys/wait.h>
#include <sys/event.h>

#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libutil.h>

/*
 * We need a signal handler so kill(2) will interrupt the child
 * instead of killing it.
 */
static void
signal_handler(int sig)
{
	(void)sig;
}

/*
 * Test that pidfile_open() can create a pidfile and that pidfile_write()
 * can write to it.
 */
static const char *
test_pidfile_uncontested(void)
{
	const char *fn = "test_pidfile_uncontested";
	struct pidfh *pf;
	pid_t other = 0;

	unlink(fn);
	pf = pidfile_open(fn, 0600, &other);
	if (pf == NULL && other != 0)
		return ("pidfile exists and is locked");
	if (pf == NULL)
		return (strerror(errno));
	if (pidfile_write(pf) != 0) {
		pidfile_close(pf);
		unlink(fn);
		return ("failed to write PID");
	}
	pidfile_close(pf);
	unlink(fn);
	return (NULL);
}

/*
 * Test that pidfile_open() locks against self.
 */
static const char *
test_pidfile_self(void)
{
	const char *fn = "test_pidfile_self";
	struct pidfh *pf1, *pf2;
	pid_t other = 0;
	int serrno;

	unlink(fn);
	pf1 = pidfile_open(fn, 0600, &other);
	if (pf1 == NULL && other != 0)
		return ("pidfile exists and is locked");
	if (pf1 == NULL)
		return (strerror(errno));
	if (pidfile_write(pf1) != 0) {
		serrno = errno;
		pidfile_close(pf1);
		unlink(fn);
		return (strerror(serrno));
	}
	// second open should fail
	pf2 = pidfile_open(fn, 0600, &other);
	if (pf2 != NULL) {
		pidfile_close(pf1);
		pidfile_close(pf2);
		unlink(fn);
		return ("managed to opened pidfile twice");
	}
	if (other != getpid()) {
		pidfile_close(pf1);
		unlink(fn);
		return ("pidfile contained wrong PID");
	}
	pidfile_close(pf1);
	unlink(fn);
	return (NULL);
}

/*
 * Common code for test_pidfile_{contested,inherited}.
 */
static const char *
common_test_pidfile_child(const char *fn, int parent_open)
{
	struct pidfh *pf = NULL;
	pid_t other = 0, pid = 0;
	int fd[2], serrno, status;
	struct kevent event, ke;
	char ch;
	int kq;

	unlink(fn);
	if (pipe(fd) != 0)
		return (strerror(errno));

	if (parent_open) {
		pf = pidfile_open(fn, 0600, &other);
		if (pf == NULL && other != 0)
			return ("pidfile exists and is locked");
		if (pf == NULL)
			return (strerror(errno));
	}

	pid = fork();
	if (pid == -1)
		return (strerror(errno));
	if (pid == 0) {
		// child
		close(fd[0]);
		signal(SIGINT, signal_handler);
		if (!parent_open) {
			pf = pidfile_open(fn, 0600, &other);
			if (pf == NULL && other != 0)
				return ("pidfile exists and is locked");
			if (pf == NULL)
				return (strerror(errno));
		}
		if (pidfile_write(pf) != 0) {
			serrno = errno;
			pidfile_close(pf);
			unlink(fn);
			return (strerror(serrno));
		}
		if (pf == NULL)
			_exit(1);
		if (pidfile_write(pf) != 0)
			_exit(2);
		kq = kqueue();
		if (kq == -1)
			_exit(3);
		EV_SET(&ke, SIGINT, EVFILT_SIGNAL, EV_ADD, 0, 0, NULL);
		/* Attach event to the kqueue. */
		if (kevent(kq, &ke, 1, NULL, 0, NULL) != 0)
			_exit(4);
		/* Inform the parent we are ready to receive SIGINT */
		if (write(fd[1], "*", 1) != 1)
			_exit(5);
		/* Wait for SIGINT received */
		if (kevent(kq, NULL, 0, &event, 1, NULL) != 1)
			_exit(6);
		_exit(0);
	}
	// parent
	close(fd[1]);
	if (pf)
		pidfile_close(pf);

	// wait for the child to signal us
	if (read(fd[0], &ch, 1) != 1) {
		serrno = errno;
		unlink(fn);
		kill(pid, SIGTERM);
		errno = serrno;
		return (strerror(errno));
	}

	// We shouldn't be able to lock the same pidfile as our child
	pf = pidfile_open(fn, 0600, &other);
	if (pf != NULL) {
		pidfile_close(pf);
		unlink(fn);
		return ("managed to lock contested pidfile");
	}

	// Failed to lock, but not because it was contested
	if (other == 0) {
		unlink(fn);
		return (strerror(errno));
	}

	// Locked by the wrong process
	if (other != pid) {
		unlink(fn);
		return ("pidfile contained wrong PID");
	}

	// check our child's fate
	if (pf)
		pidfile_close(pf);
	unlink(fn);
	if (kill(pid, SIGINT) != 0)
		return (strerror(errno));
	if (waitpid(pid, &status, 0) == -1)
		return (strerror(errno));
	if (WIFSIGNALED(status))
		return ("child caught signal");
	if (WEXITSTATUS(status) != 0) 
		return ("child returned non-zero status");

	// success
	return (NULL);
}

/*
 * Test that pidfile_open() fails when attempting to open a pidfile that
 * is already locked, and that it returns the correct PID.
 */
static const char *
test_pidfile_contested(void)
{
	const char *fn = "test_pidfile_contested";
	const char *result;

	result = common_test_pidfile_child(fn, 0);
	return (result);
}

/*
 * Test that the pidfile lock is inherited.
 */
static const char *
test_pidfile_inherited(void)
{
	const char *fn = "test_pidfile_inherited";
	const char *result;

	result = common_test_pidfile_child(fn, 1);
	return (result);
}

static struct test {
	const char *name;
	const char *(*func)(void);
} t[] = {
	{ "pidfile_uncontested", test_pidfile_uncontested },
	{ "pidfile_self", test_pidfile_self },
	{ "pidfile_contested", test_pidfile_contested },
	{ "pidfile_inherited", test_pidfile_inherited },
};

int
main(void)
{
	const char *result;
	int i, nt;

	nt = sizeof(t) / sizeof(*t);
	printf("1..%d\n", nt);
	for (i = 0; i < nt; ++i) {
		if ((result = t[i].func()) != NULL)
			printf("not ok %d - %s # %s\n", i + 1,
			    t[i].name, result);
		else
			printf("ok %d - %s\n", i + 1,
			    t[i].name);
	}
	exit(0);
}
