/*	$OpenBSD: unveil-perm.c,v 1.1.1.1 2019/08/01 15:20:51 bluhm Exp $	*/
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
	char *perm, *dir, *file, *path = NULL;

	if (argc != 3 && argc != 4)
		errx(2, "usage: unveil-perm perm dir [file]");

	perm = argv[1];
	dir = argv[2];
	file = argv[3];
	if (file != NULL) {
		if (asprintf(&path, "%s/%s", dir, file) == -1)
			err(1, "asprintf");
	}

	if (unveil(dir, perm) == -1)
		err(1, "unveil %s %s", dir, perm);
	if (file != NULL) {
		if (open(path, O_RDONLY) != -1)
			errx(1, "open %s succeeded", path);
		if (perm == NULL) {
			if (errno != ENOENT)
				err(1, "open %s error", path);
		}
	}

	return 0;
}
