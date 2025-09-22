/*	$OpenBSD: ttylog.c,v 1.8 2021/07/06 11:50:34 bluhm Exp $	*/

/*
 * Copyright (c) 2015 Alexander Bluhm <bluhm@openbsd.org>
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
#include <sys/sockio.h>

#include <errno.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <util.h>
#include <utmp.h>

__dead void usage(void);
void redirect(void);
void restore(void);
void timeout(int);
void terminate(int);
void iostdin(int);

FILE *lg;
char ptyname[16], *console, *username, *logfile, *tty;
int mfd, sfd;

__dead void
usage()
{
	fprintf(stderr, "usage: %s /dev/console|username logfile\n",
	    getprogname());
	exit(2);
}

int
main(int argc, char *argv[])
{
	char buf[8192];
	struct sigaction act;
	sigset_t set;
	ssize_t n;

	if (argc != 3)
		usage();
	if (strcmp(argv[1], "/dev/console") == 0)
		console = argv[1];
	else
		username = argv[1];
	logfile = argv[2];

	sigemptyset(&set);
	sigaddset(&set, SIGTERM);
	sigaddset(&set, SIGIO);
	if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
		err(1, "sigprocmask block init");

	if ((lg = fopen(logfile, "w")) == NULL)
		err(1, "fopen %s", logfile);
	if (setvbuf(lg, NULL, _IOLBF, 0) != 0)
		err(1, "setlinebuf");

	memset(&act, 0, sizeof(act));
	act.sa_mask = set;
	act.sa_flags = SA_RESTART;
	act.sa_handler = terminate;
	if (sigaction(SIGTERM, &act, NULL) == -1)
		err(1, "sigaction SIGTERM");
	if (sigaction(SIGINT, &act, NULL) == -1)
		err(1, "sigaction SIGINT");

	if (openpty(&mfd, &sfd, ptyname, NULL, NULL) == -1)
		err(1, "openpty");
	fprintf(lg, "openpty %s\n", ptyname);
	if ((tty = strrchr(ptyname, '/')) == NULL)
		errx(1, "tty: %s", ptyname);
	tty++;

	/* login(3) searches for a controlling tty, use the created one */
	if (dup2(sfd, 1) == -1)
		err(1, "dup2 stdout");

	redirect();

	act.sa_handler = iostdin;
	if (sigaction(SIGIO, &act, NULL) == -1)
		err(1, "sigaction SIGIO");
	if (setpgid(0, 0) == -1)
		err(1, "setpgid");
	if (fcntl(0, F_SETOWN, getpid()) == -1)
		err(1, "fcntl F_SETOWN");
	if (fcntl(0, F_SETFL, O_ASYNC) == -1)
		err(1, "fcntl O_ASYNC");

	act.sa_handler = timeout;
	if (sigaction(SIGALRM, &act, NULL) == -1)
		err(1, "sigaction SIGALRM");
	alarm(30);

	fprintf(lg, "%s: started\n", getprogname());

	if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1)
		err(1, "sigprocmask unblock init");

	/* do not block signals during read, it has to be interrupted */
	while ((n = read(mfd, buf, sizeof(buf))) > 0) {
		if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
			err(1, "sigprocmask block write");
		fprintf(lg, ">>> ");
		if (fwrite(buf, 1, n, lg) != (size_t)n)
			err(1, "fwrite %s", logfile);
		if (buf[n-1] != '\n')
			fprintf(lg, "\n");
		if (sigprocmask(SIG_UNBLOCK, &set, NULL) == -1)
			err(1, "sigprocmask unblock write");
	}
	if (sigprocmask(SIG_BLOCK, &set, NULL) == -1)
		err(1, "sigprocmask block exit");
	if (n < 0)
		err(1, "read %s", ptyname);
	fprintf(lg, "EOF %s\n", ptyname);

	restore();

	errx(3, "EOF");
}

void
redirect(void)
{
	struct utmp utmp;
	int fd, on;

	if (console) {
		/* first remove any existing console redirection */
		on = 0;
		if ((fd = open("/dev/console", O_WRONLY)) == -1)
			err(1, "open /dev/console");
		if (ioctl(fd, TIOCCONS, &on) == -1)
			err(1, "ioctl TIOCCONS");
		close(fd);
		/* then redirect console to our pseudo tty */
		on = 1;
		if (ioctl(sfd, TIOCCONS, &on) == -1)
			err(1, "ioctl TIOCCONS on");
		fprintf(lg, "console %s on %s\n", console, tty);
	}
	if (username) {
		memset(&utmp, 0, sizeof(utmp));
		strlcpy(utmp.ut_line, tty, sizeof(utmp.ut_line));
		strlcpy(utmp.ut_name, username, sizeof(utmp.ut_name));
		time(&utmp.ut_time);
		login(&utmp);
		fprintf(lg, "login %s %s\n", username, tty);
	}
}

void
restore(void)
{
	int on;

	if (tty == NULL)
		return;
	if (console) {
		on = 0;
		if (ioctl(sfd, TIOCCONS, &on) == -1)
			err(1, "ioctl TIOCCONS off");
		fprintf(lg, "console %s off\n", tty);
	}
	if (username) {
		if (logout(tty) == 0)
			errx(1, "logout %s", tty);
		fprintf(lg, "logout %s\n", tty);
	}
}

void
timeout(int sig)
{
	fprintf(lg, "signal timeout %d\n", sig);
	restore();
	errx(3, "timeout");
}

void
terminate(int sig)
{
	fprintf(lg, "signal terminate %d\n", sig);
	restore();
	errx(3, "terminate");
}

void
iostdin(int sig)
{
	char buf[8192];
	ssize_t n;

	fprintf(lg, "signal iostdin %d\n", sig);

	/* try to read as many log messages as possible before terminating */
	if (fcntl(mfd, F_SETFL, O_NONBLOCK) == -1)
		err(1, "fcntl O_NONBLOCK");
	while ((n = read(mfd, buf, sizeof(buf))) > 0) {
		fprintf(lg, ">>> ");
		if (fwrite(buf, 1, n, lg) != (size_t)n)
			err(1, "fwrite %s", logfile);
		if (buf[n-1] != '\n')
			fprintf(lg, "\n");
	}
	if (n < 0 && errno != EAGAIN)
		err(1, "read %s", ptyname);

	if ((n = read(0, buf, sizeof(buf))) < 0)
		err(1, "read stdin");
	restore();
	if (n > 0)
		errx(3, "read stdin %zd bytes", n);
	exit(0);
}
