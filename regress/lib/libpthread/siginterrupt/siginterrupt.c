/*	$OpenBSD: siginterrupt.c,v 1.1 2011/09/13 23:50:17 fgsch Exp $	*/
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
blocker(void *arg)
{
	int fds[2];
	char buf;

	CHECKe(pipe(fds));
	ASSERT(read(fds[0], &buf, 1) == -1);
	return ((caddr_t)NULL + errno);
}

int
main(int argc, char **argv)
{
	pthread_t tid;
	void *retval;

	ASSERT(signal(SIGUSR1, handler) != SIG_ERR);

	CHECKr(pthread_create(&tid, NULL, blocker, NULL));
	sleep(1);

	/* With signal(3) system calls will be restarted. */
	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	/* Same as default with signal(3). */
	CHECKe(siginterrupt(SIGUSR1, 0));
	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	/* Should interrupt the call now. */
	CHECKe(siginterrupt(SIGUSR1, 1));
	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	CHECKr(pthread_join(tid, &retval));
	ASSERT(retval == (void *)EINTR);
	ASSERT(hits == 3);
	SUCCEED;
}
