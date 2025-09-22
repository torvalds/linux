/*	$OpenBSD: connect.c,v 1.3 2017/05/27 16:04:38 bluhm Exp $	*/
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
thr_connect(void *arg)
{
	struct sockaddr_in sa;
	socklen_t len;
	int l, s;

	/* Create a bound TCP socket without listen on loopback. */
	CHECKe(l = socket(AF_INET, SOCK_STREAM, 0));
	bzero(&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECKe(bind(l, (struct sockaddr *)&sa, sizeof(sa)));
	len = sizeof(sa);
	CHECKe(getsockname(l, (struct sockaddr *)&sa, &len));

	/* Connect to the non listen socket will not reply to SYN. */
	CHECKe(s = socket(AF_INET, SOCK_STREAM, 0));
	ASSERT(connect(s, (struct sockaddr *)&sa, sizeof(sa)) == -1);
	int err = errno;
	ASSERT(connect(s, (struct sockaddr *)&sa, sizeof(sa)) == -1);
	ASSERT(errno == EALREADY);
	return ((caddr_t)NULL + err);
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

	CHECKr(pthread_create(&tid, NULL, thr_connect, NULL));
	sleep(2);

	/* Should interrupt it. */
	CHECKr(pthread_kill(tid, SIGUSR1));
	sleep(1);

	CHECKr(pthread_join(tid, &retval));
	ASSERT(retval == (void *)EINTR);
	ASSERT(hits == 1);
	SUCCEED;
}
