/* $OpenBSD: poly1305.h,v 1.4 2025/01/25 17:59:44 tb Exp $ */
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

#ifndef HEADER_POLY1305_H
#define HEADER_POLY1305_H

#include <openssl/opensslconf.h>

#include <stddef.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef struct poly1305_context {
	size_t aligner;
	unsigned char opaque[136];
} poly1305_context;

typedef struct poly1305_context poly1305_state;

void CRYPTO_poly1305_init(poly1305_context *ctx, const unsigned char key[32]);
void CRYPTO_poly1305_update(poly1305_context *ctx, const unsigned char *in,
    size_t len);
void CRYPTO_poly1305_finish(poly1305_context *ctx, unsigned char mac[16]);

#ifdef  __cplusplus
}
#endif

#endif /* HEADER_POLY1305_H */
