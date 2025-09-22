/*	$OpenBSD: signal.c,v 1.5 2003/07/31 21:48:06 deraadt Exp $	*/
/* David Leonard <d@openbsd.org>, 2001. Public Domain. */

/*
 * This program tests signal handler re-entrancy.
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "test.h"

volatile int alarmed;

static void *
sleeper(void *arg)
{
	sigset_t mask;

	/* Ignore all signals in this thread */
	sigfillset(&mask);
	CHECKe(sigprocmask(SIG_SETMASK, &mask, NULL));
	ASSERT(sleep(3) == 0);
	CHECKe(write(STDOUT_FILENO, "\n", 1));
	SUCCEED;
}

static void
handler(int sig)
{
	int save_errno = errno;

	alarmed = 1;
	alarm(1);
	signal(SIGALRM, handler);
	errno = save_errno;
}

int
main(int argc, char *argv[])
{
	pthread_t slpr;

	ASSERT(signal(SIGALRM, handler) != SIG_ERR);
	CHECKe(alarm(1));
	CHECKr(pthread_create(&slpr, NULL, sleeper, NULL));
	/* ASSERT(sleep(1) == 0); */
	for (;;) {
		if (alarmed) {
			alarmed = 0;
			CHECKe(write(STDOUT_FILENO, "!", 1));
		}
	}
}
