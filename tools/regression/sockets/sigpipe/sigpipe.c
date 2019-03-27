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
#include <sys/socket.h>

#include <netinet/in.h>

#include <err.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * This regression test is intended to verify whether or not SIGPIPE is
 * properly generated in several simple test cases, as well as testing
 * whether SO_NOSIGPIPE disables SIGPIPE, if available on the system.
 * SIGPIPE is generated if a write or send is attempted on a socket that has
 * been shutdown for write.  This test runs several test cases with UNIX
 * domain sockets and TCP sockets to confirm that either EPIPE or SIGPIPE is
 * properly returned.
 *
 * For the purposes of testing TCP, an unused port number must be specified.
 */
static void
usage(void)
{

	errx(-1, "usage: sigpipe tcpport");
}

/*
 * Signal catcher.  Set a global flag that can be tested by the caller.
 */
static int signaled;
static int
got_signal(void)
{

	return (signaled);
}

static void
signal_handler(int signum __unused)
{

	signaled = 1;
}

static void
signal_setup(const char *testname)
{

	signaled = 0;
	if (signal(SIGPIPE, signal_handler) == SIG_ERR)
		err(-1, "%s: signal(SIGPIPE)", testname);
}

static void
test_send(const char *testname, int sock)
{
	ssize_t len;
	char ch;

	ch = 0;
	len = send(sock, &ch, sizeof(ch), 0);
	if (len < 0) {
		if (errno == EPIPE)
			return;
		err(-1, "%s: send", testname);
	}
	errx(-1, "%s: send: returned %zd", testname, len);
}

static void
test_write(const char *testname, int sock)
{
	ssize_t len;
	char ch;

	ch = 0;
	len = write(sock, &ch, sizeof(ch));
	if (len < 0) {
		if (errno == EPIPE)
			return;
		err(-1, "%s: write", testname);
	}
	errx(-1, "%s: write: returned %zd", testname, len);
}

static void
test_send_wantsignal(const char *testname, int sock1, int sock2)
{

	if (shutdown(sock2, SHUT_WR) < 0)
		err(-1, "%s: shutdown", testname);
	signal_setup(testname);
	test_send(testname, sock2);
	if (!got_signal())
		errx(-1, "%s: send: didn't receive SIGPIPE", testname);
	close(sock1);
	close(sock2);
}

#ifdef SO_NOSIGPIPE
static void
test_send_dontsignal(const char *testname, int sock1, int sock2)
{
	int i;

	i = 1;
	if (setsockopt(sock2, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof(i)) < 0)
		err(-1, "%s: setsockopt(SOL_SOCKET, SO_NOSIGPIPE)", testname);
	if (shutdown(sock2, SHUT_WR) < 0)
		err(-1, "%s: shutdown", testname);
	signal_setup(testname);
	test_send(testname, sock2);
	if (got_signal())
		errx(-1, "%s: send: got SIGPIPE", testname);
	close(sock1);
	close(sock2);
}
#endif

static void
test_write_wantsignal(const char *testname, int sock1, int sock2)
{

	if (shutdown(sock2, SHUT_WR) < 0)
		err(-1, "%s: shutdown", testname);
	signal_setup(testname);
	test_write(testname, sock2);
	if (!got_signal())
		errx(-1, "%s: write: didn't receive SIGPIPE", testname);
	close(sock1);
	close(sock2);
}

#ifdef SO_NOSIGPIPE
static void
test_write_dontsignal(const char *testname, int sock1, int sock2)
{
	int i;

	i = 1;
	if (setsockopt(sock2, SOL_SOCKET, SO_NOSIGPIPE, &i, sizeof(i)) < 0)
		err(-1, "%s: setsockopt(SOL_SOCKET, SO_NOSIGPIPE)", testname);
	if (shutdown(sock2, SHUT_WR) < 0)
		err(-1, "%s: shutdown", testname);
	signal_setup(testname);
	test_write(testname, sock2);
	if (got_signal())
		errx(-1, "%s: write: got SIGPIPE", testname);
	close(sock1);
	close(sock2);
}
#endif

static int listen_sock;
static void
tcp_setup(u_short port)
{
	struct sockaddr_in sin;

	listen_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (listen_sock < 0)
		err(-1, "tcp_setup: listen");

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(port);

	if (bind(listen_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(-1, "tcp_setup: bind");

	if (listen(listen_sock, -1) < 0)
		err(-1, "tcp_setup: listen");
}

static void
tcp_teardown(void)
{

	close(listen_sock);
}

static void
tcp_pair(u_short port, int sock[2])
{
	int accept_sock, connect_sock;
	struct sockaddr_in sin;
	socklen_t len;

	connect_sock = socket(PF_INET, SOCK_STREAM, 0);
	if (connect_sock < 0)
		err(-1, "tcp_pair: socket");

	bzero(&sin, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	sin.sin_port = htons(port);

	if (connect(connect_sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		err(-1, "tcp_pair: connect");

	sleep(1);				/* Time for TCP to settle. */

	len = sizeof(sin);
	accept_sock = accept(listen_sock, (struct sockaddr *)&sin, &len);
	if (accept_sock < 0)
		err(-1, "tcp_pair: accept");

	sleep(1);				/* Time for TCP to settle. */

	sock[0] = accept_sock;
	sock[1] = connect_sock;
}

int
main(int argc, char *argv[])
{
	char *dummy;
	int sock[2];
	long port;

	if (argc != 2)
		usage();

	port = strtol(argv[1], &dummy, 10);
	if (port < 0 || port > 65535 || *dummy != '\0')
		usage();

#ifndef SO_NOSIGPIPE
	warnx("sigpipe: SO_NOSIGPIPE not defined, skipping some tests");
#endif

	/*
	 * UNIX domain socketpair().
	 */
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
		err(-1, "socketpair(PF_LOCAL, SOCK_STREAM)");
	test_send_wantsignal("test_send_wantsignal(PF_LOCAL)", sock[0],
	    sock[1]);

#ifdef SO_NOSIGPIPE
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
		err(-1, "socketpair(PF_LOCAL, SOCK_STREAM)");
	test_send_dontsignal("test_send_dontsignal(PF_LOCAL)", sock[0],
	    sock[1]);
#endif

	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
		err(-1, "socketpair(PF_LOCAL, SOCK_STREAM)");
	test_write_wantsignal("test_write_wantsignal(PF_LOCAL)", sock[0],
	    sock[1]);

#ifdef SO_NOSIGPIPE
	if (socketpair(PF_LOCAL, SOCK_STREAM, 0, sock) < 0)
		err(-1, "socketpair(PF_LOCAL, SOCK_STREAM)");
	test_write_dontsignal("test_write_dontsignal(PF_LOCAL)", sock[0],
	    sock[1]);
#endif

	/*
	 * TCP.
	 */
	tcp_setup(port);
	tcp_pair(port, sock);
	test_send_wantsignal("test_send_wantsignal(PF_INET)", sock[0],
	    sock[1]);

#ifdef SO_NOSIGPIPE
	tcp_pair(port, sock);
	test_send_dontsignal("test_send_dontsignal(PF_INET)", sock[0],
	    sock[1]);
#endif

	tcp_pair(port, sock);
	test_write_wantsignal("test_write_wantsignal(PF_INET)", sock[0],
	    sock[1]);

#ifdef SO_NOSIGPIPE
	tcp_pair(port, sock);
	test_write_dontsignal("test_write_dontsignal(PF_INET)", sock[0],
	    sock[1]);
#endif
	tcp_teardown();

	fprintf(stderr, "PASS\n");
	return (0);
}
