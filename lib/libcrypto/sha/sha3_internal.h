/*	$OpenBSD: sha3_internal.h,v 1.16 2025/04/18 07:36:11 jsing Exp $	*/
/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Markku-Juhani O. Saarinen
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stddef.h>
#include <stdint.h>

#ifndef HEADER_SHA3_INTERNAL_H
#define HEADER_SHA3_INTERNAL_H

#define KECCAK_BIT_WIDTH	1600
#define KECCAK_BYTE_WIDTH	(KECCAK_BIT_WIDTH / 8)

#define SHA3_224_BIT_LENGTH	224
#define SHA3_224_BITRATE	(2 * SHA3_224_BIT_LENGTH)
#define SHA3_224_CAPACITY	(KECCAK_BIT_WIDTH - SHA3_224_BITRATE)
#define SHA3_224_BLOCK_SIZE	(SHA3_224_CAPACITY / 8)
#define SHA3_224_DIGEST_LENGTH	(SHA3_224_BIT_LENGTH / 8)

#define SHA3_256_BIT_LENGTH	256
#define SHA3_256_BITRATE	(2 * SHA3_256_BIT_LENGTH)
#define SHA3_256_CAPACITY	(KECCAK_BIT_WIDTH - SHA3_256_BITRATE)
#define SHA3_256_BLOCK_SIZE	(SHA3_256_CAPACITY / 8)
#define SHA3_256_DIGEST_LENGTH	(SHA3_256_BIT_LENGTH / 8)

#define SHA3_384_BIT_LENGTH	384
#define SHA3_384_BITRATE	(2 * SHA3_384_BIT_LENGTH)
#define SHA3_384_CAPACITY	(KECCAK_BIT_WIDTH - SHA3_384_BITRATE)
#define SHA3_384_BLOCK_SIZE	(SHA3_384_CAPACITY / 8)
#define SHA3_384_DIGEST_LENGTH	(SHA3_384_BIT_LENGTH / 8)

#define SHA3_512_BIT_LENGTH	512
#define SHA3_512_BITRATE	(2 * SHA3_512_BIT_LENGTH)
#define SHA3_512_CAPACITY	(KECCAK_BIT_WIDTH - SHA3_512_BITRATE)
#define SHA3_512_BLOCK_SIZE	(SHA3_512_CAPACITY / 8)
#define SHA3_512_DIGEST_LENGTH	(SHA3_512_BIT_LENGTH / 8)

typedef struct sha3_ctx_st {
	union {
		uint8_t b[200];		/* State as 8 bit bytes. */
		uint64_t q[25];		/* State as 64 bit words. */
	} state;
	size_t pt;
	size_t rsize;
	size_t mdlen;
} sha3_ctx;

int sha3_init(sha3_ctx *ctx, int mdlen);
int sha3_update(sha3_ctx *ctx, const void *data, size_t len);
int sha3_final(void *md, sha3_ctx *ctx);

/* SHAKE128 and SHAKE256 extensible-output functions. */
#define shake128_init(ctx) sha3_init((ctx), 16)
#define shake256_init(ctx) sha3_init((ctx), 32)
#define shake_update sha3_update

void shake_xof(sha3_ctx *ctx);
void shake_out(sha3_ctx *ctx, void *out, size_t len);

#endif
