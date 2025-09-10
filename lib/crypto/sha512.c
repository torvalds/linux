// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-384, SHA-512, HMAC-SHA384, and HMAC-SHA512 library functions
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2003 Kyle McMartin <kyle@debian.org>
 * Copyright 2025 Google LLC
 */

#include <crypto/hmac.h>
#include <crypto/sha2.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/overflow.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/wordpart.h>

static const struct sha512_block_state sha384_iv = {
	.h = {
		SHA384_H0, SHA384_H1, SHA384_H2, SHA384_H3,
		SHA384_H4, SHA384_H5, SHA384_H6, SHA384_H7,
	},
};

static const struct sha512_block_state sha512_iv = {
	.h = {
		SHA512_H0, SHA512_H1, SHA512_H2, SHA512_H3,
		SHA512_H4, SHA512_H5, SHA512_H6, SHA512_H7,
	},
};

static const u64 sha512_K[80] = {
	0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL,
	0xe9b5dba58189dbbcULL, 0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL,
	0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL, 0xd807aa98a3030242ULL,
	0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
	0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL,
	0xc19bf174cf692694ULL, 0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL,
	0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL, 0x2de92c6f592b0275ULL,
	0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
	0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL,
	0xbf597fc7beef0ee4ULL, 0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL,
	0x06ca6351e003826fULL, 0x142929670a0e6e70ULL, 0x27b70a8546d22ffcULL,
	0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
	0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL,
	0x92722c851482353bULL, 0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL,
	0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL, 0xd192e819d6ef5218ULL,
	0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
	0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL,
	0x34b0bcb5e19b48a8ULL, 0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL,
	0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL, 0x748f82ee5defb2fcULL,
	0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
	0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL,
	0xc67178f2e372532bULL, 0xca273eceea26619cULL, 0xd186b8c721c0c207ULL,
	0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL, 0x06f067aa72176fbaULL,
	0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
	0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL,
	0x431d67c49c100d4cULL, 0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL,
	0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL,
};

#define Ch(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define Maj(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define e0(x) (ror64((x), 28) ^ ror64((x), 34) ^ ror64((x), 39))
#define e1(x) (ror64((x), 14) ^ ror64((x), 18) ^ ror64((x), 41))
#define s0(x) (ror64((x), 1) ^ ror64((x), 8) ^ ((x) >> 7))
#define s1(x) (ror64((x), 19) ^ ror64((x), 61) ^ ((x) >> 6))

static void sha512_block_generic(struct sha512_block_state *state,
				 const u8 *data)
{
	u64 a = state->h[0];
	u64 b = state->h[1];
	u64 c = state->h[2];
	u64 d = state->h[3];
	u64 e = state->h[4];
	u64 f = state->h[5];
	u64 g = state->h[6];
	u64 h = state->h[7];
	u64 t1, t2;
	u64 W[16];

	for (int j = 0; j < 16; j++)
		W[j] = get_unaligned_be64(data + j * sizeof(u64));

	for (int i = 0; i < 80; i += 8) {
		if ((i & 15) == 0 && i != 0) {
			for (int j = 0; j < 16; j++) {
				W[j & 15] += s1(W[(j - 2) & 15]) +
					     W[(j - 7) & 15] +
					     s0(W[(j - 15) & 15]);
			}
		}
		t1 = h + e1(e) + Ch(e, f, g) + sha512_K[i]   + W[(i & 15)];
		t2 = e0(a) + Maj(a, b, c);    d += t1;    h = t1 + t2;
		t1 = g + e1(d) + Ch(d, e, f) + sha512_K[i+1] + W[(i & 15) + 1];
		t2 = e0(h) + Maj(h, a, b);    c += t1;    g = t1 + t2;
		t1 = f + e1(c) + Ch(c, d, e) + sha512_K[i+2] + W[(i & 15) + 2];
		t2 = e0(g) + Maj(g, h, a);    b += t1;    f = t1 + t2;
		t1 = e + e1(b) + Ch(b, c, d) + sha512_K[i+3] + W[(i & 15) + 3];
		t2 = e0(f) + Maj(f, g, h);    a += t1;    e = t1 + t2;
		t1 = d + e1(a) + Ch(a, b, c) + sha512_K[i+4] + W[(i & 15) + 4];
		t2 = e0(e) + Maj(e, f, g);    h += t1;    d = t1 + t2;
		t1 = c + e1(h) + Ch(h, a, b) + sha512_K[i+5] + W[(i & 15) + 5];
		t2 = e0(d) + Maj(d, e, f);    g += t1;    c = t1 + t2;
		t1 = b + e1(g) + Ch(g, h, a) + sha512_K[i+6] + W[(i & 15) + 6];
		t2 = e0(c) + Maj(c, d, e);    f += t1;    b = t1 + t2;
		t1 = a + e1(f) + Ch(f, g, h) + sha512_K[i+7] + W[(i & 15) + 7];
		t2 = e0(b) + Maj(b, c, d);    e += t1;    a = t1 + t2;
	}

	state->h[0] += a;
	state->h[1] += b;
	state->h[2] += c;
	state->h[3] += d;
	state->h[4] += e;
	state->h[5] += f;
	state->h[6] += g;
	state->h[7] += h;
}

static void __maybe_unused
sha512_blocks_generic(struct sha512_block_state *state,
		      const u8 *data, size_t nblocks)
{
	do {
		sha512_block_generic(state, data);
		data += SHA512_BLOCK_SIZE;
	} while (--nblocks);
}

#ifdef CONFIG_CRYPTO_LIB_SHA512_ARCH
#include "sha512.h" /* $(SRCARCH)/sha512.h */
#else
#define sha512_blocks sha512_blocks_generic
#endif

static void __sha512_init(struct __sha512_ctx *ctx,
			  const struct sha512_block_state *iv,
			  u64 initial_bytecount)
{
	ctx->state = *iv;
	ctx->bytecount_lo = initial_bytecount;
	ctx->bytecount_hi = 0;
}

void sha384_init(struct sha384_ctx *ctx)
{
	__sha512_init(&ctx->ctx, &sha384_iv, 0);
}
EXPORT_SYMBOL_GPL(sha384_init);

void sha512_init(struct sha512_ctx *ctx)
{
	__sha512_init(&ctx->ctx, &sha512_iv, 0);
}
EXPORT_SYMBOL_GPL(sha512_init);

void __sha512_update(struct __sha512_ctx *ctx, const u8 *data, size_t len)
{
	size_t partial = ctx->bytecount_lo % SHA512_BLOCK_SIZE;

	if (check_add_overflow(ctx->bytecount_lo, len, &ctx->bytecount_lo))
		ctx->bytecount_hi++;

	if (partial + len >= SHA512_BLOCK_SIZE) {
		size_t nblocks;

		if (partial) {
			size_t l = SHA512_BLOCK_SIZE - partial;

			memcpy(&ctx->buf[partial], data, l);
			data += l;
			len -= l;

			sha512_blocks(&ctx->state, ctx->buf, 1);
		}

		nblocks = len / SHA512_BLOCK_SIZE;
		len %= SHA512_BLOCK_SIZE;

		if (nblocks) {
			sha512_blocks(&ctx->state, data, nblocks);
			data += nblocks * SHA512_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(&ctx->buf[partial], data, len);
}
EXPORT_SYMBOL_GPL(__sha512_update);

static void __sha512_final(struct __sha512_ctx *ctx,
			   u8 *out, size_t digest_size)
{
	u64 bitcount_hi = (ctx->bytecount_hi << 3) | (ctx->bytecount_lo >> 61);
	u64 bitcount_lo = ctx->bytecount_lo << 3;
	size_t partial = ctx->bytecount_lo % SHA512_BLOCK_SIZE;

	ctx->buf[partial++] = 0x80;
	if (partial > SHA512_BLOCK_SIZE - 16) {
		memset(&ctx->buf[partial], 0, SHA512_BLOCK_SIZE - partial);
		sha512_blocks(&ctx->state, ctx->buf, 1);
		partial = 0;
	}
	memset(&ctx->buf[partial], 0, SHA512_BLOCK_SIZE - 16 - partial);
	*(__be64 *)&ctx->buf[SHA512_BLOCK_SIZE - 16] = cpu_to_be64(bitcount_hi);
	*(__be64 *)&ctx->buf[SHA512_BLOCK_SIZE - 8] = cpu_to_be64(bitcount_lo);
	sha512_blocks(&ctx->state, ctx->buf, 1);

	for (size_t i = 0; i < digest_size; i += 8)
		put_unaligned_be64(ctx->state.h[i / 8], out + i);
}

void sha384_final(struct sha384_ctx *ctx, u8 out[SHA384_DIGEST_SIZE])
{
	__sha512_final(&ctx->ctx, out, SHA384_DIGEST_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(sha384_final);

void sha512_final(struct sha512_ctx *ctx, u8 out[SHA512_DIGEST_SIZE])
{
	__sha512_final(&ctx->ctx, out, SHA512_DIGEST_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(sha512_final);

void sha384(const u8 *data, size_t len, u8 out[SHA384_DIGEST_SIZE])
{
	struct sha384_ctx ctx;

	sha384_init(&ctx);
	sha384_update(&ctx, data, len);
	sha384_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(sha384);

void sha512(const u8 *data, size_t len, u8 out[SHA512_DIGEST_SIZE])
{
	struct sha512_ctx ctx;

	sha512_init(&ctx);
	sha512_update(&ctx, data, len);
	sha512_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(sha512);

static void __hmac_sha512_preparekey(struct sha512_block_state *istate,
				     struct sha512_block_state *ostate,
				     const u8 *raw_key, size_t raw_key_len,
				     const struct sha512_block_state *iv)
{
	union {
		u8 b[SHA512_BLOCK_SIZE];
		unsigned long w[SHA512_BLOCK_SIZE / sizeof(unsigned long)];
	} derived_key = { 0 };

	if (unlikely(raw_key_len > SHA512_BLOCK_SIZE)) {
		if (iv == &sha384_iv)
			sha384(raw_key, raw_key_len, derived_key.b);
		else
			sha512(raw_key, raw_key_len, derived_key.b);
	} else {
		memcpy(derived_key.b, raw_key, raw_key_len);
	}

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_IPAD_VALUE);
	*istate = *iv;
	sha512_blocks(istate, derived_key.b, 1);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_OPAD_VALUE ^
						HMAC_IPAD_VALUE);
	*ostate = *iv;
	sha512_blocks(ostate, derived_key.b, 1);

	memzero_explicit(&derived_key, sizeof(derived_key));
}

void hmac_sha384_preparekey(struct hmac_sha384_key *key,
			    const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha512_preparekey(&key->key.istate, &key->key.ostate,
				 raw_key, raw_key_len, &sha384_iv);
}
EXPORT_SYMBOL_GPL(hmac_sha384_preparekey);

void hmac_sha512_preparekey(struct hmac_sha512_key *key,
			    const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha512_preparekey(&key->key.istate, &key->key.ostate,
				 raw_key, raw_key_len, &sha512_iv);
}
EXPORT_SYMBOL_GPL(hmac_sha512_preparekey);

void __hmac_sha512_init(struct __hmac_sha512_ctx *ctx,
			const struct __hmac_sha512_key *key)
{
	__sha512_init(&ctx->sha_ctx, &key->istate, SHA512_BLOCK_SIZE);
	ctx->ostate = key->ostate;
}
EXPORT_SYMBOL_GPL(__hmac_sha512_init);

void hmac_sha384_init_usingrawkey(struct hmac_sha384_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha512_preparekey(&ctx->ctx.sha_ctx.state, &ctx->ctx.ostate,
				 raw_key, raw_key_len, &sha384_iv);
	ctx->ctx.sha_ctx.bytecount_lo = SHA512_BLOCK_SIZE;
	ctx->ctx.sha_ctx.bytecount_hi = 0;
}
EXPORT_SYMBOL_GPL(hmac_sha384_init_usingrawkey);

void hmac_sha512_init_usingrawkey(struct hmac_sha512_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha512_preparekey(&ctx->ctx.sha_ctx.state, &ctx->ctx.ostate,
				 raw_key, raw_key_len, &sha512_iv);
	ctx->ctx.sha_ctx.bytecount_lo = SHA512_BLOCK_SIZE;
	ctx->ctx.sha_ctx.bytecount_hi = 0;
}
EXPORT_SYMBOL_GPL(hmac_sha512_init_usingrawkey);

static void __hmac_sha512_final(struct __hmac_sha512_ctx *ctx,
				u8 *out, size_t digest_size)
{
	/* Generate the padded input for the outer hash in ctx->sha_ctx.buf. */
	__sha512_final(&ctx->sha_ctx, ctx->sha_ctx.buf, digest_size);
	memset(&ctx->sha_ctx.buf[digest_size], 0,
	       SHA512_BLOCK_SIZE - digest_size);
	ctx->sha_ctx.buf[digest_size] = 0x80;
	*(__be32 *)&ctx->sha_ctx.buf[SHA512_BLOCK_SIZE - 4] =
		cpu_to_be32(8 * (SHA512_BLOCK_SIZE + digest_size));

	/* Compute the outer hash, which gives the HMAC value. */
	sha512_blocks(&ctx->ostate, ctx->sha_ctx.buf, 1);
	for (size_t i = 0; i < digest_size; i += 8)
		put_unaligned_be64(ctx->ostate.h[i / 8], out + i);

	memzero_explicit(ctx, sizeof(*ctx));
}

void hmac_sha384_final(struct hmac_sha384_ctx *ctx,
		       u8 out[SHA384_DIGEST_SIZE])
{
	__hmac_sha512_final(&ctx->ctx, out, SHA384_DIGEST_SIZE);
}
EXPORT_SYMBOL_GPL(hmac_sha384_final);

void hmac_sha512_final(struct hmac_sha512_ctx *ctx,
		       u8 out[SHA512_DIGEST_SIZE])
{
	__hmac_sha512_final(&ctx->ctx, out, SHA512_DIGEST_SIZE);
}
EXPORT_SYMBOL_GPL(hmac_sha512_final);

void hmac_sha384(const struct hmac_sha384_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA384_DIGEST_SIZE])
{
	struct hmac_sha384_ctx ctx;

	hmac_sha384_init(&ctx, key);
	hmac_sha384_update(&ctx, data, data_len);
	hmac_sha384_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha384);

void hmac_sha512(const struct hmac_sha512_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA512_DIGEST_SIZE])
{
	struct hmac_sha512_ctx ctx;

	hmac_sha512_init(&ctx, key);
	hmac_sha512_update(&ctx, data, data_len);
	hmac_sha512_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha512);

void hmac_sha384_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA384_DIGEST_SIZE])
{
	struct hmac_sha384_ctx ctx;

	hmac_sha384_init_usingrawkey(&ctx, raw_key, raw_key_len);
	hmac_sha384_update(&ctx, data, data_len);
	hmac_sha384_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha384_usingrawkey);

void hmac_sha512_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA512_DIGEST_SIZE])
{
	struct hmac_sha512_ctx ctx;

	hmac_sha512_init_usingrawkey(&ctx, raw_key, raw_key_len);
	hmac_sha512_update(&ctx, data, data_len);
	hmac_sha512_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha512_usingrawkey);

#ifdef sha512_mod_init_arch
static int __init sha512_mod_init(void)
{
	sha512_mod_init_arch();
	return 0;
}
subsys_initcall(sha512_mod_init);

static void __exit sha512_mod_exit(void)
{
}
module_exit(sha512_mod_exit);
#endif

MODULE_DESCRIPTION("SHA-384, SHA-512, HMAC-SHA384, and HMAC-SHA512 library functions");
MODULE_LICENSE("GPL");
