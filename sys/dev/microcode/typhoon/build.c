/*	$OpenBSD: build.c,v 1.3 2016/12/18 18:28:39 krw Exp $	*/

/*
 * Copyright (c) 2004 Theo de Raadt <deraadt@openbsd.org>
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
#include <fcntl.h>
#include <unistd.h>
#include <err.h>
#include <stdio.h>

#include "3c990img.h"
#define FILENAME "3c990"

void
fullwrite(int fd, const void *buf, size_t nbytes)
{
	ssize_t r;

	r = write(fd, buf, nbytes);
	if (r == -1)
		err(1, "write");
	if (r != nbytes)
		errx(1, "write: short write");
}

int
main(int argc, char *argv[])
{
	int fd;
	ssize_t rlen;

	printf("creating %s length %zu\n", FILENAME, sizeof tc990image);
	fd = open(FILENAME, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", FILENAME);

	rlen = write(fd, tc990image, sizeof tc990image);
	if (rlen == -1)
		err(1, "%s", FILENAME);
	if (rlen != sizeof tc990image)
		errx(1, "%s: short write", FILENAME);
	close(fd);
	return 0;
}
