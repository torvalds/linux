/*	$OpenBSD: realpath-chdir.c,v 1.1.1.1 2019/08/05 15:16:39 bluhm Exp $	*/
/*
 * Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
 * Copyright (c) 2019 Moritz Buhl <mbuhl@moritzbuhl.de>
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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	char *cwd, *dir, *path = NULL;
	char res[PATH_MAX];

	if (argc != 3)
		errx(2, "usage: realpath-chdir cwd dir");

	cwd = argv[1];
	dir = argv[2];

	if (chdir(cwd) == -1)
		err(1, "chdir %s", cwd);

	if (realpath(dir, res) == NULL)
		err(1, "realpath %s", dir);

	return 0;
}
