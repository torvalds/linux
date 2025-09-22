/*	$OpenBSD: radius_subr.c,v 1.1 2024/07/14 15:31:49 yasuoka Exp $	*/

/*
 * Copyright (c) 2013, 2023 Internet Initiative Japan Inc.
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

#include <md5.h>
#include <string.h>

#include "radius_subr.h"

void
radius_attr_hide(const char *secret, const char *authenticator,
    const u_char *salt, u_char *plain, int plainlen)
{
	int	  i, j;
	u_char	  b[16];
	MD5_CTX	  md5ctx;

	i = 0;
	do {
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, secret, strlen(secret));
		if (i == 0) {
			MD5Update(&md5ctx, authenticator, 16);
			if (salt != NULL)
				MD5Update(&md5ctx, salt, 2);
		} else
			MD5Update(&md5ctx, plain + i - 16, 16);
		MD5Final(b, &md5ctx);

		for (j = 0; j < 16 && i < plainlen; i++, j++)
			plain[i] ^= b[j];
	} while (i < plainlen);
}

void
radius_attr_unhide(const char *secret, const char *authenticator,
    const u_char *salt, u_char *crypt0, int crypt0len)
{
	int	  i, j;
	u_char	  b[16];
	MD5_CTX	  md5ctx;

	i = 16 * ((crypt0len - 1) / 16);
	while (i >= 0) {
		MD5Init(&md5ctx);
		MD5Update(&md5ctx, secret, strlen(secret));
		if (i == 0) {
			MD5Update(&md5ctx, authenticator, 16);
			if (salt != NULL)
				MD5Update(&md5ctx, salt, 2);
		} else
			MD5Update(&md5ctx, crypt0 + i - 16, 16);
		MD5Final(b, &md5ctx);

		for (j = 0; j < 16 && i + j < crypt0len; j++)
			crypt0[i + j] ^= b[j];
		i -= 16;
	}
}
