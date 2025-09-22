/*	$OpenBSD: rcvtimeo.c,v 1.6 2021/12/13 16:56:50 deraadt Exp $	*/

/*	Written by Michael Shalayeff, 2002, Public Domain */

#include <sys/socket.h>

#include <netinet/in.h>

#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <err.h>

volatile int back;

static void
sigalarm(int sig, siginfo_t *sip, void *scp)
{
	if (!back)
		_exit(1);
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	struct sigaction sa;
	struct timeval tv;
	u_char buf[16];
	int s;

	sa.sa_sigaction = &sigalarm;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGALRM, &sa, NULL);

	if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		err(1, "socket");

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(30000);	/* XXX assuming nothing is there */
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(s, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "bind");

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		err(1, "setsockopt1");

	back = 0;
	alarm(2);
	errno = 0;
	if (recv(s, buf, sizeof(buf), 0) < 0 && errno != EAGAIN)
		err(1, "recv1");
	back = 1;

	tv.tv_sec = 0;
	tv.tv_usec = 1;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
		err(1, "setsockopt2");

	back = 0;
	alarm(2);
	errno = 0;
	if (recv(s, buf, sizeof(buf), 0) < 0 && errno != EAGAIN)
		err(1, "recv2");
	back = 1;

	exit (0);
}
