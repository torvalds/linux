/*-
 * Copyright (c) 2005 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#include <sys/types.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stdint.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/*
 * Simple micro-benchmark to see how many connections/second can be created
 * in a serialized fashion against a given server.  A timer signal is used
 * to interrupt the loop and assess the cost.
 */
#define	SECONDS	60
#define	PORT	6060

static int	timer_fired;

/*
 * Signal timer, which both interrupts the in-progress socket operation, and
 * flags the timer as having fired so the main loop can exit.
 */
static void
alarm_handler(__unused int signum)
{

	timer_fired = 1;
}

/*
 * Build a connection.  In order to make sure the signal interrupts us as
 * we wait, use non-blocking sockets and select().  Return 0 on success, or
 * -1 if we were interrupted.  Exit with a failure if we get an unspected
 * error.
 */
static int
try_connect(struct sockaddr_in *sin)
{
	struct 
	fd_set read_set;
	int i, s;

	s = socket(PF_INET, SOCK_STREAM, 0);
	if (s < 0)
		err(-1, "socket(PF_INET, SOCK_STREAM)");

	i = 1;
	if (fcntl(s, F_SETFL, FIONBIO, &i) < 0)
		err(-1, "fcntl(s, FIOBIO, 1)");

	FD_ZERO(&read_set);
	FD_SET(s, &read_set);

	if (connect(s, (struct sockaddr *)sin, sizeof(*sin)) < 0 &&
	    errno != EINPROGRESS)
		err(-1, "connect(%s)", inet_ntoa(sin->sin_addr));

	if (select(s + 1, &read_set, &read_set, &read_set, NULL) < 0) {
		if ((errno == EINTR && !timer_fired) || (errno != EINTR))
			err(-1, "select");
		return (-1);
	}

	close(s);
	return (0);
}


int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	u_int64_t counter;

	if (argc != 2)
		errx(-1, "usage: tcpconnect [ip]");

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_addr.s_addr = inet_addr(argv[1]);
	sin.sin_port = htons(PORT);

	if (signal(SIGALRM, alarm_handler) == SIG_ERR)
		err(-1, "signal(SIGALRM)");

	alarm(SECONDS);

	counter = 0;
	while (!timer_fired) {
		if (try_connect(&sin) == 0)
			counter++;
	}
	printf("%ju count\n", (uintmax_t)counter);
	printf("%ju connections/second\n", (uintmax_t)(counter / SECONDS));

	return (0);
}
