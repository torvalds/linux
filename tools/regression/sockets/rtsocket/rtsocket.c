/*-
 * Copyright (c) 2006 Robert N. M. Watson
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
 * Simple routing socket regression test: create and destroy a raw routing
 * socket, and make sure that dgram and stream don't work, socketpair, etc.
 */

#include <sys/types.h>
#include <sys/socket.h>

#include <net/route.h>

#include <err.h>
#include <errno.h>
#include <unistd.h>

int
main(void)
{
	int sock, socks[2];

	sock = socket(PF_ROUTE, SOCK_STREAM, 0);
	if (sock >= 0) {
		close(sock);
		errx(-1, "socket(PF_ROUTE, SOCK_STREAM, 0) returned %d",
		    sock);
	}

	if (errno != EPROTOTYPE)
		err(-1, "socket(PF_ROUTE, SOCK_STREAM, 0)");

	sock = socket(PF_ROUTE, SOCK_DGRAM, 0);
	if (sock >= 0) {
		close(sock);
		errx(-1, "socket(PF_ROUTE, SOCK_DGRAM, 0) returned %d",
		    sock);
	}

	if (errno != EPROTOTYPE)
		err(-1, "socket(PF_ROUTE, SOCK_DGRAM, 0)");

	sock = socket(PF_ROUTE, SOCK_RAW, 0);
	if (sock < 0)
		err(-1, "socket(PF_ROUTE, SOCK_RAW, 0)");
	close(sock);

	if (socketpair(PF_ROUTE, SOCK_STREAM, 0, socks) == 0) {
		close(socks[0]);
		close(socks[1]);
		errx(-1,
		    "socketpair(PF_ROUTE, SOCK_STREAM, 0, socks) success");
	}

	if (errno != EPROTOTYPE)
		err(-1, "socketpair(PF_ROUTE, SOCK_STREAM, 0, socks)");

	if (socketpair(PF_ROUTE, SOCK_DGRAM, 0, socks) == 0) {
		close(socks[0]);
		close(socks[1]);
		errx(-1,
		    "socketpair(PF_ROUTE, SOCK_DGRAM, 0, socks) success");
	}

	if (errno != EPROTOTYPE)
		err(-1, "socketpair(PF_ROUTE, SOCK_DGRAM, 0, socks)");

	if (socketpair(PF_ROUTE, SOCK_RAW, 0, socks) == 0) {
		close(socks[0]);
		close(socks[1]);
		errx(-1,
		    "socketpair(PF_ROUTE, SOCK_STREAM, 0, socks) success");
	}

	return (0);
}
