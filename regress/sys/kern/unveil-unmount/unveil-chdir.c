/*	$OpenBSD: unveil-chdir.c,v 1.1 2019/08/04 09:00:17 bluhm Exp $	*/
/*
 * Copyright (c) 2019 Alexander Bluhm <bluhm@openbsd.org>
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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
	char *cwd, *dir, *file, *path = NULL;

	if (argc != 3 && argc != 4)
		errx(2, "usage: unveil-chdir cwd dir [file]");

	cwd = argv[1];
	dir = argv[2];
	file = argv[3];
	if (file != NULL) {
		if (asprintf(&path, "%s/%s", dir, file) == -1)
			err(1, "asprintf");
	}

	if (chdir(cwd) == -1)
		err(1, "chdir %s", cwd);

	if (unveil(dir, "") == -1)
		err(1, "unveil %s", dir);
	if (file != NULL) {
		if (open(path, O_RDONLY) != -1)
			errx(1, "open %s succeeded", path);
		if (errno != ENOENT)
			err(1, "open %s error", path);
	}

	return 0;
}
