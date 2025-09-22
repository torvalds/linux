/*	$OpenBSD: pthread_mutex_lock.c,v 1.1 2011/10/01 10:26:59 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2011. Public Domain.
 */
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include "test.h"

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
volatile sig_atomic_t hits = 0;

void
handler(int sig)
{
	hits++;
}

void *
thr_lock(void *arg)
{
	CHECKr(pthread_mutex_lock(&lock));
	return (NULL);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;
	pthread_t tid;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = handler;
	CHECKe(sigaction(SIGUSR1, &sa, NULL));

	CHECKr(pthread_mutex_lock(&lock));
	CHECKr(pthread_create(&tid, NULL, thr_lock, NULL));
	sleep(2);

	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	CHECKr(pthread_mutex_unlock(&lock));
	ASSERT(hits == 1);
	SUCCEED;
}
