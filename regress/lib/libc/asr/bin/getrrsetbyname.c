/*	$OpenBSD: getrrsetbyname.c,v 1.2 2018/12/15 15:16:12 eric Exp $	*/
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

#include <netinet/in.h>
#include <arpa/nameser.h>

#include <err.h>
#include <errno.h>
#include <getopt.h>
#include <resolv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"

static void
usage(void)
{
	extern const char * __progname;

	fprintf(stderr, "usage: %s [-e] [-t type] [host...]\n",
	    __progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	int			ch, i;
	uint16_t		type = T_A;
	char			*host;
	struct rrsetinfo	*rrset;

	while((ch = getopt(argc, argv, "R:et:")) !=  -1) {
		switch(ch) {
		case 'R':
			parseresopt(optarg);
			break;
		case 'e':
			long_err += 1;
			break;
		case 't':
			if ((type = strtotype(optarg)) == 0)
				usage();
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	for (i = 0; i < argc; i++) {

		if (i)
			printf("\n");
		printf("===> \"%s\"\n", argv[i]);
		host = gethostarg(argv[i]);
		
		errno = 0;
		h_errno = 0;
		gai_errno = 0;
		rrset_errno = 0;
		
		rrset_errno = getrrsetbyname(host, C_IN, type, 0, &rrset);

		if (rrset_errno == 0)
			print_rrsetinfo(rrset);
		print_errors();
	}
	
	return (0);
}
