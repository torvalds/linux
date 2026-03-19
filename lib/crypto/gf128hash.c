// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * GF(2^128) polynomial hashing: GHASH and POLYVAL
 *
 * Copyright 2025 Google LLC
 */

#include <crypto/gf128hash.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/unaligned.h>

/*
 * GHASH and POLYVAL are almost-XOR-universal hash functions.  They interpret
 * the message as the coefficients of a polynomial in the finite field GF(2^128)
 * and evaluate that polynomial at a secret point.
 *
 * Neither GHASH nor POLYVAL is a cryptographic hash function.  They should be
 * used only by algorithms that are specifically designed to use them.
 *
 * GHASH is the older variant, defined as part of GCM in NIST SP 800-38D
 * (https://nvlpubs.nist.gov/nistpubs/legacy/sp/nistspecialpublication800-38d.pdf).
 * GHASH is hard to implement directly, due to its backwards mapping between
 * bits and polynomial coefficients.  GHASH implementations typically pre and
 * post-process the inputs and outputs (mainly by byte-swapping) to convert the
 * GHASH computation into an equivalent computation over a different,
 * easier-to-use representation of GF(2^128).
 *
 * POLYVAL is a newer GF(2^128) polynomial hash, originally defined as part of
 * AES-GCM-SIV (https://datatracker.ietf.org/doc/html/rfc8452) and also used by
 * HCTR2 (https://eprint.iacr.org/2021/1441.pdf).  It uses that easier-to-use
 * field representation directly, eliminating the data conversion steps.
 *
 * This file provides library APIs for GHASH and POLYVAL.  These APIs can
 * delegate to either a generic implementation or an architecture-optimized
 * implementation.  Due to the mathematical relationship between GHASH and
 * POLYVAL, in some cases code for one is reused with the other.
 *
 * For the generic implementation, we don't use the traditional table approach
 * to GF(2^128) multiplication.  That approach is not constant-time and requires
 * a lot of memory.  Instead, we use a different approach which emulates
 * carryless multiplication using standard multiplications by spreading the data
 * bits apart using "holes".  This allows the carries to spill harmlessly.  This
 * approach is borrowed from BoringSSL, which in turn credits BearSSL's
 * documentation (https://bearssl.org/constanttime.html#ghash-for-gcm) for the
 * "holes" trick and a presentation by Shay Gueron
 * (https://crypto.stanford.edu/RealWorldCrypto/slides/gueron.pdf) for the
 * 256-bit => 128-bit reduction algorithm.
 */

#ifdef CONFIG_ARCH_SUPPORTS_INT128

/* Do a 64 x 64 => 128 bit carryless multiplication. */
static void clmul64(u64 a, u64 b, u64 *out_lo, u64 *out_hi)
{
	/*
	 * With 64-bit multiplicands and one term every 4 bits, there would be
	 * up to 64 / 4 = 16 one bits per column when each multiplication is
	 * written out as a series of additions in the schoolbook manner.
	 * Unfortunately, that doesn't work since the value 16 is 1 too large to
	 * fit in 4 bits.  Carries would sometimes overflow into the next term.
	 *
	 * Using one term every 5 bits would work.  However, that would cost
	 * 5 x 5 = 25 multiplications instead of 4 x 4 = 16.
	 *
	 * Instead, mask off 4 bits from one multiplicand, giving a max of 15
	 * one bits per column.  Then handle those 4 bits separately.
	 */
	u64 a0 = a & 0x1111111111111110;
	u64 a1 = a & 0x2222222222222220;
	u64 a2 = a & 0x4444444444444440;
	u64 a3 = a & 0x8888888888888880;

	u64 b0 = b & 0x1111111111111111;
	u64 b1 = b & 0x2222222222222222;
	u64 b2 = b & 0x4444444444444444;
	u64 b3 = b & 0x8888888888888888;

	/* Multiply the high 60 bits of @a by @b. */
	u128 c0 = (a0 * (u128)b0) ^ (a1 * (u128)b3) ^
		  (a2 * (u128)b2) ^ (a3 * (u128)b1);
	u128 c1 = (a0 * (u128)b1) ^ (a1 * (u128)b0) ^
		  (a2 * (u128)b3) ^ (a3 * (u128)b2);
	u128 c2 = (a0 * (u128)b2) ^ (a1 * (u128)b1) ^
		  (a2 * (u128)b0) ^ (a3 * (u128)b3);
	u128 c3 = (a0 * (u128)b3) ^ (a1 * (u128)b2) ^
		  (a2 * (u128)b1) ^ (a3 * (u128)b0);

	/* Multiply the low 4 bits of @a by @b. */
	u64 e0 = -(a & 1) & b;
	u64 e1 = -((a >> 1) & 1) & b;
	u64 e2 = -((a >> 2) & 1) & b;
	u64 e3 = -((a >> 3) & 1) & b;
	u64 extra_lo = e0 ^ (e1 << 1) ^ (e2 << 2) ^ (e3 << 3);
	u64 extra_hi = (e1 >> 63) ^ (e2 >> 62) ^ (e3 >> 61);

	/* Add all the intermediate products together. */
	*out_lo = (((u64)c0) & 0x1111111111111111) ^
		  (((u64)c1) & 0x2222222222222222) ^
		  (((u64)c2) & 0x4444444444444444) ^
		  (((u64)c3) & 0x8888888888888888) ^ extra_lo;
	*out_hi = (((u64)(c0 >> 64)) & 0x1111111111111111) ^
		  (((u64)(c1 >> 64)) & 0x2222222222222222) ^
		  (((u64)(c2 >> 64)) & 0x4444444444444444) ^
		  (((u64)(c3 >> 64)) & 0x8888888888888888) ^ extra_hi;
}

#else /* CONFIG_ARCH_SUPPORTS_INT128 */

/* Do a 32 x 32 => 64 bit carryless multiplication. */
static u64 clmul32(u32 a, u32 b)
{
	/*
	 * With 32-bit multiplicands and one term every 4 bits, there are up to
	 * 32 / 4 = 8 one bits per column when each multiplication is written
	 * out as a series of additions in the schoolbook manner.  The value 8
	 * fits in 4 bits, so the carries don't overflow into the next term.
	 */
	u32 a0 = a & 0x11111111;
	u32 a1 = a & 0x22222222;
	u32 a2 = a & 0x44444444;
	u32 a3 = a & 0x88888888;

	u32 b0 = b & 0x11111111;
	u32 b1 = b & 0x22222222;
	u32 b2 = b & 0x44444444;
	u32 b3 = b & 0x88888888;

	u64 c0 = (a0 * (u64)b0) ^ (a1 * (u64)b3) ^
		 (a2 * (u64)b2) ^ (a3 * (u64)b1);
	u64 c1 = (a0 * (u64)b1) ^ (a1 * (u64)b0) ^
		 (a2 * (u64)b3) ^ (a3 * (u64)b2);
	u64 c2 = (a0 * (u64)b2) ^ (a1 * (u64)b1) ^
		 (a2 * (u64)b0) ^ (a3 * (u64)b3);
	u64 c3 = (a0 * (u64)b3) ^ (a1 * (u64)b2) ^
		 (a2 * (u64)b1) ^ (a3 * (u64)b0);

	/* Add all the intermediate products together. */
	return (c0 & 0x1111111111111111) ^
	       (c1 & 0x2222222222222222) ^
	       (c2 & 0x4444444444444444) ^
	       (c3 & 0x8888888888888888);
}

/* Do a 64 x 64 => 128 bit carryless multiplication. */
static void clmul64(u64 a, u64 b, u64 *out_lo, u64 *out_hi)
{
	u32 a_lo = (u32)a;
	u32 a_hi = a >> 32;
	u32 b_lo = (u32)b;
	u32 b_hi = b >> 32;

	/* Karatsuba multiplication */
	u64 lo = clmul32(a_lo, b_lo);
	u64 hi = clmul32(a_hi, b_hi);
	u64 mi = clmul32(a_lo ^ a_hi, b_lo ^ b_hi) ^ lo ^ hi;

	*out_lo = lo ^ (mi << 32);
	*out_hi = hi ^ (mi >> 32);
}
#endif /* !CONFIG_ARCH_SUPPORTS_INT128 */

/* Compute @a = @a * @b * x^-128 in the POLYVAL field. */
static void __maybe_unused
polyval_mul_generic(struct polyval_elem *a, const struct polyval_elem *b)
{
	u64 c0, c1, c2, c3, mi0, mi1;

	/*
	 * Carryless-multiply @a by @b using Karatsuba multiplication.  Store
	 * the 256-bit product in @c0 (low) through @c3 (high).
	 */
	clmul64(le64_to_cpu(a->lo), le64_to_cpu(b->lo), &c0, &c1);
	clmul64(le64_to_cpu(a->hi), le64_to_cpu(b->hi), &c2, &c3);
	clmul64(le64_to_cpu(a->lo ^ a->hi), le64_to_cpu(b->lo ^ b->hi),
		&mi0, &mi1);
	mi0 ^= c0 ^ c2;
	mi1 ^= c1 ^ c3;
	c1 ^= mi0;
	c2 ^= mi1;

	/*
	 * Cancel out the low 128 bits of the product by adding multiples of
	 * G(x) = x^128 + x^127 + x^126 + x^121 + 1.  Do this in two steps, each
	 * of which cancels out 64 bits.  Note that we break G(x) into three
	 * parts: 1, x^64 * (x^63 + x^62 + x^57), and x^128 * 1.
	 */

	/*
	 * First, add G(x) times c0 as follows:
	 *
	 * (c0, c1, c2) = (0,
	 *                 c1 + (c0 * (x^63 + x^62 + x^57) mod x^64),
	 *		   c2 + c0 + floor((c0 * (x^63 + x^62 + x^57)) / x^64))
	 */
	c1 ^= (c0 << 63) ^ (c0 << 62) ^ (c0 << 57);
	c2 ^= c0 ^ (c0 >> 1) ^ (c0 >> 2) ^ (c0 >> 7);

	/*
	 * Second, add G(x) times the new c1:
	 *
	 * (c1, c2, c3) = (0,
	 *                 c2 + (c1 * (x^63 + x^62 + x^57) mod x^64),
	 *		   c3 + c1 + floor((c1 * (x^63 + x^62 + x^57)) / x^64))
	 */
	c2 ^= (c1 << 63) ^ (c1 << 62) ^ (c1 << 57);
	c3 ^= c1 ^ (c1 >> 1) ^ (c1 >> 2) ^ (c1 >> 7);

	/* Return (c2, c3).  This implicitly multiplies by x^-128. */
	a->lo = cpu_to_le64(c2);
	a->hi = cpu_to_le64(c3);
}

static void __maybe_unused ghash_blocks_generic(struct polyval_elem *acc,
						const struct polyval_elem *key,
						const u8 *data, size_t nblocks)
{
	do {
		acc->lo ^=
			cpu_to_le64(get_unaligned_be64((__be64 *)(data + 8)));
		acc->hi ^= cpu_to_le64(get_unaligned_be64((__be64 *)data));
		polyval_mul_generic(acc, key);
		data += GHASH_BLOCK_SIZE;
	} while (--nblocks);
}

static void __maybe_unused
polyval_blocks_generic(struct polyval_elem *acc, const struct polyval_elem *key,
		       const u8 *data, size_t nblocks)
{
	do {
		acc->lo ^= get_unaligned((__le64 *)data);
		acc->hi ^= get_unaligned((__le64 *)(data + 8));
		polyval_mul_generic(acc, key);
		data += POLYVAL_BLOCK_SIZE;
	} while (--nblocks);
}

/* Convert the key from GHASH format to POLYVAL format. */
static void __maybe_unused ghash_key_to_polyval(const u8 in[GHASH_BLOCK_SIZE],
						struct polyval_elem *out)
{
	u64 hi = get_unaligned_be64(&in[0]);
	u64 lo = get_unaligned_be64(&in[8]);
	u64 mask = (s64)hi >> 63;

	hi = (hi << 1) ^ (lo >> 63) ^ (mask & ((u64)0xc2 << 56));
	lo = (lo << 1) ^ (mask & 1);
	out->lo = cpu_to_le64(lo);
	out->hi = cpu_to_le64(hi);
}

/* Convert the accumulator from POLYVAL format to GHASH format. */
static void polyval_acc_to_ghash(const struct polyval_elem *in,
				 u8 out[GHASH_BLOCK_SIZE])
{
	put_unaligned_be64(le64_to_cpu(in->hi), &out[0]);
	put_unaligned_be64(le64_to_cpu(in->lo), &out[8]);
}

/* Convert the accumulator from GHASH format to POLYVAL format. */
static void __maybe_unused ghash_acc_to_polyval(const u8 in[GHASH_BLOCK_SIZE],
						struct polyval_elem *out)
{
	out->lo = cpu_to_le64(get_unaligned_be64(&in[8]));
	out->hi = cpu_to_le64(get_unaligned_be64(&in[0]));
}

#ifdef CONFIG_CRYPTO_LIB_GF128HASH_ARCH
#include "gf128hash.h" /* $(SRCARCH)/gf128hash.h */
#endif

void ghash_preparekey(struct ghash_key *key, const u8 raw_key[GHASH_BLOCK_SIZE])
{
#ifdef ghash_preparekey_arch
	ghash_preparekey_arch(key, raw_key);
#else
	ghash_key_to_polyval(raw_key, &key->h);
#endif
}
EXPORT_SYMBOL_GPL(ghash_preparekey);

static void ghash_mul(struct ghash_ctx *ctx)
{
#ifdef ghash_mul_arch
	ghash_mul_arch(&ctx->acc, ctx->key);
#elif defined(ghash_blocks_arch)
	static const u8 zeroes[GHASH_BLOCK_SIZE];

	ghash_blocks_arch(&ctx->acc, ctx->key, zeroes, 1);
#else
	polyval_mul_generic(&ctx->acc, &ctx->key->h);
#endif
}

/* nblocks is always >= 1. */
static void ghash_blocks(struct ghash_ctx *ctx, const u8 *data, size_t nblocks)
{
#ifdef ghash_blocks_arch
	ghash_blocks_arch(&ctx->acc, ctx->key, data, nblocks);
#else
	ghash_blocks_generic(&ctx->acc, &ctx->key->h, data, nblocks);
#endif
}

void ghash_update(struct ghash_ctx *ctx, const u8 *data, size_t len)
{
	if (unlikely(ctx->partial)) {
		size_t n = min(len, GHASH_BLOCK_SIZE - ctx->partial);

		len -= n;
		while (n--)
			ctx->acc.bytes[GHASH_BLOCK_SIZE - 1 - ctx->partial++] ^=
				*data++;
		if (ctx->partial < GHASH_BLOCK_SIZE)
			return;
		ghash_mul(ctx);
	}
	if (len >= GHASH_BLOCK_SIZE) {
		size_t nblocks = len / GHASH_BLOCK_SIZE;

		ghash_blocks(ctx, data, nblocks);
		data += len & ~(GHASH_BLOCK_SIZE - 1);
		len &= GHASH_BLOCK_SIZE - 1;
	}
	for (size_t i = 0; i < len; i++)
		ctx->acc.bytes[GHASH_BLOCK_SIZE - 1 - i] ^= data[i];
	ctx->partial = len;
}
EXPORT_SYMBOL_GPL(ghash_update);

void ghash_final(struct ghash_ctx *ctx, u8 out[GHASH_BLOCK_SIZE])
{
	if (unlikely(ctx->partial))
		ghash_mul(ctx);
	polyval_acc_to_ghash(&ctx->acc, out);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(ghash_final);

void polyval_preparekey(struct polyval_key *key,
			const u8 raw_key[POLYVAL_BLOCK_SIZE])
{
#ifdef polyval_preparekey_arch
	polyval_preparekey_arch(key, raw_key);
#else
	memcpy(key->h.bytes, raw_key, POLYVAL_BLOCK_SIZE);
#endif
}
EXPORT_SYMBOL_GPL(polyval_preparekey);

/*
 * polyval_mul_generic() and polyval_blocks_generic() take the key as a
 * polyval_elem rather than a polyval_key, so that arch-optimized
 * implementations with a different key format can use it as a fallback (if they
 * have H^1 stored somewhere in their struct).  Thus, the following dispatch
 * code is needed to pass the appropriate key argument.
 */

static void polyval_mul(struct polyval_ctx *ctx)
{
#ifdef polyval_mul_arch
	polyval_mul_arch(&ctx->acc, ctx->key);
#elif defined(polyval_blocks_arch)
	static const u8 zeroes[POLYVAL_BLOCK_SIZE];

	polyval_blocks_arch(&ctx->acc, ctx->key, zeroes, 1);
#else
	polyval_mul_generic(&ctx->acc, &ctx->key->h);
#endif
}

/* nblocks is always >= 1. */
static void polyval_blocks(struct polyval_ctx *ctx,
			   const u8 *data, size_t nblocks)
{
#ifdef polyval_blocks_arch
	polyval_blocks_arch(&ctx->acc, ctx->key, data, nblocks);
#else
	polyval_blocks_generic(&ctx->acc, &ctx->key->h, data, nblocks);
#endif
}

void polyval_update(struct polyval_ctx *ctx, const u8 *data, size_t len)
{
	if (unlikely(ctx->partial)) {
		size_t n = min(len, POLYVAL_BLOCK_SIZE - ctx->partial);

		len -= n;
		while (n--)
			ctx->acc.bytes[ctx->partial++] ^= *data++;
		if (ctx->partial < POLYVAL_BLOCK_SIZE)
			return;
		polyval_mul(ctx);
	}
	if (len >= POLYVAL_BLOCK_SIZE) {
		size_t nblocks = len / POLYVAL_BLOCK_SIZE;

		polyval_blocks(ctx, data, nblocks);
		data += len & ~(POLYVAL_BLOCK_SIZE - 1);
		len &= POLYVAL_BLOCK_SIZE - 1;
	}
	for (size_t i = 0; i < len; i++)
		ctx->acc.bytes[i] ^= data[i];
	ctx->partial = len;
}
EXPORT_SYMBOL_GPL(polyval_update);

void polyval_final(struct polyval_ctx *ctx, u8 out[POLYVAL_BLOCK_SIZE])
{
	if (unlikely(ctx->partial))
		polyval_mul(ctx);
	memcpy(out, &ctx->acc, POLYVAL_BLOCK_SIZE);
	memzero_explicit(ctx, sizeof(*ctx));
}
EXPORT_SYMBOL_GPL(polyval_final);

#ifdef gf128hash_mod_init_arch
static int __init gf128hash_mod_init(void)
{
	gf128hash_mod_init_arch();
	return 0;
}
subsys_initcall(gf128hash_mod_init);

static void __exit gf128hash_mod_exit(void)
{
}
module_exit(gf128hash_mod_exit);
#endif

MODULE_DESCRIPTION("GF(2^128) polynomial hashing: GHASH and POLYVAL");
MODULE_LICENSE("GPL");
