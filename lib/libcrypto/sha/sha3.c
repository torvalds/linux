/*	$OpenBSD: sha3.c,v 1.20 2025/04/18 07:36:11 jsing Exp $	*/
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

#include <endian.h>
#include <string.h>

#include "crypto_internal.h"
#include "sha3_internal.h"

#define KECCAKF_ROUNDS 24

static const uint64_t sha3_keccakf_rndc[24] = {
	0x0000000000000001, 0x0000000000008082, 0x800000000000808a,
	0x8000000080008000, 0x000000000000808b, 0x0000000080000001,
	0x8000000080008081, 0x8000000000008009, 0x000000000000008a,
	0x0000000000000088, 0x0000000080008009, 0x000000008000000a,
	0x000000008000808b, 0x800000000000008b, 0x8000000000008089,
	0x8000000000008003, 0x8000000000008002, 0x8000000000000080,
	0x000000000000800a, 0x800000008000000a, 0x8000000080008081,
	0x8000000000008080, 0x0000000080000001, 0x8000000080008008
};
static const int sha3_keccakf_rotc[24] = {
	1,  3,  6,  10, 15, 21, 28, 36, 45, 55, 2,  14,
	27, 41, 56, 8,  25, 43, 62, 18, 39, 61, 20, 44
};
static const int sha3_keccakf_piln[24] = {
	10, 7,  11, 17, 18, 3, 5,  16, 8,  21, 24, 4,
	15, 23, 19, 13, 12, 2, 20, 14, 22, 9,  6,  1
};

static void
sha3_keccakf(uint64_t st[25])
{
	uint64_t t0, t1, bc[5];
	int i, j, r;

	for (i = 0; i < 25; i++)
		st[i] = le64toh(st[i]);

	for (r = 0; r < KECCAKF_ROUNDS; r++) {

		/* Theta */
		for (i = 0; i < 5; i++)
			bc[i] = st[i] ^ st[i + 5] ^ st[i + 10] ^ st[i + 15] ^ st[i + 20];

		for (i = 0; i < 5; i++) {
			t0 = bc[(i + 4) % 5] ^ crypto_rol_u64(bc[(i + 1) % 5], 1);
			for (j = 0; j < 25; j += 5)
				st[j + i] ^= t0;
		}

		/* Rho Pi */
		t0 = st[1];
		for (i = 0; i < 24; i++) {
			j = sha3_keccakf_piln[i];
			t1 = st[j];
			st[j] = crypto_rol_u64(t0, sha3_keccakf_rotc[i]);
			t0 = t1;
		}

		/* Chi */
		for (j = 0; j < 25; j += 5) {
			for (i = 0; i < 5; i++)
				bc[i] = st[j + i];
			for (i = 0; i < 5; i++)
				st[j + i] ^= (~bc[(i + 1) % 5]) & bc[(i + 2) % 5];
		}

		/* Iota */
		st[0] ^= sha3_keccakf_rndc[r];
	}

	for (i = 0; i < 25; i++)
		st[i] = htole64(st[i]);
}

int
sha3_init(sha3_ctx *ctx, int mdlen)
{
	if (mdlen < 0 || mdlen >= KECCAK_BYTE_WIDTH / 2)
		return 0;

	memset(ctx, 0, sizeof(*ctx));

	ctx->mdlen = mdlen;
	ctx->rsize = KECCAK_BYTE_WIDTH - 2 * mdlen;

	return 1;
}

int
sha3_update(sha3_ctx *ctx, const void *_data, size_t len)
{
	const uint8_t *data = _data;
	size_t i, j;

	j = ctx->pt;
	for (i = 0; i < len; i++) {
		ctx->state.b[j++] ^= data[i];
		if (j >= ctx->rsize) {
			sha3_keccakf(ctx->state.q);
			j = 0;
		}
	}
	ctx->pt = j;

	return 1;
}

int
sha3_final(void *_md, sha3_ctx *ctx)
{
	uint8_t *md = _md;
	int i;

	ctx->state.b[ctx->pt] ^= 0x06;
	ctx->state.b[ctx->rsize - 1] ^= 0x80;
	sha3_keccakf(ctx->state.q);

	for (i = 0; i < ctx->mdlen; i++)
		md[i] = ctx->state.b[i];

	return 1;
}

/* SHAKE128 and SHAKE256 extensible-output functionality. */
void
shake_xof(sha3_ctx *ctx)
{
	ctx->state.b[ctx->pt] ^= 0x1f;
	ctx->state.b[ctx->rsize - 1] ^= 0x80;
	sha3_keccakf(ctx->state.q);
	ctx->pt = 0;
}

void
shake_out(sha3_ctx *ctx, void *_out, size_t len)
{
	uint8_t *out = _out;
	size_t i, j;

	j = ctx->pt;
	for (i = 0; i < len; i++) {
		if (j >= ctx->rsize) {
			sha3_keccakf(ctx->state.q);
			j = 0;
		}
		out[i] = ctx->state.b[j++];
	}
	ctx->pt = j;
}
