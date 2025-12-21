/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * POLYVAL library API
 *
 * Copyright 2025 Google LLC
 */

#ifndef _CRYPTO_POLYVAL_H
#define _CRYPTO_POLYVAL_H

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
#ifdef CONFIG_CRYPTO_LIB_POLYVAL_ARCH
#ifdef CONFIG_ARM64
	/** @h_powers: Powers of the hash key H^8 through H^1 */
	struct polyval_elem h_powers[8];
#elif defined(CONFIG_X86)
	/** @h_powers: Powers of the hash key H^8 through H^1 */
	struct polyval_elem h_powers[8];
#else
#error "Unhandled arch"
#endif
#else /* CONFIG_CRYPTO_LIB_POLYVAL_ARCH */
	/** @h: The hash key H */
	struct polyval_elem h;
#endif /* !CONFIG_CRYPTO_LIB_POLYVAL_ARCH */
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
#ifdef CONFIG_CRYPTO_LIB_POLYVAL_ARCH
void polyval_preparekey(struct polyval_key *key,
			const u8 raw_key[POLYVAL_BLOCK_SIZE]);

#else
static inline void polyval_preparekey(struct polyval_key *key,
				      const u8 raw_key[POLYVAL_BLOCK_SIZE])
{
	/* Just a simple copy, so inline it. */
	memcpy(key->h.bytes, raw_key, POLYVAL_BLOCK_SIZE);
}
#endif

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

#endif /* _CRYPTO_POLYVAL_H */
