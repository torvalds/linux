/*	$OpenBSD: sha_internal.h,v 1.3 2023/04/25 15:47:29 tb Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <openssl/sha.h>

#ifndef HEADER_SHA_INTERNAL_H
#define HEADER_SHA_INTERNAL_H

#define SHA512_224_DIGEST_LENGTH	28
#define SHA512_256_DIGEST_LENGTH	32

int SHA512_224_Init(SHA512_CTX *c);
int SHA512_224_Update(SHA512_CTX *c, const void *data, size_t len)
	__attribute__ ((__bounded__(__buffer__,2,3)));
int SHA512_224_Final(unsigned char *md, SHA512_CTX *c);

int SHA512_256_Init(SHA512_CTX *c);
int SHA512_256_Update(SHA512_CTX *c, const void *data, size_t len)
	__attribute__ ((__bounded__(__buffer__,2,3)));
int SHA512_256_Final(unsigned char *md, SHA512_CTX *c);

#endif
