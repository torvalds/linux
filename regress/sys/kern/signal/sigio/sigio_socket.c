/*	$OpenBSD: sigio_socket.c,v 1.1 2020/09/16 14:02:23 mpi Exp $	*/

/*
 * Copyright (c) 2018 Visa Hankala
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

int
test_socket_badpgid(void)
{
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return test_common_badpgid(fds[0]);
}

int
test_socket_badsession(void)
{
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return test_common_badsession(fds[0]);
}

int
test_socket_cansigio(void)
{
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return test_common_cansigio(fds);
}

int
test_socket_getown(void)
{
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return test_common_getown(fds[0]);
}

/*
 * Test that the parent socket's signal target gets assigned to the socket
 * of an accepted connection.
 */
int
test_socket_inherit(void)
{
	struct sockaddr_in inaddr;
	socklen_t inaddrlen;
	pid_t pid;
	int cli, flags, sfd, sock;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	assert(sock != -1);

	memset(&inaddr, 0, sizeof(inaddr));
	inaddr.sin_len = sizeof(inaddr);
	inaddr.sin_family = AF_INET;
	inaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	assert(bind(sock, (struct sockaddr *)&inaddr, sizeof(inaddr)) == 0);
	assert(listen(sock, 1) == 0);

	flags = fcntl(sock, F_GETFL);
	assert(fcntl(sock, F_SETFL, flags | O_ASYNC) == 0);

	if (test_fork(&pid, &sfd) == PARENT) {
		inaddrlen = sizeof(inaddr);
		cli = accept(sock, (struct sockaddr *)&inaddr, &inaddrlen);
		assert(cli != -1);
		assert(fcntl(cli, F_GETOWN) == 0);
		close(cli);

		assert(fcntl(sock, F_SETOWN, getpid()) == 0);
		test_barrier(sfd);

		inaddrlen = sizeof(inaddr);
		cli = accept(sock, (struct sockaddr *)&inaddr, &inaddrlen);
		assert(cli != -1);
		assert(fcntl(cli, F_GETOWN) == getpid());
		close(cli);
	} else {
		inaddrlen = sizeof(inaddr);
		assert(getsockname(sock, (struct sockaddr *)&inaddr,
		    &inaddrlen) == 0);

		cli = socket(AF_INET, SOCK_STREAM, 0);
		assert(cli != -1);
		assert(connect(cli, (struct sockaddr *)&inaddr, sizeof(inaddr))
		    == 0);
		close(cli);

		test_barrier(sfd);

		cli = socket(AF_INET, SOCK_STREAM, 0);
		assert(cli != -1);
		assert(connect(cli, (struct sockaddr *)&inaddr, sizeof(inaddr))
		    == 0);
		close(cli);
	}
	return test_wait(pid, sfd);
}

int
test_socket_read(void)
{
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return test_common_read(fds);
}

int
test_socket_write(void)
{
	int fds[2];

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
	return test_common_write(fds);
}
