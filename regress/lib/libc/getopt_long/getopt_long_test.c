/*
 * Copyright (c) 2002 Todd C. Miller <millert@openbsd.org>
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
 *
 * Sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F39502-99-1-0512.
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

/*
 * Simple getopt_long() and getopt_long_only() excerciser.
 * ENVIRONMENT:
 *	LONG_ONLY	: use getopt_long_only() (default is getopt_long())
 *	POSIXLY_CORRECT	: don't permute args
 */

int
main(int argc, char **argv)
{
	int ch, idx, goggles;
	int (*gl)(int, char * const *, const char *, const struct option *, int *);
	struct option longopts[] = {
		{ "force", no_argument, 0, 0 },
		{ "fast", no_argument, 0, '1' },
		{ "best", no_argument, 0, '9' },
		{ "input", required_argument, 0, 'i' },
		{ "illiterate", no_argument, 0, 0 },
		{ "drinking", required_argument, &goggles, 42 },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 },
	};

	if (getenv("LONG_ONLY")) {
		gl = getopt_long_only;
		printf("getopt_long_only");
	} else {
		gl = getopt_long;
		printf("getopt_long");
	}
	if (getenv("POSIXLY_CORRECT"))
		printf(" (POSIXLY_CORRECT)");
	printf(": ");
	for (idx = 1; idx < argc; idx++)
		printf("%s ", argv[idx]);
	printf("\n");

	goggles = 0;
	for (;;) {
		idx = -1;
		ch = gl(argc, argv, "19bf:i:hW;-", longopts, &idx);
		if (ch == -1)
			break;
		switch (ch) {
		case 0:
		case '1':
		case '9':
		case 'h':
		case 'b':
		case '-':
			if (idx != -1) {
				if (goggles == 42)
					printf("option %s, arg %s\n",
					    longopts[idx].name, optarg);
				else
					printf("option %s\n",
					    longopts[idx].name);
			} else
				printf("option %c\n", ch);
			break;
		case 'f':
		case 'i':
			if (idx != -1)
				printf("option %s, arg %s\n",
				    longopts[idx].name, optarg);
			else
				printf("option %c, arg %s\n", ch, optarg);
			break;

		case '?':
			break;

		default:
			printf("unexpected return value: %c\n", ch);
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (argc > 0) {
		printf("remaining ARGV: ");
		while (argc--)
			printf("%s ", *argv++);
		printf("\n");
	}
	printf("\n");

	exit (0);
}
