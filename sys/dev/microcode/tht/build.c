/*	$OpenBSD: build.c,v 1.2 2007/04/19 04:08:51 dlg Exp $ */

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

#define THT_FW_NAME "tht"

int
main(int argc, char *argv[])
{
	ssize_t		len;
	int		fd;
	int		i;

	fd = open(THT_FW_NAME, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", THT_FW_NAME);

	for (i = 0; i < (sizeof(tht_fw) / sizeof(tht_fw[0])); i++)
		tht_fw[i] = htole32(tht_fw[i]);

	len = write(fd, tht_fw, sizeof(tht_fw));
	if (len == -1)
		err(1, "%s write", THT_FW_NAME);
	if (len != sizeof(tht_fw))
		errx(1, "%s: short write", THT_FW_NAME);

	close(fd);

	return (0);
}
