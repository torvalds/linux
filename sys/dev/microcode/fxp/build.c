/*	$OpenBSD: build.c,v 1.4 2016/12/18 18:28:39 krw Exp $	*/

/*
 * Copyright (c) 2004 Theo de Raadt <deraadt@openbsd.org>
 * Copyright (c) 2004 Dmitry Bogdan <dsb@imcs.dvgu.ru>
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
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <unistd.h>

#include "rcvbundl.h"

const u_int32_t fxp_ucode_d101a[] = D101_A_RCVBUNDLE_UCODE;
const u_int32_t fxp_ucode_d101b0[] = D101_B0_RCVBUNDLE_UCODE;
const u_int32_t fxp_ucode_d101ma[] = D101M_B_RCVBUNDLE_UCODE;
const u_int32_t fxp_ucode_d101s[] = D101S_RCVBUNDLE_UCODE;
const u_int32_t fxp_ucode_d102[] = D102_B_RCVBUNDLE_UCODE;
const u_int32_t fxp_ucode_d102c[] = D102_C_RCVBUNDLE_UCODE;
const u_int32_t fxp_ucode_d102e[] = D102_E_RCVBUNDLE_UCODE;

#define UCODE(x)	x, sizeof(x)

static void
output(const char *name, const u_int32_t *ucode, const int ucode_len)
{
	ssize_t rlen;
	int fd, i;
	u_int32_t dword;

	printf("creating %s length %d (microcode: %zu DWORDS)\n",
	    name, ucode_len, ucode_len / sizeof(u_int32_t));
	fd = open(name, O_WRONLY|O_CREAT|O_TRUNC, 0644);
	if (fd == -1)
		err(1, "%s", name);
	for (i = 0; i < ucode_len / sizeof(u_int32_t); i++) {
		dword = htole32(ucode[i]);
		rlen = write(fd, &dword, sizeof(dword));
		if (rlen == -1)
			err(1, "%s", name);
		if (rlen != sizeof(dword))
			errx(1, "%s: short write", name);
	}
	close(fd);
}

int
main(int argc, char *argv[])
{
	output("fxp-d101a", UCODE(fxp_ucode_d101a));
	output("fxp-d101b0", UCODE(fxp_ucode_d101b0));
	output("fxp-d101ma", UCODE(fxp_ucode_d101ma));
	output("fxp-d101s", UCODE(fxp_ucode_d101s));
	output("fxp-d102", UCODE(fxp_ucode_d102));
	output("fxp-d102c", UCODE(fxp_ucode_d102c));
	output("fxp-d102e", UCODE(fxp_ucode_d102e));

	return (0);
}
