/*	$OpenBSD: setsockopt3a.c,v 1.6 2012/02/22 20:33:51 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2009. Public Domain.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <err.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"

/* resolution of the monotonic clock */
struct timespec mono_res;

static void
alarm_handler(int sig)
{
	_exit(NOTOK);
}

void
check_timeout(int s, int sec, const struct timespec *to)
{
	struct timespec t1, t2, e;
	char buf[BUFSIZ];

	ASSERT(signal(SIGALRM, alarm_handler) != SIG_ERR);
	CHECKe(alarm(sec));
	CHECKe(clock_gettime(CLOCK_MONOTONIC, &t1));
	ASSERT(read(s, &buf, sizeof(buf)) == -1);
	CHECKe(clock_gettime(CLOCK_MONOTONIC, &t2));
	ASSERT(errno == EAGAIN);
	timespecsub(&t2, &t1, &e);

	/*
	 * verify that the difference between the duration and the
	 * timeout is less than the resolution of the clock
	 */
	if (timespeccmp(&e, to, <))
		timespecsub(to, &e, &t1);
	else
		timespecsub(&e, to, &t1);
	ASSERT(timespeccmp(&t1, &mono_res, <=));
}

static void *
sock_accept(void *arg)
{
	struct sockaddr_in sin;
	struct timeval to;
	struct timespec ts;
	int s, s2, s3;

	CHECKe(clock_getres(CLOCK_MONOTONIC, &mono_res));
	CHECKe(s = strtol(arg, NULL, 10));
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = htons(6545);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECKe(connect(s, (struct sockaddr *)&sin, sizeof(sin)));
	to.tv_sec = 2;
	to.tv_usec = 0.5 * 1e6;
	TIMEVAL_TO_TIMESPEC(&to, &ts);
	check_timeout(s, 3, &ts);
	CHECKe(s2 = dup(s));
	CHECKe(s3 = fcntl(s, F_DUPFD, s));
	check_timeout(s2, 3, &ts);
	check_timeout(s3, 3, &ts);
	return (NULL);
}

int
main(int argc, char **argv)
{
	pthread_t accept_thread;

	if (argc != 2)
		exit(NOTOK);
	CHECKr(pthread_create(&accept_thread, NULL, sock_accept, argv[1]));
	CHECKr(pthread_join(accept_thread, NULL));
	SUCCEED;
}
