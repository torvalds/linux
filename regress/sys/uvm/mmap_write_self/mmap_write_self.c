/*
 * Copyright (c) 2011 Artur Grabowski <art@openbsd.org>
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
#include <sys/mman.h>
#include <err.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/*
 * Test a corner case writing to a file from an mmap region from that file
 * should fail.
 */
int
main(int argc, char **argv)
{
	char name[20] = "/tmp/fluff.XXXXXX";
	char *buf;
	size_t ps;
	int fd;

	ps = getpagesize();

	if ((fd = mkstemp(name)) == -1)
		err(1, "mkstemp");

	if (unlink(name) == -1)
		err(1, "unlink");

	buf = mmap(NULL, ps, PROT_READ, MAP_FILE|MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED)
		err(1, "mmap");

	if (pwrite(fd, buf, ps, 0) == ps)
		errx(1, "write to self succeeded");

	if (errno != EFAULT)
		err(1, "unexpected errno");

	return (0);
}
