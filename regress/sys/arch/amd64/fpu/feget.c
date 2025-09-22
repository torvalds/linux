/*	$OpenBSD: feget.c,v 1.2 2021/12/13 16:56:49 deraadt Exp $	*/
/*
 * Copyright (c) 2018 Alexander Bluhm <bluhm@openbsd.org>
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

#include <err.h>
#include <fenv.h>
#include <stdio.h>

#define nitems(_a)     (sizeof((_a)) / sizeof((_a)[0]))

int
main(int argc, char *argv[])
{
	fenv_t fenv;
	size_t i;

	if (fegetenv(&fenv))
		err(1, "fegetenv");

	printf("control\t%08x\n", fenv.__x87.__control);
	printf("status\t%08x\n", fenv.__x87.__status);
	printf("tag\t%08x\n", fenv.__x87.__tag);
	for (i = 0; i < nitems(fenv.__x87.__others); i++)
		printf("others[%zu]\t%08x\n", i, fenv.__x87.__others[i]);
	printf("mxcsr\t%08x\n", fenv.__mxcsr);

	return 0;
}
