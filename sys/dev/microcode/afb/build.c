/*	$OpenBSD: build.c,v 1.1 2009/12/07 20:35:25 kettenis Exp $ */

/*
 * Copyright (c) 2007 David Gwynne <dlg@openbsd.org>
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

#define AFB_FW_NAME "afb"

int
main(int argc, char *argv[])
{
	ssize_t		len;
	int		fd;
	int		i;

	fd = open(AFB_FW_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", AFB_FW_NAME);

	len = write(fd, afb_fw, sizeof(afb_fw));
	if (len == -1)
		err(1, "%s write", AFB_FW_NAME);
	if (len != sizeof(afb_fw))
		errx(1, "%s: short write", AFB_FW_NAME);

	close(fd);

	return (0);
}
