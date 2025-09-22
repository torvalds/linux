/*	$OpenBSD: kqueue-signal.c,v 1.3 2016/09/20 23:05:27 bluhm Exp $	*/
/*
 *	Written by Philip Guenther <guenther@openbsd.org> 2011 Public Domain
 */

#include <sys/types.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "main.h"

volatile sig_atomic_t saw_usr1 = 0;
volatile sig_atomic_t result = 0;
int kq;

int
sigtest(int signum, int catch)
{
	struct kevent ke;
	struct timespec ts;

	ts.tv_sec = 10;
	ts.tv_nsec = 0;

	ASS(kevent(kq, NULL, 0, &ke, 1, &ts) == 1,
	    warn("can't fetch event on kqueue"));
	ASSX(ke.filter == EVFILT_SIGNAL);
	ASSX(ke.ident == signum);
	ASSX(ke.data == catch);
	return (0);
}

void
usr1handler(int signum)
{
	saw_usr1 = 1;
	result = sigtest(SIGUSR1, 1);
}

int
do_signal(void)
{
	struct kevent ke;
	pid_t pid = getpid();
	sigset_t mask;

	ASS((kq = kqueue()) >= 0, warn("kqueue"));

	signal(SIGUSR1, usr1handler);
	signal(SIGUSR2, SIG_IGN);

	EV_SET(&ke, SIGUSR1, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, NULL);
	ASS(kevent(kq, &ke, 1, NULL, 0, NULL) == 0,
	    warn("can't register events on kqueue"));
	EV_SET(&ke, SIGUSR2, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, NULL);
	ASS(kevent(kq, &ke, 1, NULL, 0, NULL) == 0,
	    warn("can't register events on kqueue"));

	EV_SET(&ke, 10000, EVFILT_SIGNAL, EV_ADD|EV_ENABLE, 0, 0, NULL);
	ASS(kevent(kq, &ke, 1, NULL, 0, NULL) != 0,
	    warnx("registered bogus signal on kqueue"));
	ASS(errno == EINVAL,
	    warn("registering bogus signal on kqueue returned wrong error"));

	ASSX(saw_usr1 == 0);
	kill(pid, SIGUSR1);
	ASSX(saw_usr1 == 1);

	kill(pid, SIGUSR2);
	ASSX(sigtest(SIGUSR2, 1) == 0);
	kill(pid, SIGUSR2);
	kill(pid, SIGUSR2);
	ASSX(sigtest(SIGUSR2, 2) == 0);

	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGUSR2);
	sigprocmask(SIG_BLOCK, &mask, NULL);

	signal(SIGUSR1, SIG_DFL);
	kill(pid, SIGUSR1);
	kill(pid, SIGUSR2);

	close(kq);

	return (0);
}
