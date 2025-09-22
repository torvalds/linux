/*	$OpenBSD: build.c,v 1.5 2017/08/27 08:15:48 otto Exp $	*/

/*
 * Copyright (c) 2007 Reyk Floeter <reyk@openbsd.org>
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

#include <dev/pci/if_myxreg.h>

#include <fcntl.h>
#include <stdlib.h>
#include <err.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <zlib.h>

#include "eth_z8e.h"
#include "ethp_z8e.h"

#define CHUNK 8192

void
myx_build_firmware(u_int8_t *fw, size_t len, size_t ulen, const char *file)
{
	z_stream zs;

	FILE *f;
	size_t rlen, total = 0;
	u_int8_t *ufw;
	int rv;

	f = fopen(file, "w");
	if (f == NULL)
		err(1, "%s", file);

	ufw = malloc(ulen);
	if (ufw == NULL)
		err(1, "ufw malloc");

	bzero(&zs, sizeof (zs));
	rv = inflateInit(&zs);
	if (rv != Z_OK)
		errx(1, "uncompress init failure");

	zs.avail_in = len;
	zs.next_in = fw;
	zs.avail_out = ulen;
	zs.next_out = ufw;
	rv = inflate(&zs, Z_FINISH);
        if (rv != Z_STREAM_END)
		errx(1, "zlib %d", rv);

	inflateEnd(&zs);

	do {
		rlen = ulen - total;
		if (rlen > CHUNK)
			rlen = CHUNK;

		if (fwrite(&ufw[total], rlen, 1, f) < 1) {
			if (!ferror(f))
				errx(1, "unexpected short write");
			err(1, "%s", file);
		}

		total += rlen;
	} while (total < ulen);

	printf("%s: len %zu -> %zu\n", file, len, ulen);
	free(ufw);
	fclose(f);
}

int
main(int argc, char *argv[])
{
	myx_build_firmware(eth_z8e, eth_z8e_length,
	    eth_z8e_uncompressed_length, MYXFW_ALIGNED);
	myx_build_firmware(ethp_z8e, ethp_z8e_length,
	    ethp_z8e_uncompressed_length, MYXFW_UNALIGNED);
	return (0);
}
