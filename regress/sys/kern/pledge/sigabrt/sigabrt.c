/*	$OpenBSD: sigabrt.c,v 1.2 2016/01/09 06:13:43 semarie Exp $ */
/*
 * Copyright (c) 2015 Sebastien Marie <semarie@openbsd.org>
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
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

void
handler(int sigraised)
{
	/* this handler shouldn't not be called */
	printf("forbidden STDIO in SIGABRT handler\n");
}

int
main(int argc, char *argv[])
{
	/* install SIGABRT handler */
	signal(SIGABRT, &handler);

	printf("permitted STDIO\n");
	fflush(stdout);

	if (pledge("", NULL) == -1)
		err(EXIT_FAILURE, "pledge");

	/* this will triggered pledge_fail() */
	printf("forbidden STDIO 1\n");

	/* shouldn't continue */
	printf("forbidden STDIO 2\n");
	return (EXIT_SUCCESS);
}
