/* $OpenBSD: signodefer.c,v 1.3 2003/07/31 21:48:06 deraadt Exp $ */
/* PUBLIC DOMAIN Oct 2002 <marc@snafu.org> */

/*
 * test signal delivery of active signals (SA_NODEFER)
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "test.h"

volatile sig_atomic_t sigactive;
volatile sig_atomic_t sigcount;
volatile sig_atomic_t was_active;

static void
act_handler(int signal, siginfo_t *siginfo, void *context)
{
	char *str;

	/* how many times has the handler been called */
	was_active += sigactive++;
	sigcount += 1;

	/* verify siginfo since we asked for it. */
	ASSERT(siginfo != NULL);

	asprintf(&str,
		 "%sact_handler/%d, signal %d, siginfo %p, context %p\n",
		 was_active ? "[recurse] " : "",
		 sigcount, signal, siginfo, context);
	CHECKe(write(STDOUT_FILENO, str, strlen(str)));
	/* Odd times entered send ourself the same signal */
	if (sigcount & 1)
		CHECKe(kill(getpid(), SIGUSR1));

	sigactive = 0;
}
 
int
main(int argc, char **argv)
{
	struct sigaction act;

	act.sa_sigaction = act_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	ASSERT(sigaction(SIGUSR1, &act, NULL) == 0);

	/* see if the signal handler recurses */
	CHECKe(kill(getpid(), SIGUSR1));
	sleep(1);
        ASSERT(was_active == 0);
	
	/* allow recursive handlers, see that it is handled right */
	act.sa_flags |= SA_NODEFER;
	ASSERT(sigaction(SIGUSR1, &act, NULL) == 0);

	/* see if the signal handler recurses */
	CHECKe(kill(getpid(), SIGUSR1));
	sleep(1);
	ASSERT(was_active == 1);
	
	SUCCEED;
}
