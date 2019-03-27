/*
Copyright 2004 Michiel Boland.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

1. Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

$FreeBSD$

*/

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The following code sets up two connected TCP sockets that send data to each
 * other until the window is closed. Then one of the sockets is closed, which
 * will generate a RST once the TCP at the other socket does a window probe.
 *
 * All versions of FreeBSD prior to 11/26/2004 will ignore this RST into a 0
 * window, causing the connection (and application) to hang indefinitely.
 * On patched versions of FreeBSD (and other operating systems), the RST
 * will be accepted and the program will exit in a few seconds.
 */

/*
 * If the alarm fired then we've hung and the test failed.
 */
void
do_alrm(int s)
{
	printf("not ok 1 - tcpfullwindowrst\n");
	exit(0);
}

int
main(void)
{
	int o, s, t, u, do_t, do_u;
	struct pollfd pfd[2];
	struct sockaddr_in sa;
	char buf[4096];

	printf("1..1\n");
	signal(SIGALRM, do_alrm);
	alarm(20);

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s == -1)
		return 1;
	o = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &o, sizeof o);
	memset(&sa, 0, sizeof sa);
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sa.sin_port = htons(3737);
	if (bind(s, (struct sockaddr *) &sa, sizeof sa) == -1)
		return 1;
	if (listen(s, 1) == -1)
		return 1;
	t = socket(AF_INET, SOCK_STREAM, 0);
	if (t == -1)
		return 1;
	if (connect(t, (struct sockaddr *) &sa, sizeof sa) == -1)
		return 1;
	u = accept(s, 0, 0);
	if (u == -1)
		return 1;
	close(s);
	fcntl(t, F_SETFL, fcntl(t, F_GETFL) | O_NONBLOCK);
	fcntl(u, F_SETFL, fcntl(t, F_GETFL) | O_NONBLOCK);
	do_t = 1;
	do_u = 1;
	pfd[0].fd = t;
	pfd[0].events = POLLOUT;
	pfd[1].fd = u;
	pfd[1].events = POLLOUT;
	while (do_t || do_u) {
		if (poll(pfd, 2, 1000) == 0) {
			if (do_t) {
				close(t);
				pfd[0].fd = -1;
				do_t = 0;
			}
			continue;
		}
		if (pfd[0].revents & POLLOUT) {
			if (write(t, buf, sizeof buf) == -1) {
				close(t);
				pfd[0].fd = -1;
				do_t = 0;
			}
		}
		if (pfd[1].revents & POLLOUT) {
			if (write(u, buf, sizeof buf) == -1) {
				close(u);
				pfd[1].fd = -1;
				do_u = 0;
			}
		}
	}

	printf("ok 1 - tcpfullwindowrst\n");
	return 0;
}
