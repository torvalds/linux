/*	$OpenBSD: pthread_join.c,v 1.1 2011/10/01 10:26:59 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2011. Public Domain.
 */
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "test.h"

volatile sig_atomic_t hits = 0;

void
handler(int sig)
{
	hits++;
}

void *
thr_sleep(void *arg)
{
	return ((caddr_t)NULL + sleep(5));
}

void *
thr_join(void *arg)
{
	pthread_t tid = *(pthread_t *)arg;
	void *retval;

	CHECKr(pthread_join(tid, &retval));
	return (retval);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;
	pthread_t tid[2];
	void *retval;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = handler;
	CHECKe(sigaction(SIGUSR1, &sa, NULL));

	CHECKr(pthread_create(&tid[0], NULL, thr_sleep, NULL));
	CHECKr(pthread_create(&tid[1], NULL, thr_join, &tid[0]));
	sleep(2);

	CHECKr(pthread_kill(tid[1], SIGUSR1));
	sleep(2);

	ASSERT(hits == 1);
	CHECKr(pthread_join(tid[1], &retval));
	ASSERT(retval == (void *)0);
	SUCCEED;
}
