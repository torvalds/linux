/* $OpenBSD: siginfo.c,v 1.12 2024/08/29 15:16:43 claudio Exp $ */
/* PUBLIC DOMAIN Oct 2002 <marc@snafu.org> */

/*
 * test SA_SIGINFO support.   Also check that SA_RESETHAND does the right
 * thing.
 */

#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "test.h"

#define BOGUS	(char *)0x987230

static void
act_handler(int signal, siginfo_t *siginfo, void *context)
{
	struct sigaction sa;

	CHECKe(sigaction(SIGSEGV, NULL, &sa));
	ASSERT(sa.sa_handler == SIG_DFL);
	ASSERT(siginfo != NULL);
	dprintf(STDOUT_FILENO, "act_handler: signal %d, siginfo %p, "
		 "context %p\naddr %p, code %d, trap %d\n", signal, siginfo,
		 context, siginfo->si_addr, siginfo->si_code,
		 siginfo->si_trapno);
 	ASSERT(siginfo->si_addr == BOGUS);
	ASSERT(siginfo->si_code == SEGV_MAPERR ||
	       siginfo->si_code == SEGV_ACCERR);
	SUCCEED;
}
 
int
main(int argc, char **argv)
{
	struct sigaction act;

	act.sa_sigaction = act_handler;
	sigemptyset(&act.sa_mask);
	act.sa_flags = SA_SIGINFO | SA_RESETHAND | SA_NODEFER;
	CHECKe(sigaction(SIGSEGV, &act, NULL));
	*BOGUS = 1;
	PANIC("How did we get here?");
}
