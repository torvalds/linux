/*	$OpenBSD: nonxt-sendrecv.c,v 1.2 2018/05/21 01:19:21 bluhm Exp $	*/
/*
 * Copyright (c) Alexander Bluhm <bluhm@genua.de>
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

#include <sys/types.h>
#include <sys/socket.h>

#include <netdb.h>

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void __dead usage(void);

void
usage(void)
{
	fprintf(stderr, "usage: nonxt-sendrecv [localaddr] remoteaddr\n"
	    "Send empty protocol 59 packet and wait for answer.\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct addrinfo hints, *res, *res0;
	struct timeval to;
	const char *cause = NULL, *local, *remote;
	int error;
	int save_errno;
	int s;
	char buf[1024];

	switch (argc) {
	case 2:
		local = NULL;
		remote = argv[1];
		break;
	case 3:
		local = argv[1];
		remote = argv[2];
		break;
	default:
		usage();
	}

	if (pledge("stdio inet dns", NULL) == -1)
		err(1, "pledge");

	/* Create socket and connect it to remote address. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_RAW;
	hints.ai_protocol = IPPROTO_NONE;
	error = getaddrinfo(remote, NULL, &hints, &res0);
	if (error)
		errx(1, "getaddrinfo remote: %s", gai_strerror(error));
	for (res = res0; res; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			cause = "socket";
			continue;
		}
		if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
			cause = "connect";
			save_errno = errno;
			close(s);
			errno = save_errno;
			continue;
		}
		break;
	}
	if (res == NULL)
		err(1, "%s", cause);

	/* Optionally bind the socket to local address. */
	if (local != NULL) {
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = res->ai_family;
		hints.ai_socktype = SOCK_RAW;
		hints.ai_protocol = IPPROTO_NONE;
		hints.ai_flags = AI_PASSIVE;
		freeaddrinfo(res0);
		error = getaddrinfo(local, NULL, &hints, &res0);
		if (error)
			errx(1, "getaddrinfo local: %s", gai_strerror(error));
		for (res = res0; res; res = res->ai_next) {
			if (bind(s, res->ai_addr, res->ai_addrlen) == -1)
				continue;
			break;
		}
		if (res == NULL)
			err(1, "bind");
	}
	freeaddrinfo(res0);

	if (pledge("stdio inet", NULL) == -1)
		err(1, "pledge");

	/* Send a protocol 59 packet. */
	if (send(s, buf, 0, 0) == -1)
		err(1, "send");
	/* Wait for up to 3 seconds to receive a reply packet. */
	to.tv_sec = 3;
	to.tv_usec = 0;
	if (setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to)) == -1)
		err(1, "setsockopt");
	if (recv(s, buf, sizeof(buf), 0) == -1)
		err(1, "recv");

	return 0;
}
