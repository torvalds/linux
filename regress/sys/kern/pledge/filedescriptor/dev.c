/*	$OpenBSD: dev.c,v 1.1.1.1 2018/04/10 23:00:53 bluhm Exp $	*/
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

#include <err.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "header.h"

void
fdops(int fdpre, int fdpost)
{
	char *devpre, *devpost;

	if (asprintf(&devpre, "/dev/fd/%d", fdpre) == -1)
		err(1, "asprintf fdpre");
	if (asprintf(&devpost, "/dev/fd/%d", fdpost) == -1)
		err(1, "asprintf fdpost");

	if (open(devpre, O_RDONLY) == -1)
		err(1, "open dev pre");
	if (open(devpost, O_RDONLY) == -1)
		err(1, "open dev post");

	free(devpre);
	free(devpost);
}
