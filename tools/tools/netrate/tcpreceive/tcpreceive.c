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

/*
 * Back end to a variety of TCP-related benchmarks.  This program accepts TCP
 * connections on a port, and echoes all received data back to the sender.
 * It is capable of handling only one connection at a time out of a single
 * thread.
 */
#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * Simple micro-benchmark to see how many connections/second can be created
 * in a serialized fashion against a given server.  A timer signal is used
 * to interrupt the loop and assess the cost, and uses a fixed maximum
 * buffer size.  It makes no attempt to time out old connections.
 */
#define	BUFFERSIZE	128*1024
#define	PORT		6060

static void
handle_connection(int accept_sock)
{
	u_char buffer[BUFFERSIZE];
	ssize_t len, recvlen, sofar;
	int s;

	s = accept(accept_sock, NULL, NULL);
	if (s < 0) {
		warn("accept");
		return;
	}

	while (1) {
		recvlen = recv(s, buffer, BUFFERSIZE, 0);
		if (recvlen < 0 || recvlen == 0) {
			close(s);
			return;
		}
		sofar = 0;
		while (sofar < recvlen) {
			len = send(s, buffer + sofar, recvlen - sofar, 0);
			if (len < 0) {
				close(s);
				return;
			}
			sofar += len;
		}
	}
}

int
main(int argc, char *argv[])
{
	struct sockaddr_in sin;
	int accept_sock;

	accept_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (accept_sock < 0)
		err(-1, "socket(PF_INET, SOCKET_STREAM, 0)");

	bzero(&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	sin.sin_len = sizeof(sin);
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(PORT);

	if (bind(accept_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(-1, "bind");

	if (listen(accept_sock, -1) < 0)
		err(-1, "listen");

	while (1)
		handle_connection(accept_sock);

	return (0);
}
