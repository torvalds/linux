/*	$OpenBSD: kevent.c,v 1.1 2011/09/13 23:50:17 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2011. Public Domain.
 */
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
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
thr_kevent(void *arg)
{
	struct kevent ev;
	struct timespec ts = { 10, 0 };
	int kq;

	CHECKe(kq = kqueue());
	ASSERT(kevent(kq, NULL, 0, &ev, 1, &ts) == -1);
	return ((caddr_t)NULL + errno);
}

int
main(int argc, char **argv)
{
	struct sigaction sa;
	pthread_t tid;
	void *retval;

	bzero(&sa, sizeof(sa));
	sa.sa_handler = handler;
	sa.sa_flags = SA_RESTART;
	CHECKe(sigaction(SIGUSR1, &sa, NULL));

	CHECKr(pthread_create(&tid, NULL, thr_kevent, NULL));
	sleep(2);

	/* Should interrupt it. */
	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	CHECKr(pthread_join(tid, &retval));
	ASSERT(retval == (void *)EINTR);
	ASSERT(hits == 1);
	SUCCEED;
}
