/*	$OpenBSD: build.c,v 1.6 2009/11/10 23:55:12 mpf Exp $ */

/*
 * Copyright (c) 2009 Marcus Glocker <mglocker@openbsd.org>
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

#include "udl_huffman.h"

#define FILENAME "udl_huffman"

int
main(void)
{
	int i;
	FILE *file;
	uint32_t bit_count;
	uint32_t bit_pattern;

	if ((file = fopen(FILENAME, "w")) == NULL)
		err(1, "fopen %s", FILENAME);

	for (i = 0; i < UDL_HUFFMAN_RECORDS; i++) {
		bit_count = udl_huffman[i].bit_count;
		bit_pattern = htobe32(udl_huffman[i].bit_pattern);
		if (fwrite(&bit_count, sizeof(bit_count), 1, file) != 1)
			err(1, "fwrite");
		if (fwrite(&bit_pattern, sizeof(bit_pattern), 1, file) != 1)
			err(1, "fwrite");
	}

	fclose(file);

	return (0);
}
