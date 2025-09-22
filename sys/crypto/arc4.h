/*	$OpenBSD: arc4.h,v 1.3 2007/09/11 12:07:05 djm Exp $	*/
/*
 * Copyright (c) 2003 Markus Friedl <markus@openbsd.org>
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

#define RC4STATE 256
#define RC4KEYLEN 16

struct rc4_ctx {
	u_int8_t x, y;
	u_int8_t state[RC4STATE];
};

void	rc4_keysetup(struct rc4_ctx *, u_char *, u_int32_t)
    __attribute__((__bounded__(__buffer__,2,3)));
void	rc4_crypt(struct rc4_ctx *, u_char *, u_char *, u_int32_t)
    __attribute__((__bounded__(__buffer__,2,4)))
    __attribute__((__bounded__(__buffer__,3,4)));
void	rc4_getbytes(struct rc4_ctx *, u_char *, u_int32_t)
    __attribute__((__bounded__(__buffer__,2,3)));
void	rc4_skip(struct rc4_ctx *, u_int32_t);
