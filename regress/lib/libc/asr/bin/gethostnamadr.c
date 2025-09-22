/*	$OpenBSD: gethostnamadr.c,v 1.3 2018/12/15 15:16:12 eric Exp $	*/
/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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
#include <netinet/in.h>
#include <arpa/inet.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

static void
usage(void)
{
	extern const char * __progname;

	fprintf(stderr, "usage: %s [-46e] <host...>\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int			 i, ch, aflag, family = AF_INET;
	struct hostent		*h;
	char			*host;
	char			 addr[16];
	int			 addrlen;

	aflag = 0;
	while((ch = getopt(argc, argv, "46R:ae")) !=  -1) {
		switch(ch) {
		case '4':
			family = AF_INET;
			break;
		case '6':
			family = AF_INET6;
			break;
		case 'R':
			parseresopt(optarg);
			break;
		case 'a':
			aflag = 1;
			break;
		case 'e':
			long_err += 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	for(i = 0; i < argc; i++) {

		if (i)
			printf("\n");
		printf("===> \"%s\"\n", argv[i]);
		host = gethostarg(argv[i]);

		if (aflag && addr_from_str(addr, &family, &addrlen, host) == -1)
			errx(1, "bad address");

		errno = 0;
		h_errno = 0;
		gai_errno = 0;
		rrset_errno = 0;

		if (aflag == 0)
			h = gethostbyname2(host, family);
		else
			h = gethostbyaddr(addr, addrlen, family);
		if (h)
			print_hostent(h);
		print_errors();
	}

	return (0);
}

int
addr_from_str(char *addr, int *family, int *len, const char *src)
{
	if (inet_pton(AF_INET6, src, addr) == 1) {
		*family = AF_INET6;
		*len = 16;
		return (0);
	}
	if (inet_pton(AF_INET, src, addr) == 1) {
		*family = AF_INET;
		*len = 4;
		return (0);
	}
	return (-1);
}
