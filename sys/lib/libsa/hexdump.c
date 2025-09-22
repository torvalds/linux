/*	$OpenBSD: hexdump.c,v 1.1 2019/11/28 00:17:13 bluhm Exp $	*/
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

#include <sys/types.h>

#include "stand.h"

void
hexdump(const void *addr, size_t size)
{
	const unsigned char *line, *end;
	int byte;

	end = (const char *)addr + size;
	for (line = addr; line < end; line += 16) {
		printf("%08lx  ", line);
		for (byte = 0; byte < 16; byte++) {
			if (&line[byte] < end)
				printf("%02x ", line[byte]);
			else
				printf("   ");
			if (byte == 7)
				printf(" ");
		}
		printf(" |");
		for (byte = 0; byte < 16; byte++) {
			if (&line[byte] < end) {
				if (line[byte] >= ' ' && line[byte] <= '~')
					printf("%c", line[byte]);
				else
					printf(".");
			} else
				break;
		}
		printf("|\n");
	}
	printf("%08lx\n", end);
}
