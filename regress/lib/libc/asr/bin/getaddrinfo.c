/*	$OpenBSD: getaddrinfo.c,v 1.3 2018/12/15 15:16:12 eric Exp $	*/
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

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

static void
usage(void)
{
	extern const char * __progname;

	fprintf(stderr, "usage: %s [-CHSPe] [-f family] [-p proto] "
		"[-s servname]\n	[-t socktype] <host...>\n", __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct addrinfo		*ai, *res, hints;
	char			*servname = NULL, *host;
	int			 i, ch;

	memset(&hints, 0, sizeof hints);

	while((ch = getopt(argc, argv, "CFHPR:Sef:p:s:t:")) !=  -1) {
		switch(ch) {
		case 'C':
			hints.ai_flags |= AI_CANONNAME;
			break;
		case 'F':
			hints.ai_flags |= AI_FQDN;
			break;
		case 'H':
			hints.ai_flags |= AI_NUMERICHOST;
			break;
		case 'P':
			hints.ai_flags |= AI_PASSIVE;
			break;
		case 'R':
			parseresopt(optarg);
			break;
		case 'S':
			hints.ai_flags |= AI_NUMERICSERV;
			break;
		case 'e':
			long_err += 1;
			break;
		case 'f':
			if (!strcmp(optarg, "inet"))
				hints.ai_family = AF_INET;
			else if (!strcmp(optarg, "inet6"))
				hints.ai_family = AF_INET6;
			else
				usage();
			break;
		case 'p':
			if (!strcmp(optarg, "udp"))
				hints.ai_protocol = IPPROTO_UDP;
			else if (!strcmp(optarg, "tcp"))
				hints.ai_protocol = IPPROTO_TCP;
			else if (!strcmp(optarg, "icmp"))
				hints.ai_protocol = IPPROTO_ICMP;
			else if (!strcmp(optarg, "icmpv6"))
				hints.ai_protocol = IPPROTO_ICMPV6;
			else
				usage();
			break;
		case 's':
			servname = optarg;
			break;
		case 't':
			if (!strcmp(optarg, "stream"))
				hints.ai_socktype = SOCK_STREAM;
			else if (!strcmp(optarg, "dgram"))
				hints.ai_socktype = SOCK_DGRAM;
			else if (!strcmp(optarg, "raw"))
				hints.ai_socktype = SOCK_RAW;
			else
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
		host = gethostarg(argv[i]);

		errno = 0;
		h_errno = 0;
		gai_errno = 0;
		rrset_errno = 0;

		gai_errno = getaddrinfo(host, servname, &hints, &ai);

		print_errors();
		if (gai_errno == 0) {
			for (res = ai; res; res = res->ai_next)
				print_addrinfo(res);
			freeaddrinfo(ai);
		}
	}

	return (0);
}
