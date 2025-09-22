/*	$OpenBSD: build.c,v 1.6 2017/09/08 05:36:52 deraadt Exp $	*/

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
#include <sys/param.h>
#include <sys/uio.h>
#include <fcntl.h>
#include <err.h>
#include <stdio.h>
#include <unistd.h>

#include "tusb3410.h"
#define FILENAME "tusb3410"

int
main(int argc, char *argv[])
{
	ssize_t rlen;
	int fd;

	printf("creating %s length %zu\n", FILENAME, sizeof uticom_fw_3410);
	fd = open(FILENAME, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", FILENAME);

	rlen = write(fd, uticom_fw_3410, sizeof uticom_fw_3410);
	if (rlen != sizeof uticom_fw_3410)
		err(1, "%s", FILENAME);
	return 0;
}
