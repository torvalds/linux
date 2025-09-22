/*	$OpenBSD: dup3.c,v 1.1.1.1 2018/04/10 23:00:53 bluhm Exp $	*/
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
#include <unistd.h>

#include "header.h"

void
fdops(int fdpre, int fdpost)
{
	if (dup3(fdpre, 5, O_CLOEXEC) == -1)
		err(1, "dup3 pre");
	if (dup3(fdpost, 6, O_CLOEXEC) == -1)
		err(1, "dup3 post");
	if (dup3(fdpre, 7, O_CLOEXEC) == -1)
		err(1, "dup3 pepare post overwite");
	if (dup3(fdpost, 7, O_CLOEXEC) == -1)
		err(1, "dup3 post overwrite");
	if (dup3(fdpost, 8, O_CLOEXEC) == -1)
		err(1, "dup3 pepare pre overwrite");
	if (dup3(fdpre, 8, O_CLOEXEC) == -1)
		err(1, "dup3 pre overwite");
	if (dup3(fdpre, 9, 0) == -1)
		err(1, "dup3 pepare pre equal");
	if (dup3(9, 9, O_CLOEXEC) != -1)
		errx(1, "dup3 pre equal succeeded");
	if (dup3(fdpost, 10, 0) == -1)
		err(1, "dup3 pepare post equal");
	if (dup3(10, 10, O_CLOEXEC) != -1)
		errx(1, "dup3 post equal succeeded");
}
