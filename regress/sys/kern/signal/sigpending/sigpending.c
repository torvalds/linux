/*	$OpenBSD: sigpending.c,v 1.1 2020/09/16 14:02:24 mpi Exp $	*/
/*
 * Written by Matthew Dempsky, 2012.
 * Public domain.
 */

#include <assert.h>
#include <signal.h>
#include <stddef.h>

int
main()
{
	sigset_t set;

	assert(sigemptyset(&set) == 0);
	assert(sigaddset(&set, SIGUSR1) == 0);
	assert(sigprocmask(SIG_BLOCK, &set, NULL) == 0);
	assert(raise(SIGUSR1) == 0);
	assert(sigemptyset(&set) == 0);
	assert(sigpending(&set) == 0);
	assert(sigismember(&set, SIGUSR1) == 1);

	return (0);
}
