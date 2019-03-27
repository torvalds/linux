/*-
 * Copyright (c) 2007 Robert N. M. Watson
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

#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define	PORT1	10001
#define	PORT2	10002

static void
try_0send(const char *test, int fd)
{
	ssize_t len;
	char ch;

	ch = 0;
	len = send(fd, &ch, 0, 0);
	if (len < 0)
		err(1, "%s: try_0send", test);
	if (len != 0)
		errx(1, "%s: try_0send: returned %zd", test, len);
}

static void
try_0write(const char *test, int fd)
{
	ssize_t len;
	char ch;

	ch = 0;
	len = write(fd, &ch, 0);
	if (len < 0)
		err(1, "%s: try_0write", test);
	if (len != 0)
		errx(1, "%s: try_0write: returned %zd", test, len);
}

static void
setup_udp(const char *test, int *fdp, int port1, int port2)
{
	struct sockaddr_in sin;
	int sock1, sock2;

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	sin.sin_port = htons(port1);
	sock1 = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock1 < 0)
		err(1, "%s: setup_udp: socket", test);
	if (bind(sock1, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "%s: setup_udp: bind(%s, %d)", test,
		    inet_ntoa(sin.sin_addr), PORT1);
	sin.sin_port = htons(port2);
	if (connect(sock1, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "%s: setup_udp: connect(%s, %d)", test,
		    inet_ntoa(sin.sin_addr), PORT2);

	sock2 = socket(PF_INET, SOCK_DGRAM, 0);
	if (sock2 < 0)
		err(1, "%s: setup_udp: socket", test);
	if (bind(sock2, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "%s: setup_udp: bind(%s, %d)", test,
		    inet_ntoa(sin.sin_addr), PORT2);
	sin.sin_port = htons(port1);
	if (connect(sock2, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "%s: setup_udp: connect(%s, %d)", test,
		    inet_ntoa(sin.sin_addr), PORT1);

	fdp[0] = sock1;
	fdp[1] = sock2;
}

static void
setup_tcp(const char *test, int *fdp, int port)
{
	fd_set writefds, exceptfds;
	struct sockaddr_in sin;
	int ret, sock1, sock2, sock3;
	struct timeval tv;

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	/*
	 * First set up the listen socket.
	 */
	sin.sin_port = htons(port);
	sock1 = socket(PF_INET, SOCK_STREAM, 0);
	if (sock1 < 0)
		err(1, "%s: setup_tcp: socket", test);
	if (bind(sock1, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(1, "%s: bind(%s, %d)", test, inet_ntoa(sin.sin_addr),
		    PORT1);
	if (listen(sock1, -1) < 0)
		err(1, "%s: listen", test);

	/*
	 * Now connect to it, non-blocking so that we don't deadlock against
	 * ourselves.
	 */
	sock2 = socket(PF_INET, SOCK_STREAM, 0);
	if (sock2 < 0)
		err(1, "%s: setup_tcp: socket", test);
	if (fcntl(sock2, F_SETFL, O_NONBLOCK) < 0)
		err(1, "%s: setup_tcp: fcntl(O_NONBLOCK)", test);
	if (connect(sock2, (struct sockaddr *)&sin, sizeof(sin)) < 0 &&
	    errno != EINPROGRESS)
		err(1, "%s: setup_tcp: connect(%s, %d)", test,
		    inet_ntoa(sin.sin_addr), PORT1);

	/*
	 * Now pick up the connection after sleeping a moment to make sure
	 * there's been time for some packets to go back and forth.
	 */
	if (sleep(1) != 0)
		err(1, "%s: sleep(1)", test);
	sock3 = accept(sock1, NULL, NULL);
	if (sock3 < 0)
		err(1, "%s: accept", test);
	if (sleep(1) != 0)
		err(1, "%s: sleep(1)", test);

	FD_ZERO(&writefds);
	FD_SET(sock2, &writefds);
	FD_ZERO(&exceptfds);
	FD_SET(sock2, &exceptfds);
	tv.tv_sec = 1;
	tv.tv_usec = 0;
	ret = select(sock2 + 1, NULL, &writefds, &exceptfds, &tv);
	if (ret < 0)
		err(1, "%s: setup_tcp: select", test);
	if (FD_ISSET(sock2, &exceptfds))
		errx(1, "%s: setup_tcp: select: exception", test);
	if (!FD_ISSET(sock2, &writefds))
		errx(1, "%s: setup_tcp: select: not writable", test);

	close(sock1);
	fdp[0] = sock2;
	fdp[1] = sock3;
}

static void
setup_udsstream(const char *test, int *fdp)
{

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, fdp) < 0)
		err(1, "%s: setup_udsstream: socketpair", test);
}

static void
setup_udsdgram(const char *test, int *fdp)
{

	if (socketpair(PF_LOCAL, SOCK_DGRAM, 0, fdp) < 0)
		err(1, "%s: setup_udsdgram: socketpair", test);
}

static void
setup_pipe(const char *test, int *fdp)
{

	if (pipe(fdp) < 0)
		err(1, "%s: setup_pipe: pipe", test);
}

static void
setup_fifo(const char *test, int *fdp)
{
	char path[] = "0send_fifo.XXXXXXX";
	int fd1, fd2;

	if (mkstemp(path) == -1)
		err(1, "%s: setup_fifo: mktemp", test);
	unlink(path);

	if (mkfifo(path, 0600) < 0)
		err(1, "%s: setup_fifo: mkfifo(%s)", test, path);

	fd1 = open(path, O_RDONLY | O_NONBLOCK);
	if (fd1 < 0)
		err(1, "%s: setup_fifo: open(%s, O_RDONLY)", test, path);

	fd2 = open(path, O_WRONLY | O_NONBLOCK);
	if (fd2 < 0)
		err(1, "%s: setup_fifo: open(%s, O_WRONLY)", test, path);

	fdp[0] = fd2;
	fdp[1] = fd1;
}

static void
close_both(int *fdp)
{

	close(fdp[0]);
	fdp[0] = -1;
	close(fdp[1]);
	fdp[1] = -1;
}

int
main(void)
{
	int fd[2];

	setup_udp("udp_0send", fd, PORT1, PORT2);
	try_0send("udp_0send", fd[0]);
	close_both(fd);

	setup_udp("udp_0write", fd, PORT1 + 10, PORT2 + 10);
	try_0write("udp_0write", fd[0]);
	close_both(fd);

	setup_tcp("tcp_0send", fd, PORT1);
	try_0send("tcp_0send", fd[0]);
	close_both(fd);

	setup_tcp("tcp_0write", fd, PORT1 + 10);
	try_0write("tcp_0write", fd[0]);
	close_both(fd);

	setup_udsstream("udsstream_0send", fd);
	try_0send("udsstream_0send", fd[0]);
	close_both(fd);

	setup_udsstream("udsstream_0write", fd);
	try_0write("udsstream_0write", fd[0]);
	close_both(fd);

	setup_udsdgram("udsdgram_0send", fd);
	try_0send("udsdgram_0send", fd[0]);
	close_both(fd);

	setup_udsdgram("udsdgram_0write", fd);
	try_0write("udsdgram_0write", fd[0]);
	close_both(fd);

	setup_pipe("pipe_0write", fd);
	try_0write("pipd_0write", fd[0]);
	close_both(fd);

	setup_fifo("fifo_0write", fd);
	try_0write("fifo_0write", fd[0]);
	close_both(fd);

	return (0);
}
