// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-224, SHA-256, HMAC-SHA224, and HMAC-SHA256 library functions
 *
 * Copyright (c) Jean-Luc Cooke <jlcooke@certainkey.com>
 * Copyright (c) Andrew McDonald <andrew@mcdonald.org.uk>
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright (c) 2014 Red Hat Inc.
 * Copyright 2025 Google LLC
 */

#include <crypto/hmac.h>
#include <crypto/sha2.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/wordpart.h>

static const struct sha256_block_state sha224_iv = {
	.h = {
		SHA224_H0, SHA224_H1, SHA224_H2, SHA224_H3,
		SHA224_H4, SHA224_H5, SHA224_H6, SHA224_H7,
	},
};

static const struct sha256_ctx initial_sha256_ctx = {
	.ctx = {
		.state = {
			.h = {
				SHA256_H0, SHA256_H1, SHA256_H2, SHA256_H3,
				SHA256_H4, SHA256_H5, SHA256_H6, SHA256_H7,
			},
		},
		.bytecount = 0,
	},
};

#define sha256_iv (initial_sha256_ctx.ctx.state)

static const u32 sha256_K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
	0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
	0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
	0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
	0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
	0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define Ch(x, y, z) ((z) ^ ((x) & ((y) ^ (z))))
#define Maj(x, y, z) (((x) & (y)) | ((z) & ((x) | (y))))
#define e0(x) (ror32((x), 2) ^ ror32((x), 13) ^ ror32((x), 22))
#define e1(x) (ror32((x), 6) ^ ror32((x), 11) ^ ror32((x), 25))
#define s0(x) (ror32((x), 7) ^ ror32((x), 18) ^ ((x) >> 3))
#define s1(x) (ror32((x), 17) ^ ror32((x), 19) ^ ((x) >> 10))

static inline void LOAD_OP(int I, u32 *W, const u8 *input)
{
	W[I] = get_unaligned_be32((__u32 *)input + I);
}

static inline void BLEND_OP(int I, u32 *W)
{
	W[I] = s1(W[I - 2]) + W[I - 7] + s0(W[I - 15]) + W[I - 16];
}

#define SHA256_ROUND(i, a, b, c, d, e, f, g, h)                    \
	do {                                                       \
		u32 t1, t2;                                        \
		t1 = h + e1(e) + Ch(e, f, g) + sha256_K[i] + W[i]; \
		t2 = e0(a) + Maj(a, b, c);                         \
		d += t1;                                           \
		h = t1 + t2;                                       \
	} while (0)

static void sha256_block_generic(struct sha256_block_state *state,
				 const u8 *input, u32 W[64])
{
	u32 a, b, c, d, e, f, g, h;
	int i;

	/* load the input */
	for (i = 0; i < 16; i += 8) {
		LOAD_OP(i + 0, W, input);
		LOAD_OP(i + 1, W, input);
		LOAD_OP(i + 2, W, input);
		LOAD_OP(i + 3, W, input);
		LOAD_OP(i + 4, W, input);
		LOAD_OP(i + 5, W, input);
		LOAD_OP(i + 6, W, input);
		LOAD_OP(i + 7, W, input);
	}

	/* now blend */
	for (i = 16; i < 64; i += 8) {
		BLEND_OP(i + 0, W);
		BLEND_OP(i + 1, W);
		BLEND_OP(i + 2, W);
		BLEND_OP(i + 3, W);
		BLEND_OP(i + 4, W);
		BLEND_OP(i + 5, W);
		BLEND_OP(i + 6, W);
		BLEND_OP(i + 7, W);
	}

	/* load the state into our registers */
	a = state->h[0];
	b = state->h[1];
	c = state->h[2];
	d = state->h[3];
	e = state->h[4];
	f = state->h[5];
	g = state->h[6];
	h = state->h[7];

	/* now iterate */
	for (i = 0; i < 64; i += 8) {
		SHA256_ROUND(i + 0, a, b, c, d, e, f, g, h);
		SHA256_ROUND(i + 1, h, a, b, c, d, e, f, g);
		SHA256_ROUND(i + 2, g, h, a, b, c, d, e, f);
		SHA256_ROUND(i + 3, f, g, h, a, b, c, d, e);
		SHA256_ROUND(i + 4, e, f, g, h, a, b, c, d);
		SHA256_ROUND(i + 5, d, e, f, g, h, a, b, c);
		SHA256_ROUND(i + 6, c, d, e, f, g, h, a, b);
		SHA256_ROUND(i + 7, b, c, d, e, f, g, h, a);
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
sha256_blocks_generic(struct sha256_block_state *state,
		      const u8 *data, size_t nblocks)
{
	u32 W[64];

	do {
		sha256_block_generic(state, data, W);
		data += SHA256_BLOCK_SIZE;
	} while (--nblocks);

	memzero_explicit(W, sizeof(W));
}

#if defined(CONFIG_CRYPTO_LIB_SHA256_ARCH) && !defined(__DISABLE_EXPORTS)
#include "sha256.h" /* $(SRCARCH)/sha256.h */
#else
#define sha256_blocks sha256_blocks_generic
#endif

static void __sha256_init(struct __sha256_ctx *ctx,
			  const struct sha256_block_state *iv,
			  u64 initial_bytecount)
{
	ctx->state = *iv;
	ctx->bytecount = initial_bytecount;
}

void sha224_init(struct sha224_ctx *ctx)
{
	__sha256_init(&ctx->ctx, &sha224_iv, 0);
}
EXPORT_SYMBOL_GPL(sha224_init);

void sha256_init(struct sha256_ctx *ctx)
{
	__sha256_init(&ctx->ctx, &sha256_iv, 0);
}
EXPORT_SYMBOL_GPL(sha256_init);

void __sha256_update(struct __sha256_ctx *ctx, const u8 *data, size_t len)
{
	size_t partial = ctx->bytecount % SHA256_BLOCK_SIZE;

	ctx->bytecount += len;

	if (partial + len >= SHA256_BLOCK_SIZE) {
		size_t nblocks;

		if (partial) {
			size_t l = SHA256_BLOCK_SIZE - partial;

			memcpy(&ctx->buf[partial], data, l);
			data += l;
			len -= l;

			sha256_blocks(&ctx->state, ctx->buf, 1);
		}

		nblocks = len / SHA256_BLOCK_SIZE;
		len %= SHA256_BLOCK_SIZE;

		if (nblocks) {
			sha256_blocks(&ctx->state, data, nblocks);
			data += nblocks * SHA256_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(&ctx->buf[partial], data, len);
}
EXPORT_SYMBOL(__sha256_update);

static void __sha256_final(struct __sha256_ctx *ctx,
			   u8 *out, size_t digest_size)
{
	u64 bitcount = ctx->bytecount << 3;
	size_t partial = ctx->bytecount % SHA256_BLOCK_SIZE;

	ctx->buf[partial++] = 0x80;
	if (partial > SHA256_BLOCK_SIZE - 8) {
		memset(&ctx->buf[partial], 0, SHA256_BLOCK_SIZE - partial);
		sha256_blocks(&ctx->state, ctx->buf, 1);
		partial = 0;
	}
	memset(&ctx->buf[partial], 0, SHA256_BLOCK_SIZE - 8 - partial);
	*(__be64 *)&ctx->buf[SHA256_BLOCK_SIZE - 8] = cpu_to_be64(bitcount);
	sha256_blocks(&ctx->state, ctx->buf, 1);

	for (size_t i = 0; i < digest_size; i += 4)
		put_unaligned_be32(ctx->state.h[i / 4], out + i);
}

void sha224_final(struct sha224_ctx *ctx, u8 out[SHA224_DIGEST_SIZE])
{
	__sha256_final(&ctx->ctx, out, SHA224_DIGEST_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL(sha224_final);

void sha256_final(struct sha256_ctx *ctx, u8 out[SHA256_DIGEST_SIZE])
{
	__sha256_final(&ctx->ctx, out, SHA256_DIGEST_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL(sha256_final);

void sha224(const u8 *data, size_t len, u8 out[SHA224_DIGEST_SIZE])
{
	struct sha224_ctx ctx;

	sha224_init(&ctx);
	sha224_update(&ctx, data, len);
	sha224_final(&ctx, out);
}
EXPORT_SYMBOL(sha224);

void sha256(const u8 *data, size_t len, u8 out[SHA256_DIGEST_SIZE])
{
	struct sha256_ctx ctx;

	sha256_init(&ctx);
	sha256_update(&ctx, data, len);
	sha256_final(&ctx, out);
}
EXPORT_SYMBOL(sha256);

/*
 * Pre-boot environment (as indicated by __DISABLE_EXPORTS being defined)
 * doesn't need either HMAC support or interleaved hashing support
 */
#ifndef __DISABLE_EXPORTS

#ifndef sha256_finup_2x_arch
static bool sha256_finup_2x_arch(const struct __sha256_ctx *ctx,
				 const u8 *data1, const u8 *data2, size_t len,
				 u8 out1[SHA256_DIGEST_SIZE],
				 u8 out2[SHA256_DIGEST_SIZE])
{
	return false;
}
static bool sha256_finup_2x_is_optimized_arch(void)
{
	return false;
}
#endif

/* Sequential fallback implementation of sha256_finup_2x() */
static noinline_for_stack void sha256_finup_2x_sequential(
	const struct __sha256_ctx *ctx, const u8 *data1, const u8 *data2,
	size_t len, u8 out1[SHA256_DIGEST_SIZE], u8 out2[SHA256_DIGEST_SIZE])
{
	struct __sha256_ctx mut_ctx;

	mut_ctx = *ctx;
	__sha256_update(&mut_ctx, data1, len);
	__sha256_final(&mut_ctx, out1, SHA256_DIGEST_SIZE);

	mut_ctx = *ctx;
	__sha256_update(&mut_ctx, data2, len);
	__sha256_final(&mut_ctx, out2, SHA256_DIGEST_SIZE);
}

void sha256_finup_2x(const struct sha256_ctx *ctx, const u8 *data1,
		     const u8 *data2, size_t len, u8 out1[SHA256_DIGEST_SIZE],
		     u8 out2[SHA256_DIGEST_SIZE])
{
	if (ctx == NULL)
		ctx = &initial_sha256_ctx;

	if (likely(sha256_finup_2x_arch(&ctx->ctx, data1, data2, len, out1,
					out2)))
		return;
	sha256_finup_2x_sequential(&ctx->ctx, data1, data2, len, out1, out2);
}
EXPORT_SYMBOL_GPL(sha256_finup_2x);

bool sha256_finup_2x_is_optimized(void)
{
	return sha256_finup_2x_is_optimized_arch();
}
EXPORT_SYMBOL_GPL(sha256_finup_2x_is_optimized);

static void __hmac_sha256_preparekey(struct sha256_block_state *istate,
				     struct sha256_block_state *ostate,
				     const u8 *raw_key, size_t raw_key_len,
				     const struct sha256_block_state *iv)
{
	union {
		u8 b[SHA256_BLOCK_SIZE];
		unsigned long w[SHA256_BLOCK_SIZE / sizeof(unsigned long)];
	} derived_key = { 0 };

	if (unlikely(raw_key_len > SHA256_BLOCK_SIZE)) {
		if (iv == &sha224_iv)
			sha224(raw_key, raw_key_len, derived_key.b);
		else
			sha256(raw_key, raw_key_len, derived_key.b);
	} else {
		memcpy(derived_key.b, raw_key, raw_key_len);
	}

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_IPAD_VALUE);
	*istate = *iv;
	sha256_blocks(istate, derived_key.b, 1);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_OPAD_VALUE ^
						HMAC_IPAD_VALUE);
	*ostate = *iv;
	sha256_blocks(ostate, derived_key.b, 1);

	memzero_explicit(&derived_key, sizeof(derived_key));
}

void hmac_sha224_preparekey(struct hmac_sha224_key *key,
			    const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha256_preparekey(&key->key.istate, &key->key.ostate,
				 raw_key, raw_key_len, &sha224_iv);
}
EXPORT_SYMBOL_GPL(hmac_sha224_preparekey);

void hmac_sha256_preparekey(struct hmac_sha256_key *key,
			    const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha256_preparekey(&key->key.istate, &key->key.ostate,
				 raw_key, raw_key_len, &sha256_iv);
}
EXPORT_SYMBOL_GPL(hmac_sha256_preparekey);

void __hmac_sha256_init(struct __hmac_sha256_ctx *ctx,
			const struct __hmac_sha256_key *key)
{
	__sha256_init(&ctx->sha_ctx, &key->istate, SHA256_BLOCK_SIZE);
	ctx->ostate = key->ostate;
}
EXPORT_SYMBOL_GPL(__hmac_sha256_init);

void hmac_sha224_init_usingrawkey(struct hmac_sha224_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha256_preparekey(&ctx->ctx.sha_ctx.state, &ctx->ctx.ostate,
				 raw_key, raw_key_len, &sha224_iv);
	ctx->ctx.sha_ctx.bytecount = SHA256_BLOCK_SIZE;
}
EXPORT_SYMBOL_GPL(hmac_sha224_init_usingrawkey);

void hmac_sha256_init_usingrawkey(struct hmac_sha256_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len)
{
	__hmac_sha256_preparekey(&ctx->ctx.sha_ctx.state, &ctx->ctx.ostate,
				 raw_key, raw_key_len, &sha256_iv);
	ctx->ctx.sha_ctx.bytecount = SHA256_BLOCK_SIZE;
}
EXPORT_SYMBOL_GPL(hmac_sha256_init_usingrawkey);

static void __hmac_sha256_final(struct __hmac_sha256_ctx *ctx,
				u8 *out, size_t digest_size)
{
	/* Generate the padded input for the outer hash in ctx->sha_ctx.buf. */
	__sha256_final(&ctx->sha_ctx, ctx->sha_ctx.buf, digest_size);
	memset(&ctx->sha_ctx.buf[digest_size], 0,
	       SHA256_BLOCK_SIZE - digest_size);
	ctx->sha_ctx.buf[digest_size] = 0x80;
	*(__be32 *)&ctx->sha_ctx.buf[SHA256_BLOCK_SIZE - 4] =
		cpu_to_be32(8 * (SHA256_BLOCK_SIZE + digest_size));

	/* Compute the outer hash, which gives the HMAC value. */
	sha256_blocks(&ctx->ostate, ctx->sha_ctx.buf, 1);
	for (size_t i = 0; i < digest_size; i += 4)
		put_unaligned_be32(ctx->ostate.h[i / 4], out + i);

	memzero_explicit(ctx, sizeof(*ctx));
}

void hmac_sha224_final(struct hmac_sha224_ctx *ctx,
		       u8 out[SHA224_DIGEST_SIZE])
{
	__hmac_sha256_final(&ctx->ctx, out, SHA224_DIGEST_SIZE);
}
EXPORT_SYMBOL_GPL(hmac_sha224_final);

void hmac_sha256_final(struct hmac_sha256_ctx *ctx,
		       u8 out[SHA256_DIGEST_SIZE])
{
	__hmac_sha256_final(&ctx->ctx, out, SHA256_DIGEST_SIZE);
}
EXPORT_SYMBOL_GPL(hmac_sha256_final);

void hmac_sha224(const struct hmac_sha224_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA224_DIGEST_SIZE])
{
	struct hmac_sha224_ctx ctx;

	hmac_sha224_init(&ctx, key);
	hmac_sha224_update(&ctx, data, data_len);
	hmac_sha224_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha224);

void hmac_sha256(const struct hmac_sha256_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA256_DIGEST_SIZE])
{
	struct hmac_sha256_ctx ctx;

	hmac_sha256_init(&ctx, key);
	hmac_sha256_update(&ctx, data, data_len);
	hmac_sha256_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha256);

void hmac_sha224_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA224_DIGEST_SIZE])
{
	struct hmac_sha224_ctx ctx;

	hmac_sha224_init_usingrawkey(&ctx, raw_key, raw_key_len);
	hmac_sha224_update(&ctx, data, data_len);
	hmac_sha224_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha224_usingrawkey);

void hmac_sha256_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA256_DIGEST_SIZE])
{
	struct hmac_sha256_ctx ctx;

	hmac_sha256_init_usingrawkey(&ctx, raw_key, raw_key_len);
	hmac_sha256_update(&ctx, data, data_len);
	hmac_sha256_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_sha256_usingrawkey);
#endif /* !__DISABLE_EXPORTS */

#ifdef sha256_mod_init_arch
static int __init sha256_mod_init(void)
{
	sha256_mod_init_arch();
	return 0;
}
subsys_initcall(sha256_mod_init);

static void __exit sha256_mod_exit(void)
{
}
module_exit(sha256_mod_exit);
#endif

MODULE_DESCRIPTION("SHA-224, SHA-256, HMAC-SHA224, and HMAC-SHA256 library functions");
MODULE_LICENSE("GPL");
