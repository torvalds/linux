/*	$OpenBSD: sigio.c,v 1.5 2020/01/08 16:27:40 visa Exp $	*/

/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
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

#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/wait.h>

#include <dev/wscons/wsconsio.h>

#include <err.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

static int test_getown_fcntl(int);
static int test_getown_ioctl(int);
static int test_gpgrp(int);
static int test_setown_fcntl(int);
static int test_setown_ioctl(int);
static int test_sigio(int);
static int test_spgrp(int);

static int test_common_getown(int, int);
static int test_common_setown(int, int);

static void sigio(int);
static void syncrecv(int, int);
static void syncsend(int, int);

static volatile sig_atomic_t nsigio;

static int
test_getown_fcntl(int fd)
{
	return test_common_getown(fd, 1);
}

static int
test_getown_ioctl(int fd)
{
	return test_common_getown(fd, 0);
}

static int
test_gpgrp(int fd)
{
	int arg, pgrp;

	if (ioctl(fd, TIOCGPGRP, &pgrp) == -1)
		err(1, "ioctl: TIOCGPGRP");
	if (pgrp != 0)
		errx(1, "ioctl: TIOCGPGRP: expected 0, got %d", pgrp);

	arg = getpgrp();
	if (ioctl(fd, TIOCSPGRP, &arg) == -1)
		err(1, "ioctl: TIOCSPGRP");
	if (ioctl(fd, TIOCGPGRP, &pgrp) == -1)
		err(1, "ioctl: TIOCGPGRP");
	if (pgrp != getpgrp())
		errx(1, "ioctl: TIOCGPGRP: expected %d, got %d", getpgrp(), pgrp);

	return 0;
}

static int
test_setown_fcntl(int fd)
{
	return test_common_setown(fd, 1);
}

static int
test_setown_ioctl(int fd)
{
	return test_common_setown(fd, 0);
}

static int
test_sigio(int fd)
{
	struct wscons_event ev;
	int cfd[2], pfd[2];
	ssize_t n;
	pid_t pid;
	int arg, len, status;

	if (pipe(cfd) == -1)
		err(1, "pipe");
	if (pipe(pfd) == -1)
		err(1, "pipe");

	arg = getpid();
	if (ioctl(fd, FIOSETOWN, &arg) == -1)
		err(1, "ioctl: FIOSETOWN");

	/* Enable async IO. */
	arg = 1;
	if (ioctl(fd, FIOASYNC, &arg) == -1)
		err(1, "ioctl: FIOASYNC");

	pid = fork();
	if (pid == -1)
		err(1, "fork");
	if (pid == 0) {
		close(cfd[1]);
		close(pfd[0]);

		syncsend(pfd[1], 1);
		syncrecv(cfd[0], 2);

		memset(&ev, 0, sizeof(ev));
		if (ioctl(fd, WSMUXIO_INJECTEVENT, &ev) == -1)
			err(1, "ioctl: WSMUXIO_INJECTEVENT");

		close(cfd[0]);
		close(pfd[1]);
		_exit(0);
	}
	close(cfd[0]);
	close(pfd[1]);

	syncrecv(pfd[0], 1);

	if (signal(SIGIO, sigio) == SIG_ERR)
		err(1, "signal");

	syncsend(cfd[1], 2);

	if (waitpid(pid, &status, 0) == -1)
		err(1, "waitpid");
	if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
		errx(1, "child exited %d", WEXITSTATUS(status));
	if (WIFSIGNALED(status))
                errx(1, "child killed by signal %d", WTERMSIG(status));

	if (nsigio != 1)
                errx(1, "expected SIGIO to be received once, got %d", nsigio);

	len = sizeof(ev);
	n = read(fd, &ev, len);
	if (n == -1)
		err(1, "read");
	if (n != len)
		errx(1, "read: expected %d bytes, got %ld", len, n);

	/* Disable async IO. */
	arg = 0;
	if (ioctl(fd, FIOASYNC, &arg) == -1)
		err(1, "ioctl: FIOASYNC");

	return 0;
}

static int
test_spgrp(int fd)
{
	int arg;

	/* The process group must be able to receive SIGIO. */
	arg = getpgrp();
	if (ioctl(fd, TIOCSPGRP, &arg) == -1)
		errx(1, "ioctl: TIOCSPGRP");

	/* Bogus process groups must be rejected. */
	arg = -getpgrp();
	if (ioctl(fd, TIOCSPGRP, &arg) != -1)
		errx(1, "ioctl: TIOCSPGRP: %d accepted", arg);
	arg = 1000000;
	if (ioctl(fd, TIOCSPGRP, &arg) != -1)
		errx(1, "ioctl: TIOCSPGRP: %d accepted", arg);

	return 0;
}

static int
test_common_getown(int fd, int dofcntl)
{
	int arg, pgrp;

	if (dofcntl) {
		pgrp = fcntl(fd, F_GETOWN);
		if (pgrp == -1)
			err(1, "fcntl: F_GETOWN");
		if (pgrp != 0)
			errx(1, "fcntl: F_GETOWN: expected 0, got %d", pgrp);
	} else {
		if (ioctl(fd, FIOGETOWN, &pgrp) == -1)
			err(1, "ioctl: FIOGETOWN");
		if (pgrp != 0)
			errx(1, "ioctl: FIOGETOWN: expected 0, got %d", pgrp);
	}

	arg = -getpgrp();
	if (ioctl(fd, FIOSETOWN, &arg) == -1)
		err(1, "ioctl: FIOSETOWN");
	if (dofcntl) {
		pgrp = fcntl(fd, F_GETOWN);
		if (pgrp == -1)
			err(1, "fcntl: F_GETOWN");
		if (pgrp != -getpgrp())
			errx(1, "fcntl: F_GETOWN: expected %d, got %d",
			    -getpgrp(), pgrp);
	} else {
		if (ioctl(fd, FIOGETOWN, &pgrp) == -1)
			err(1, "ioctl: FIOGETOWN");
		if (pgrp != -getpgrp())
			errx(1, "ioctl: FIOGETOWN: expected %d, got %d",
			    -getpgrp(), pgrp);
	}

	return 0;
}

static int
test_common_setown(int fd, int dofcntl)
{
	int arg;

        /* The process must be able to receive SIGIO. */
	arg = getpid();
	if (dofcntl) {
		if (fcntl(fd, F_SETOWN, arg) == -1)
			errx(1, "fcntl: F_SETOWN: process rejected");
	} else {
		if (ioctl(fd, FIOSETOWN, &arg) == -1)
			errx(1, "ioctl: FIOSETOWN: process rejected");
	}

	/* The process group must be able to receive SIGIO. */
	arg = -getpgrp();
	if (dofcntl) {
		if (fcntl(fd, F_SETOWN, arg) == -1)
			errx(1, "fcntl: F_SETOWN: process group rejected");
	} else {
		if (ioctl(fd, FIOSETOWN, &arg) == -1)
			errx(1, "ioctl: FIOSETOWN: process group rejected");
	}

	/* A bogus process must be rejected. */
	arg = 1000000;
	if (dofcntl) {
		if (fcntl(fd, F_SETOWN, arg) != -1)
			errx(1, "fcntl: F_SETOWN: bogus process accepted");
	} else {
		if (ioctl(fd, FIOSETOWN, &arg) != -1)
			errx(1, "ioctl: FIOSETOWN: bogus process accepted");
	}

	/* A bogus process group must be rejected. */
	arg = -1000000;
	if (dofcntl) {
		if (fcntl(fd, F_SETOWN, arg) != -1)
			errx(1, "fcntl: F_SETOWN: bogus process group accepted");
	} else {
		if (ioctl(fd, FIOSETOWN, &arg) != -1)
			errx(1, "ioctl: FIOSETOWN: bogus process group accepted");
	}

	return 0;
}

static void
sigio(int signo)
{
	nsigio++;
}

static void
syncrecv(int fd, int id)
{
	int r;

	if (read(fd, &r, sizeof(r)) == -1)
		err(1, "%s: read", __func__);
	if (r != id)
		errx(1, "%s: expected %d, got %d", __func__, id, r);
}

static void
syncsend(int fd, int id)
{
	if (write(fd, &id, sizeof(id)) == -1)
		err(1, "%s: write", __func__);
}

int
main(int argc, char *argv[])
{
	struct test tests[] = {
		{ "getown-fcntl",	test_getown_fcntl },
		{ "getown-ioctl",	test_getown_ioctl },
		{ "gpgrp",		test_gpgrp },
		{ "setown-fcntl",	test_setown_fcntl },
		{ "setown-ioctl",	test_setown_ioctl },
		{ "sigio",		test_sigio },
		{ "spgrp",		test_spgrp },
		{ NULL,			NULL },
	};

	return dotest(argc, argv, tests);
}
