/*	$OpenBSD: arc4.c,v 1.3 2007/09/11 12:07:05 djm Exp $	*/
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

#include <sys/types.h>                                                         

#include <crypto/arc4.h> 

#define RC4SWAP(x,y) \
	do { \
		u_int8_t t = ctx->state[x];  \
		ctx->state[x] = ctx->state[y]; \
		ctx->state[y] = t; \
	} while(0)

void
rc4_keysetup(struct rc4_ctx *ctx, u_char *key, u_int32_t klen)
{
	u_int8_t x, y;
	u_int32_t i;

	x = y = 0;
	for (i = 0; i < RC4STATE; i++)
		ctx->state[i] = i;
	for (i = 0; i < RC4STATE; i++) {
		y = (key[x] + ctx->state[i] + y) & (RC4STATE - 1);
		RC4SWAP(i, y);
		x = (x + 1) % klen;
	}
	ctx->x = ctx->y = 0;
}

void
rc4_crypt(struct rc4_ctx *ctx, u_char *src, u_char *dst,
    u_int32_t len)
{
	u_int32_t i;

	for (i = 0; i < len; i++) {
		ctx->x = (ctx->x + 1) & (RC4STATE - 1);
		ctx->y = (ctx->state[ctx->x] + ctx->y) & (RC4STATE - 1);
		RC4SWAP(ctx->x, ctx->y);
		dst[i] = src[i] ^ ctx->state[
		   (ctx->state[ctx->x] + ctx->state[ctx->y]) & (RC4STATE - 1)];
	}
}

void
rc4_getbytes(struct rc4_ctx *ctx, u_char *dst, u_int32_t len)
{
	u_int32_t i;

	for (i = 0; i < len; i++) {
		ctx->x = (ctx->x + 1) & (RC4STATE - 1);
		ctx->y = (ctx->state[ctx->x] + ctx->y) & (RC4STATE - 1);
		RC4SWAP(ctx->x, ctx->y);
		dst[i] = ctx->state[
		   (ctx->state[ctx->x] + ctx->state[ctx->y]) & (RC4STATE - 1)];
	}
}

void
rc4_skip(struct rc4_ctx *ctx, u_int32_t len)
{
	for (; len > 0; len--) {
		ctx->x = (ctx->x + 1) & (RC4STATE - 1);
		ctx->y = (ctx->state[ctx->x] + ctx->y) & (RC4STATE - 1);
		RC4SWAP(ctx->x, ctx->y);
	}
}
