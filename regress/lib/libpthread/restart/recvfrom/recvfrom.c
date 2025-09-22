/*	$OpenBSD: recvfrom.c,v 1.2 2016/01/27 01:20:10 jca Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2011. Public Domain.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
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
thr_recvfrom(void *arg)
{
	struct sockaddr_in sa;
	socklen_t salen;
	char buf;
	int s;

	CHECKe(s = socket(AF_INET, SOCK_DGRAM, 0));
	bzero(&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_port = htons(6543);
	CHECKe(bind(s, (const void*)&sa, sizeof(sa)));
	salen = sizeof(sa);
	ASSERT(recvfrom(s, &buf, 1, 0,(struct sockaddr *)&sa, &salen) == -1);
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
	sa.sa_flags = 0;
	CHECKe(sigaction(SIGUSR2, &sa, NULL));

	CHECKr(pthread_create(&tid, NULL, thr_recvfrom, NULL));
	sleep(2);

	/* Should restart it. */
	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	/* Should interrupt it. */
	CHECKr(pthread_kill(tid, SIGUSR2));
	sleep(1);

	CHECKr(pthread_join(tid, &retval));
	ASSERT(retval == (void *)EINTR);
	ASSERT(hits == 2);
	SUCCEED;
}
