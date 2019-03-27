/*-
 * Copyright (C) 2005 The FreeBSD Project.  All rights reserved.
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
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int
main(void)
{
	struct sockaddr_in sock;
	socklen_t len;
	int listen_sock, connect_sock;
	u_short port;

	listen_sock = -1;

	/* Shutdown(2) on an invalid file descriptor has to return EBADF. */
	if ((shutdown(listen_sock, SHUT_RDWR) != -1) && (errno != EBADF))
		errx(-1, "shutdown() for invalid file descriptor does not "
		    "return EBADF");

	listen_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_sock == -1)
		errx(-1,
		    "socket(PF_INET, SOCK_STREAM, 0) for listen socket: %s",
		    strerror(errno));

	bzero(&sock, sizeof(sock));
	sock.sin_len = sizeof(sock);
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sock.sin_port = 0;

	if (bind(listen_sock, (struct sockaddr *)&sock, sizeof(sock)) < 0)
		errx(-1, "bind(%s, %d) for listen socket: %s",
		    inet_ntoa(sock.sin_addr), sock.sin_port, strerror(errno));

	len = sizeof(sock);
	if (getsockname(listen_sock, (struct sockaddr *)&sock, &len) < 0)
		errx(-1, "getsockname() for listen socket: %s",
		    strerror(errno));
	port = sock.sin_port;

	if (listen(listen_sock, -1) < 0)
		errx(-1, "listen() for listen socket: %s", strerror(errno));

	connect_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (connect_sock == -1)
		errx(-1, "socket(PF_INET, SOCK_STREAM, 0) for connect "
		    "socket: %s", strerror(errno));

	bzero(&sock, sizeof(sock));
	sock.sin_len = sizeof(sock);
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sock.sin_port = port;

	if (connect(connect_sock, (struct sockaddr *)&sock, sizeof(sock)) < 0)
		errx(-1, "connect() for connect socket: %s", strerror(errno));
	/* Try to pass an invalid flags. */
	if ((shutdown(connect_sock, SHUT_RD - 1) != -1) && (errno != EINVAL))
		errx(-1, "shutdown(SHUT_RD - 1) does not return EINVAL");
	if ((shutdown(connect_sock, SHUT_RDWR + 1) != -1) && (errno != EINVAL))
		errx(-1, "shutdown(SHUT_RDWR + 1) does not return EINVAL");

	if (shutdown(connect_sock, SHUT_RD) < 0)
		errx(-1, "shutdown(SHUT_RD) for connect socket: %s",
		    strerror(errno));
	if (shutdown(connect_sock, SHUT_WR) < 0)
		errx(-1, "shutdown(SHUT_WR) for connect socket: %s",
		    strerror(errno));

	close(connect_sock);
	close(listen_sock);

	return (0);
}
