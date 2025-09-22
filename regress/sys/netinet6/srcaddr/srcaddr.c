/*	$OpenBSD: srcaddr.c,v 1.1 2020/04/13 03:09:29 pamela Exp $	*/

/*
 * Copyright (c) 2020 Pamela Mosiejczuk <pamela@openbsd.org>
 * Copyright (c) 2020 Florian Obser <florian@openbsd.org>
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

#include <err.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

__dead void	usage(void);

/* given an IPv6 destination address, return the source address selected */
int
main(int argc, char *argv[])
{
	struct addrinfo		 hints, *res;
	struct sockaddr_storage	 ss;
	socklen_t		 len;
	int			 ch, error, s;
	char			*target, src[NI_MAXHOST];

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	target = *argv;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
	hints.ai_family = AF_INET6;
	hints.ai_socktype = SOCK_DGRAM;

	if ((error = getaddrinfo(target, "8888", &hints, &res)))
		errx(1, "%s", gai_strerror(error));

	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s == -1)
		err(1, "socket");

	if (connect(s, res->ai_addr, res->ai_addrlen) == -1)
		err(1, "connect");
	freeaddrinfo(res);

	len = sizeof(ss);
	if (getsockname(s, (struct sockaddr *)&ss, &len) == -1)
		err(1, "getsockname");

	if ((error = getnameinfo((struct sockaddr *)&ss, ss.ss_len, src,
	    sizeof(src), NULL, 0, NI_NUMERICHOST | NI_NUMERICSERV)))
		errx(1, "%s", gai_strerror(error));

	printf("%s\n", src);

	return (0);
}

__dead void
usage(void)
{
	fprintf(stderr, "usage: srcaddr destination\n");
	exit (1);
}
