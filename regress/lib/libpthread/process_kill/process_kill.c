/*	$OpenBSD: process_kill.c,v 1.1 2012/03/07 22:01:29 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2012. Public Domain.
 */

#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <unistd.h>
#include "test.h"

void
deadlock(int sig)
{
	PANIC("deadlock detected");
}

void
handler(int sig)
{

}

void *
killer(void *arg)
{
	sleep(2);
	CHECKe(kill(getpid(), SIGUSR1)); 
	return (NULL);
}

int
main(int argc, char **argv)
{
	pthread_t t;

	ASSERT(signal(SIGALRM, deadlock) != SIG_ERR);
	ASSERT(signal(SIGUSR1, handler) != SIG_ERR);
	CHECKr(pthread_create(&t, NULL, killer, NULL));
	alarm(5);
	sleep(15);
	CHECKr(pthread_join(t, NULL));
	SUCCEED;
}
