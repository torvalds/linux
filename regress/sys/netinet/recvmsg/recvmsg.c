/*	$OpenBSD: recvmsg.c,v 1.2 2018/07/08 02:18:51 anton Exp $	*/
/*
 * Copyright (c) 2018 Anton Lindqvist <anton@openbsd.org>
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

/*
 * Regression test for double free of mbuf caused by rip{6,}_usrreq().
 */

#include <sys/socket.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static __dead void usage(void);

int
main(int argc, char *argv[])
{
	struct msghdr msg;
	ssize_t n;
	int c, s;
	int domain = -1;
	int type = -1;

	while ((c = getopt(argc, argv, "46dr")) != -1)
		switch (c) {
		case '4':
			domain = AF_INET;
			break;
		case '6':
			domain = AF_INET6;
			break;
		case 'd':
			type = SOCK_DGRAM;
			break;
		case 'r':
			type = SOCK_RAW;
			break;
		default:
			usage();
		}
	argc -= optind;
	argv += optind;
	if (argc > 0 || domain == -1 || type == -1)
		usage();

	s = socket(domain, type, 0);
	if (s == -1)
		err(1, "socket");
	memset(&msg, 0, sizeof(msg));
	n = recvmsg(s, &msg, MSG_OOB);
	assert(n == -1);
	assert(errno == EOPNOTSUPP);
	close(s);

	return 0;
}

static __dead void
usage(void)
{
	fprintf(stderr, "usage: recvmsg [-46dr]\n");
	exit(1);
}
