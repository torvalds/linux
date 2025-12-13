// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MD5 and HMAC-MD5 library functions
 *
 * md5_block_generic() is derived from cryptoapi implementation, originally
 * based on the public domain implementation written by Colin Plumb in 1993.
 *
 * Copyright (c) Cryptoapi developers.
 * Copyright (c) 2002 James Morris <jmorris@intercode.com.au>
 * Copyright 2025 Google LLC
 */

#include <crypto/hmac.h>
#include <crypto/md5.h>
#include <linux/export.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>
#include <linux/wordpart.h>

static const struct md5_block_state md5_iv = {
	.h = { MD5_H0, MD5_H1, MD5_H2, MD5_H3 },
};

#define F1(x, y, z) (z ^ (x & (y ^ z)))
#define F2(x, y, z) F1(z, x, y)
#define F3(x, y, z) (x ^ y ^ z)
#define F4(x, y, z) (y ^ (x | ~z))

#define MD5STEP(f, w, x, y, z, in, s) \
	(w += f(x, y, z) + in, w = (w << s | w >> (32 - s)) + x)

static void md5_block_generic(struct md5_block_state *state,
			      const u8 data[MD5_BLOCK_SIZE])
{
	u32 in[MD5_BLOCK_WORDS];
	u32 a, b, c, d;

	memcpy(in, data, MD5_BLOCK_SIZE);
	le32_to_cpu_array(in, ARRAY_SIZE(in));

	a = state->h[0];
	b = state->h[1];
	c = state->h[2];
	d = state->h[3];

	MD5STEP(F1, a, b, c, d, in[0] + 0xd76aa478, 7);
	MD5STEP(F1, d, a, b, c, in[1] + 0xe8c7b756, 12);
	MD5STEP(F1, c, d, a, b, in[2] + 0x242070db, 17);
	MD5STEP(F1, b, c, d, a, in[3] + 0xc1bdceee, 22);
	MD5STEP(F1, a, b, c, d, in[4] + 0xf57c0faf, 7);
	MD5STEP(F1, d, a, b, c, in[5] + 0x4787c62a, 12);
	MD5STEP(F1, c, d, a, b, in[6] + 0xa8304613, 17);
	MD5STEP(F1, b, c, d, a, in[7] + 0xfd469501, 22);
	MD5STEP(F1, a, b, c, d, in[8] + 0x698098d8, 7);
	MD5STEP(F1, d, a, b, c, in[9] + 0x8b44f7af, 12);
	MD5STEP(F1, c, d, a, b, in[10] + 0xffff5bb1, 17);
	MD5STEP(F1, b, c, d, a, in[11] + 0x895cd7be, 22);
	MD5STEP(F1, a, b, c, d, in[12] + 0x6b901122, 7);
	MD5STEP(F1, d, a, b, c, in[13] + 0xfd987193, 12);
	MD5STEP(F1, c, d, a, b, in[14] + 0xa679438e, 17);
	MD5STEP(F1, b, c, d, a, in[15] + 0x49b40821, 22);

	MD5STEP(F2, a, b, c, d, in[1] + 0xf61e2562, 5);
	MD5STEP(F2, d, a, b, c, in[6] + 0xc040b340, 9);
	MD5STEP(F2, c, d, a, b, in[11] + 0x265e5a51, 14);
	MD5STEP(F2, b, c, d, a, in[0] + 0xe9b6c7aa, 20);
	MD5STEP(F2, a, b, c, d, in[5] + 0xd62f105d, 5);
	MD5STEP(F2, d, a, b, c, in[10] + 0x02441453, 9);
	MD5STEP(F2, c, d, a, b, in[15] + 0xd8a1e681, 14);
	MD5STEP(F2, b, c, d, a, in[4] + 0xe7d3fbc8, 20);
	MD5STEP(F2, a, b, c, d, in[9] + 0x21e1cde6, 5);
	MD5STEP(F2, d, a, b, c, in[14] + 0xc33707d6, 9);
	MD5STEP(F2, c, d, a, b, in[3] + 0xf4d50d87, 14);
	MD5STEP(F2, b, c, d, a, in[8] + 0x455a14ed, 20);
	MD5STEP(F2, a, b, c, d, in[13] + 0xa9e3e905, 5);
	MD5STEP(F2, d, a, b, c, in[2] + 0xfcefa3f8, 9);
	MD5STEP(F2, c, d, a, b, in[7] + 0x676f02d9, 14);
	MD5STEP(F2, b, c, d, a, in[12] + 0x8d2a4c8a, 20);

	MD5STEP(F3, a, b, c, d, in[5] + 0xfffa3942, 4);
	MD5STEP(F3, d, a, b, c, in[8] + 0x8771f681, 11);
	MD5STEP(F3, c, d, a, b, in[11] + 0x6d9d6122, 16);
	MD5STEP(F3, b, c, d, a, in[14] + 0xfde5380c, 23);
	MD5STEP(F3, a, b, c, d, in[1] + 0xa4beea44, 4);
	MD5STEP(F3, d, a, b, c, in[4] + 0x4bdecfa9, 11);
	MD5STEP(F3, c, d, a, b, in[7] + 0xf6bb4b60, 16);
	MD5STEP(F3, b, c, d, a, in[10] + 0xbebfbc70, 23);
	MD5STEP(F3, a, b, c, d, in[13] + 0x289b7ec6, 4);
	MD5STEP(F3, d, a, b, c, in[0] + 0xeaa127fa, 11);
	MD5STEP(F3, c, d, a, b, in[3] + 0xd4ef3085, 16);
	MD5STEP(F3, b, c, d, a, in[6] + 0x04881d05, 23);
	MD5STEP(F3, a, b, c, d, in[9] + 0xd9d4d039, 4);
	MD5STEP(F3, d, a, b, c, in[12] + 0xe6db99e5, 11);
	MD5STEP(F3, c, d, a, b, in[15] + 0x1fa27cf8, 16);
	MD5STEP(F3, b, c, d, a, in[2] + 0xc4ac5665, 23);

	MD5STEP(F4, a, b, c, d, in[0] + 0xf4292244, 6);
	MD5STEP(F4, d, a, b, c, in[7] + 0x432aff97, 10);
	MD5STEP(F4, c, d, a, b, in[14] + 0xab9423a7, 15);
	MD5STEP(F4, b, c, d, a, in[5] + 0xfc93a039, 21);
	MD5STEP(F4, a, b, c, d, in[12] + 0x655b59c3, 6);
	MD5STEP(F4, d, a, b, c, in[3] + 0x8f0ccc92, 10);
	MD5STEP(F4, c, d, a, b, in[10] + 0xffeff47d, 15);
	MD5STEP(F4, b, c, d, a, in[1] + 0x85845dd1, 21);
	MD5STEP(F4, a, b, c, d, in[8] + 0x6fa87e4f, 6);
	MD5STEP(F4, d, a, b, c, in[15] + 0xfe2ce6e0, 10);
	MD5STEP(F4, c, d, a, b, in[6] + 0xa3014314, 15);
	MD5STEP(F4, b, c, d, a, in[13] + 0x4e0811a1, 21);
	MD5STEP(F4, a, b, c, d, in[4] + 0xf7537e82, 6);
	MD5STEP(F4, d, a, b, c, in[11] + 0xbd3af235, 10);
	MD5STEP(F4, c, d, a, b, in[2] + 0x2ad7d2bb, 15);
	MD5STEP(F4, b, c, d, a, in[9] + 0xeb86d391, 21);

	state->h[0] += a;
	state->h[1] += b;
	state->h[2] += c;
	state->h[3] += d;
}

static void __maybe_unused md5_blocks_generic(struct md5_block_state *state,
					      const u8 *data, size_t nblocks)
{
	do {
		md5_block_generic(state, data);
		data += MD5_BLOCK_SIZE;
	} while (--nblocks);
}

#ifdef CONFIG_CRYPTO_LIB_MD5_ARCH
#include "md5.h" /* $(SRCARCH)/md5.h */
#else
#define md5_blocks md5_blocks_generic
#endif

void md5_init(struct md5_ctx *ctx)
{
	ctx->state = md5_iv;
	ctx->bytecount = 0;
}
EXPORT_SYMBOL_GPL(md5_init);

void md5_update(struct md5_ctx *ctx, const u8 *data, size_t len)
{
	size_t partial = ctx->bytecount % MD5_BLOCK_SIZE;

	ctx->bytecount += len;

	if (partial + len >= MD5_BLOCK_SIZE) {
		size_t nblocks;

		if (partial) {
			size_t l = MD5_BLOCK_SIZE - partial;

			memcpy(&ctx->buf[partial], data, l);
			data += l;
			len -= l;

			md5_blocks(&ctx->state, ctx->buf, 1);
		}

		nblocks = len / MD5_BLOCK_SIZE;
		len %= MD5_BLOCK_SIZE;

		if (nblocks) {
			md5_blocks(&ctx->state, data, nblocks);
			data += nblocks * MD5_BLOCK_SIZE;
		}
		partial = 0;
	}
	if (len)
		memcpy(&ctx->buf[partial], data, len);
}
EXPORT_SYMBOL_GPL(md5_update);

static void __md5_final(struct md5_ctx *ctx, u8 out[MD5_DIGEST_SIZE])
{
	u64 bitcount = ctx->bytecount << 3;
	size_t partial = ctx->bytecount % MD5_BLOCK_SIZE;

	ctx->buf[partial++] = 0x80;
	if (partial > MD5_BLOCK_SIZE - 8) {
		memset(&ctx->buf[partial], 0, MD5_BLOCK_SIZE - partial);
		md5_blocks(&ctx->state, ctx->buf, 1);
		partial = 0;
	}
	memset(&ctx->buf[partial], 0, MD5_BLOCK_SIZE - 8 - partial);
	*(__le64 *)&ctx->buf[MD5_BLOCK_SIZE - 8] = cpu_to_le64(bitcount);
	md5_blocks(&ctx->state, ctx->buf, 1);

	cpu_to_le32_array(ctx->state.h, ARRAY_SIZE(ctx->state.h));
	memcpy(out, ctx->state.h, MD5_DIGEST_SIZE);
}

void md5_final(struct md5_ctx *ctx, u8 out[MD5_DIGEST_SIZE])
{
	__md5_final(ctx, out);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(md5_final);

void md5(const u8 *data, size_t len, u8 out[MD5_DIGEST_SIZE])
{
	struct md5_ctx ctx;

	md5_init(&ctx);
	md5_update(&ctx, data, len);
	md5_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(md5);

static void __hmac_md5_preparekey(struct md5_block_state *istate,
				  struct md5_block_state *ostate,
				  const u8 *raw_key, size_t raw_key_len)
{
	union {
		u8 b[MD5_BLOCK_SIZE];
		unsigned long w[MD5_BLOCK_SIZE / sizeof(unsigned long)];
	} derived_key = { 0 };

	if (unlikely(raw_key_len > MD5_BLOCK_SIZE))
		md5(raw_key, raw_key_len, derived_key.b);
	else
		memcpy(derived_key.b, raw_key, raw_key_len);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_IPAD_VALUE);
	*istate = md5_iv;
	md5_blocks(istate, derived_key.b, 1);

	for (size_t i = 0; i < ARRAY_SIZE(derived_key.w); i++)
		derived_key.w[i] ^= REPEAT_BYTE(HMAC_OPAD_VALUE ^
						HMAC_IPAD_VALUE);
	*ostate = md5_iv;
	md5_blocks(ostate, derived_key.b, 1);

	memzero_explicit(&derived_key, sizeof(derived_key));
}

void hmac_md5_preparekey(struct hmac_md5_key *key,
			 const u8 *raw_key, size_t raw_key_len)
{
	__hmac_md5_preparekey(&key->istate, &key->ostate, raw_key, raw_key_len);
}
EXPORT_SYMBOL_GPL(hmac_md5_preparekey);

void hmac_md5_init(struct hmac_md5_ctx *ctx, const struct hmac_md5_key *key)
{
	ctx->hash_ctx.state = key->istate;
	ctx->hash_ctx.bytecount = MD5_BLOCK_SIZE;
	ctx->ostate = key->ostate;
}
EXPORT_SYMBOL_GPL(hmac_md5_init);

void hmac_md5_init_usingrawkey(struct hmac_md5_ctx *ctx,
			       const u8 *raw_key, size_t raw_key_len)
{
	__hmac_md5_preparekey(&ctx->hash_ctx.state, &ctx->ostate,
			      raw_key, raw_key_len);
	ctx->hash_ctx.bytecount = MD5_BLOCK_SIZE;
}
EXPORT_SYMBOL_GPL(hmac_md5_init_usingrawkey);

void hmac_md5_final(struct hmac_md5_ctx *ctx, u8 out[MD5_DIGEST_SIZE])
{
	/* Generate the padded input for the outer hash in ctx->hash_ctx.buf. */
	__md5_final(&ctx->hash_ctx, ctx->hash_ctx.buf);
	memset(&ctx->hash_ctx.buf[MD5_DIGEST_SIZE], 0,
	       MD5_BLOCK_SIZE - MD5_DIGEST_SIZE);
	ctx->hash_ctx.buf[MD5_DIGEST_SIZE] = 0x80;
	*(__le64 *)&ctx->hash_ctx.buf[MD5_BLOCK_SIZE - 8] =
		cpu_to_le64(8 * (MD5_BLOCK_SIZE + MD5_DIGEST_SIZE));

	/* Compute the outer hash, which gives the HMAC value. */
	md5_blocks(&ctx->ostate, ctx->hash_ctx.buf, 1);
	cpu_to_le32_array(ctx->ostate.h, ARRAY_SIZE(ctx->ostate.h));
	memcpy(out, ctx->ostate.h, MD5_DIGEST_SIZE);

	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(hmac_md5_final);

void hmac_md5(const struct hmac_md5_key *key,
	      const u8 *data, size_t data_len, u8 out[MD5_DIGEST_SIZE])
{
	struct hmac_md5_ctx ctx;

	hmac_md5_init(&ctx, key);
	hmac_md5_update(&ctx, data, data_len);
	hmac_md5_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_md5);

void hmac_md5_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			  const u8 *data, size_t data_len,
			  u8 out[MD5_DIGEST_SIZE])
{
	struct hmac_md5_ctx ctx;

	hmac_md5_init_usingrawkey(&ctx, raw_key, raw_key_len);
	hmac_md5_update(&ctx, data, data_len);
	hmac_md5_final(&ctx, out);
}
EXPORT_SYMBOL_GPL(hmac_md5_usingrawkey);

#ifdef md5_mod_init_arch
static int __init md5_mod_init(void)
{
	md5_mod_init_arch();
	return 0;
}
subsys_initcall(md5_mod_init);

static void __exit md5_mod_exit(void)
{
}
module_exit(md5_mod_exit);
#endif

MODULE_DESCRIPTION("MD5 and HMAC-MD5 library functions");
MODULE_LICENSE("GPL");
