/*	$OpenBSD: setsockopt2.c,v 1.5 2012/02/26 21:08:06 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2009. Public Domain.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
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
check_timeout(int s, int sec, struct timespec *to)
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
sock_connect(void *arg)
{
	struct sockaddr_in sin;
	struct timeval to;
	struct timespec ts;
	pid_t child_pid;
	int status;
	int s, s2, s3;

	CHECKe(clock_getres(CLOCK_MONOTONIC, &mono_res));
	CHECKe(s = socket(AF_INET, SOCK_STREAM, 0));
	CHECKe(s2 = dup(s));
	CHECKe(s3 = fcntl(s, F_DUPFD, s));
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = htons(6544);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECKe(connect(s, (struct sockaddr *)&sin, sizeof(sin)));
	to.tv_sec = 2;
	to.tv_usec = 0.5 * 1e6;
	TIMEVAL_TO_TIMESPEC(&to, &ts);
	CHECKe(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to)));
	CHECKe(child_pid = fork());
	if (child_pid == 0) {
		to.tv_sec = 1;
		to.tv_usec = 0.5 * 1e6;
		TIMEVAL_TO_TIMESPEC(&to, &ts);
		CHECKe(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to)));
		check_timeout(s, 2, &ts);
		check_timeout(s2, 2, &ts);
		check_timeout(s3, 2, &ts);
		return (NULL);
	}
	sleep(2);
	ts.tv_sec = 1;	/* the fd timeout was changed in the child */
	check_timeout(s, 2, &ts);
	check_timeout(s2, 2, &ts);
	check_timeout(s3, 2, &ts);
	CHECKe(s2 = dup(s));
	CHECKe(s3 = fcntl(s, F_DUPFD, s));
	check_timeout(s2, 2, &ts);
	check_timeout(s3, 2, &ts);
	CHECKe(close(s));
	CHECKe(close(s2));
	CHECKe(close(s3));
	ASSERTe(wait(&status), == child_pid);
	ASSERT(WIFEXITED(status));
	CHECKr(WEXITSTATUS(status));
	return (NULL);
}

static void *
sock_accept(void *arg)
{
	pthread_t connect_thread;
	struct sockaddr_in sin;
	int s;

	CHECKe(s = socket(AF_INET, SOCK_STREAM, 0));
	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_port = htons(6544);
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	CHECKe(bind(s, (struct sockaddr *)&sin, sizeof(sin)));
	CHECKe(listen(s, 2));

	CHECKr(pthread_create(&connect_thread, NULL, sock_connect, NULL));
	CHECKr(pthread_join(connect_thread, NULL));
	return (NULL);
}

int
main(int argc, char **argv)
{
	pthread_t accept_thread;

	CHECKr(pthread_create(&accept_thread, NULL, sock_accept, NULL));
	CHECKr(pthread_join(accept_thread, NULL));
	SUCCEED;
}
