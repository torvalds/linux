/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GF(2^128) polynomial hashing: GHASH and POLYVAL
 *
 * Copyright 2025 Google LLC
 */

#ifndef _CRYPTO_GF128HASH_H
#define _CRYPTO_GF128HASH_H

#include <crypto/ghash.h>
#include <linux/string.h>
#include <linux/types.h>

#define POLYVAL_BLOCK_SIZE	16
#define POLYVAL_DIGEST_SIZE	16

/**
 * struct polyval_elem - An element of the POLYVAL finite field
 * @bytes: View of the element as a byte array (unioned with @lo and @hi)
 * @lo: The low 64 terms of the element's polynomial
 * @hi: The high 64 terms of the element's polynomial
 *
 * This represents an element of the finite field GF(2^128), using the POLYVAL
 * convention: little-endian byte order and natural bit order.
 */
struct polyval_elem {
	union {
		u8 bytes[POLYVAL_BLOCK_SIZE];
		struct {
			__le64 lo;
			__le64 hi;
		};
	};
};

/**
 * struct ghash_key - Prepared key for GHASH
 *
 * Use ghash_preparekey() to initialize this.
 */
struct ghash_key {
#if defined(CONFIG_CRYPTO_LIB_GF128HASH_ARCH) && defined(CONFIG_PPC64)
	/** @htable: GHASH key format used by the POWER8 assembly code */
	u64 htable[4][2];
#elif defined(CONFIG_CRYPTO_LIB_GF128HASH_ARCH) && \
	(defined(CONFIG_RISCV) || defined(CONFIG_S390))
	/** @h_raw: The hash key H, in GHASH format */
	u8 h_raw[GHASH_BLOCK_SIZE];
#endif
	/** @h: The hash key H, in POLYVAL format */
	struct polyval_elem h;
};

/**
 * struct polyval_key - Prepared key for POLYVAL
 *
 * This may contain just the raw key H, or it may contain precomputed key
 * powers, depending on the platform's POLYVAL implementation.  Use
 * polyval_preparekey() to initialize this.
 *
 * By H^i we mean H^(i-1) * H * x^-128, with base case H^1 = H.  I.e. the
 * exponentiation repeats the POLYVAL dot operation, with its "extra" x^-128.
 */
struct polyval_key {
#if defined(CONFIG_CRYPTO_LIB_GF128HASH_ARCH) && \
	(defined(CONFIG_ARM64) || defined(CONFIG_X86))
	/** @h_powers: Powers of the hash key H^8 through H^1 */
	struct polyval_elem h_powers[8];
#else
	/** @h: The hash key H */
	struct polyval_elem h;
#endif
};

/**
 * struct ghash_ctx - Context for computing a GHASH value
 * @key: Pointer to the prepared GHASH key.  The user of the API is
 *	 responsible for ensuring that the key lives as long as the context.
 * @acc: The accumulator.  It is stored in POLYVAL format rather than GHASH
 *	 format, since most implementations want it in POLYVAL format.
 * @partial: Number of data bytes processed so far modulo GHASH_BLOCK_SIZE
 */
struct ghash_ctx {
	const struct ghash_key *key;
	struct polyval_elem acc;
	size_t partial;
};

/**
 * struct polyval_ctx - Context for computing a POLYVAL value
 * @key: Pointer to the prepared POLYVAL key.  The user of the API is
 *	 responsible for ensuring that the key lives as long as the context.
 * @acc: The accumulator
 * @partial: Number of data bytes processed so far modulo POLYVAL_BLOCK_SIZE
 */
struct polyval_ctx {
	const struct polyval_key *key;
	struct polyval_elem acc;
	size_t partial;
};

/**
 * ghash_preparekey() - Prepare a GHASH key
 * @key: (output) The key structure to initialize
 * @raw_key: The raw hash key
 *
 * Initialize a GHASH key structure from a raw key.
 *
 * Context: Any context.
 */
void ghash_preparekey(struct ghash_key *key,
		      const u8 raw_key[GHASH_BLOCK_SIZE]);

/**
 * polyval_preparekey() - Prepare a POLYVAL key
 * @key: (output) The key structure to initialize
 * @raw_key: The raw hash key
 *
 * Initialize a POLYVAL key structure from a raw key.  This may be a simple
 * copy, or it may involve precomputing powers of the key, depending on the
 * platform's POLYVAL implementation.
 *
 * Context: Any context.
 */
void polyval_preparekey(struct polyval_key *key,
			const u8 raw_key[POLYVAL_BLOCK_SIZE]);

/**
 * ghash_init() - Initialize a GHASH context for a new message
 * @ctx: The context to initialize
 * @key: The key to use.  Note that a pointer to the key is saved in the
 *	 context, so the key must live at least as long as the context.
 */
static inline void ghash_init(struct ghash_ctx *ctx,
			      const struct ghash_key *key)
{
	*ctx = (struct ghash_ctx){ .key = key };
}

/**
 * polyval_init() - Initialize a POLYVAL context for a new message
 * @ctx: The context to initialize
 * @key: The key to use.  Note that a pointer to the key is saved in the
 *	 context, so the key must live at least as long as the context.
 */
static inline void polyval_init(struct polyval_ctx *ctx,
				const struct polyval_key *key)
{
	*ctx = (struct polyval_ctx){ .key = key };
}

/**
 * polyval_import_blkaligned() - Import a POLYVAL accumulator value
 * @ctx: The context to initialize
 * @key: The key to import.  Note that a pointer to the key is saved in the
 *	 context, so the key must live at least as long as the context.
 * @acc: The accumulator value to import.
 *
 * This imports an accumulator that was saved by polyval_export_blkaligned().
 * The same key must be used.
 */
static inline void
polyval_import_blkaligned(struct polyval_ctx *ctx,
			  const struct polyval_key *key,
			  const struct polyval_elem *acc)
{
	*ctx = (struct polyval_ctx){ .key = key, .acc = *acc };
}

/**
 * polyval_export_blkaligned() - Export a POLYVAL accumulator value
 * @ctx: The context to export the accumulator value from
 * @acc: (output) The exported accumulator value
 *
 * This exports the accumulator from a POLYVAL context.  The number of data
 * bytes processed so far must be a multiple of POLYVAL_BLOCK_SIZE.
 */
static inline void polyval_export_blkaligned(const struct polyval_ctx *ctx,
					     struct polyval_elem *acc)
{
	*acc = ctx->acc;
}

/**
 * ghash_update() - Update a GHASH context with message data
 * @ctx: The context to update; must have been initialized
 * @data: The message data
 * @len: The data length in bytes.  Doesn't need to be block-aligned.
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void ghash_update(struct ghash_ctx *ctx, const u8 *data, size_t len);

/**
 * polyval_update() - Update a POLYVAL context with message data
 * @ctx: The context to update; must have been initialized
 * @data: The message data
 * @len: The data length in bytes.  Doesn't need to be block-aligned.
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void polyval_update(struct polyval_ctx *ctx, const u8 *data, size_t len);

/**
 * ghash_final() - Finish computing a GHASH value
 * @ctx: The context to finalize
 * @out: The output value
 *
 * If the total data length isn't a multiple of GHASH_BLOCK_SIZE, then the
 * final block is automatically zero-padded.
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void ghash_final(struct ghash_ctx *ctx, u8 out[GHASH_BLOCK_SIZE]);

/**
 * polyval_final() - Finish computing a POLYVAL value
 * @ctx: The context to finalize
 * @out: The output value
 *
 * If the total data length isn't a multiple of POLYVAL_BLOCK_SIZE, then the
 * final block is automatically zero-padded.
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void polyval_final(struct polyval_ctx *ctx, u8 out[POLYVAL_BLOCK_SIZE]);

/**
 * ghash() - Compute a GHASH value
 * @key: The prepared key
 * @data: The message data
 * @len: The data length in bytes.  Doesn't need to be block-aligned.
 * @out: The output value
 *
 * Context: Any context.
 */
static inline void ghash(const struct ghash_key *key, const u8 *data,
			 size_t len, u8 out[GHASH_BLOCK_SIZE])
{
	struct ghash_ctx ctx;

	ghash_init(&ctx, key);
	ghash_update(&ctx, data, len);
	ghash_final(&ctx, out);
}

/**
 * polyval() - Compute a POLYVAL value
 * @key: The prepared key
 * @data: The message data
 * @len: The data length in bytes.  Doesn't need to be block-aligned.
 * @out: The output value
 *
 * Context: Any context.
 */
static inline void polyval(const struct polyval_key *key,
			   const u8 *data, size_t len,
			   u8 out[POLYVAL_BLOCK_SIZE])
{
	struct polyval_ctx ctx;

	polyval_init(&ctx, key);
	polyval_update(&ctx, data, len);
	polyval_final(&ctx, out);
}

#endif /* _CRYPTO_GF128HASH_H */
