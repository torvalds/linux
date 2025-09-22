/*	$OpenBSD: execpromise.c,v 1.2 2021/12/13 18:04:28 deraadt Exp $	*/
/*
 * Copyright (c) 2017 Florian Obser <florian@openbsd.org>
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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
	int ch, child = 0, s;
	char **oargv = argv;

	while ((ch = getopt(argc, argv, "C")) != -1) {
		switch (ch) {
		case 'C':
			child = 1;
			break;
		default:
			errx(1, "");
		}
	}
	argc -= optind;
	argv += optind;

	if (child ==1) {
		warnx("child");
		if (argc > 1)
			errx(1, "argc: %d", argc);
		if (argc == 1) {
			warnx("plege(\"%s\",\"\")", argv[0]);
			if (pledge(argv[0], "") == -1)
				err(24, "child pledge");
		}

		warnx("trying to open socket");

		s = socket(AF_INET, SOCK_DGRAM, 0);
		if (s == -1)
			err(23, "open");
		else
			warnx("opened socket");

		close(s);
		exit(0);
	} else {
		warnx("parent");
		if (argc == 2)
			warnx("execpromise: \"%s\", child pledge: \"%s\"",
			    argv[0], argv[1]);
		else if (argc == 1)
			warnx("execpromise: \"%s\"", argv[0]);
		else
			errx(1, "argc out of range");

		if (pledge("stdio exec", argv[0]) == -1)
			err(1, "parent pledge");

		oargv[1] = "-C";
		execvp(oargv[0], &oargv[0]);
		err((errno == ENOENT) ? 127 : 126, "%s", argv[0]);
	}
}
