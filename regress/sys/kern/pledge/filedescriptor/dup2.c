/*	$OpenBSD: dup2.c,v 1.1.1.1 2018/04/10 23:00:53 bluhm Exp $	*/
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
#include <unistd.h>

#include "header.h"

void
fdops(int fdpre, int fdpost)
{
	if (dup2(fdpre, 5) == -1)
		err(1, "dup2 pre");
	if (dup2(fdpost, 6) == -1)
		err(1, "dup2 post");
	if (dup2(fdpre, 7) == -1)
		err(1, "dup2 pepare post overwite");
	if (dup2(fdpost, 7) == -1)
		err(1, "dup2 post overwrite");
	if (dup2(fdpost, 8) == -1)
		err(1, "dup2 pepare pre overwrite");
	if (dup2(fdpre, 8) == -1)
		err(1, "dup2 pre overwite");
	if (dup2(fdpre, 9) == -1)
		err(1, "dup2 pepare pre equal");
	if (dup2(9, 9) == -1)
		err(1, "dup2 pre equal");
	if (dup2(fdpost, 10) == -1)
		err(1, "dup2 pepare post equal");
	if (dup2(10, 10) == -1)
		err(1, "dup2 post equal");
}
