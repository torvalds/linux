/* $OpenBSD: getopt-test.c,v 1.1 2020/03/23 03:01:21 schwarze Exp $ */
/*
 * Copyright (c) 2020 Ingo Schwarze <schwarze@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Process command line options and arguments according to the
 * optstring in the environment variable OPTS.  Write:
 * OPT(c) for an option "-c" without an option argument
 * OPT(carg) for an option "-c" with an option argument "arg"
 * ARG(arg) for a non-option argument "arg"
 * NONE(arg) for a non-option argument "arg" processed with OPTS =~ ^-
 * ERR(?c) for an invalid option "-c", or one lacking an argument
 * ERR(:c) for an option "-c" lacking an argument while OPTS =~ ^:
 */
int
main(int argc, char *argv[])
{
	char	*optstring;
	int	 ch;

	if ((optstring = getenv("OPTS")) == NULL)
		optstring = "";

	opterr = 0;
	while ((ch = getopt(argc, argv, optstring)) != -1) {
		switch (ch) {
		case '\1':
			printf("NONE(%s)", optarg);
			break;
		case ':':
		case '?':
			printf("ERR(%c%c)", ch, optopt);
			break;
		default:
			printf("OPT(%c%s)", ch, optarg == NULL ? "" : optarg);
			break;
		}
	}
	while (optind < argc)
		printf("ARG(%s)", argv[optind++]);
	putchar('\n');
	return 0;
}
