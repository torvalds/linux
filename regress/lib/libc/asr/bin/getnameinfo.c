/*	$OpenBSD: getnameinfo.c,v 1.2 2018/12/15 15:16:12 eric Exp $	*/
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

	fprintf(stderr, "usage: %s [-DFHNSe] [-p portno] <addr...>\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char			 serv[1024];
	char			 host[1024];
	const char		 *e;
	int			 i, ch, flags = 0, port = 0;
	struct sockaddr_storage	 ss;
	struct sockaddr		*sa;

	sa = (struct sockaddr*)&ss;

	while((ch = getopt(argc, argv, "DFHNR:Saep:")) !=  -1) {
		switch(ch) {
		case 'D':
			flags |= NI_DGRAM;
			break;
		case 'F':
			flags |= NI_NOFQDN;
			break;
		case 'H':
			flags |= NI_NUMERICHOST;
			break;
		case 'N':
			flags |= NI_NAMEREQD;
			break;
		case 'R':
			parseresopt(optarg);
			break;
		case 'S':
			flags |= NI_NUMERICSERV;
			break;
		case 'e':
			long_err += 1;
			break;
		case 'p':
			port = strtonum(optarg, 0, 65535, &e);
			if (e)
				usage();
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

		if (sockaddr_from_str(sa, AF_UNSPEC, argv[i]) == -1) {
			printf("   => invalid address\n");
			continue;
		}

		if (sa->sa_family == PF_INET)
			((struct sockaddr_in *)sa)->sin_port = htons(port);
		else if (sa->sa_family == PF_INET6)
			((struct sockaddr_in6 *)sa)->sin6_port = htons(port);

		errno = 0;
		h_errno = 0;
		gai_errno = 0;
		rrset_errno = 0;

		gai_errno = getnameinfo(sa, sa->sa_len, host, sizeof host, serv,
		    sizeof serv, flags);

		if (gai_errno == 0)
			printf("   %s:%s\n", host, serv);
		print_errors();

	}

	return (0);
}
