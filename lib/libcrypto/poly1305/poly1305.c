/* $OpenBSD: poly1305.c,v 1.4 2023/07/07 12:01:32 beck Exp $ */
/*
 * Copyright (c) 2014 Joel Sing <jsing@openbsd.org>
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

#include <openssl/poly1305.h>
#include "poly1305-donna.c"

void
CRYPTO_poly1305_init(poly1305_context *ctx, const unsigned char key[32])
{
	poly1305_init(ctx, key);
}
LCRYPTO_ALIAS(CRYPTO_poly1305_init);

void
CRYPTO_poly1305_update(poly1305_context *ctx, const unsigned char *in,
    size_t len)
{
	poly1305_update(ctx, in, len);
}
LCRYPTO_ALIAS(CRYPTO_poly1305_update);

void
CRYPTO_poly1305_finish(poly1305_context *ctx, unsigned char mac[16])
{
	poly1305_finish(ctx, mac);
}
LCRYPTO_ALIAS(CRYPTO_poly1305_finish);
