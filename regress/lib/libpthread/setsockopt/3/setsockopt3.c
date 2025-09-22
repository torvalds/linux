/*	$OpenBSD: setsockopt3.c,v 1.4 2012/02/22 20:33:51 fgsch Exp $	*/
/*
 * Federico G. Schwindt <fgsch@openbsd.org>, 2009. Public Domain.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <err.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"

static void *
sock_connect(void *arg)
{
	struct timeval to;
	pid_t child_pid;
	int status;
	int s;

	CHECKe(s = socket(AF_INET, SOCK_STREAM, 0));
	to.tv_sec = 2;
	to.tv_usec = 0.5 * 1e6;
	CHECKe(setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to)));
	CHECKe(child_pid = fork());
	if (child_pid == 0) {
		char *argv[3];
		char fdstr[3];
		snprintf(fdstr, sizeof(fdstr), "%d", s);
		argv[0] = "setsockopt3a";
		argv[1] = fdstr;
		argv[2] = NULL;
		execv(argv[0], argv);
		_exit(NOTOK);
	}
	ASSERTe(wait(&status), == child_pid);
	ASSERT(WIFEXITED(status));
	ASSERT(WEXITSTATUS(status) == 0);
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
	sin.sin_port = htons(6545);
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
