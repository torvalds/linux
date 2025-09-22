/*	$OpenBSD: build.c,v 1.1 2021/10/04 01:33:42 kevlo Exp $	*/

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
	output("rtwn-rtl8188e", rtl8188e, sizeof rtl8188e);
	output("rtwn-rtl8192cU", rtl8192cU, sizeof rtl8192cU);
	output("rtwn-rtl8192cU_B", rtl8192cU_B, sizeof rtl8192cU_B);
	output("rtwn-rtl8723", rtl8723, sizeof rtl8723);
	output("rtwn-rtl8723_B", rtl8723_B, sizeof rtl8723_B);

	return 0;
}
