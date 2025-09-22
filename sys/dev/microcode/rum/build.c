/*	$OpenBSD: build.c,v 1.3 2009/05/15 15:53:55 damien Exp $	*/

/*-
 * Copyright (c) 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "microcode.h"

static void
output(const char *name, const uint8_t *ucode, int size)
{
	ssize_t rlen;
	int fd;

	printf("creating %s length %d\n", name, size);

	fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", name);

	rlen = write(fd, ucode, size);
	if (rlen == -1)
		err(1, "%s", name);
	if (rlen != size)
		errx(1, "%s: short write", name);

	close(fd);
}

int
main(void)
{
	output("rum-rt2573", rt2573, sizeof rt2573);
	output("run-rt2870", rt2870, sizeof rt2870);
	output("run-rt3071", rt3071, sizeof rt3071);

	return 0;
}
