/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NH hash function for Adiantum
 */

#ifndef _CRYPTO_NH_H
#define _CRYPTO_NH_H

#include <linux/types.h>

/* NH parameterization: */

/* Endianness: little */
/* Word size: 32 bits (works well on NEON, SSE2, AVX2) */

/* Stride: 2 words (optimal on ARM32 NEON; works okay on other CPUs too) */
#define NH_PAIR_STRIDE		2
#define NH_MESSAGE_UNIT		(NH_PAIR_STRIDE * 2 * sizeof(u32))

/* Num passes (Toeplitz iteration count): 4, to give ε = 2^{-128} */
#define NH_NUM_PASSES		4
#define NH_HASH_BYTES		(NH_NUM_PASSES * sizeof(u64))

/* Max message size: 1024 bytes (32x compression factor) */
#define NH_NUM_STRIDES		64
#define NH_MESSAGE_WORDS	(NH_PAIR_STRIDE * 2 * NH_NUM_STRIDES)
#define NH_MESSAGE_BYTES	(NH_MESSAGE_WORDS * sizeof(u32))
#define NH_KEY_WORDS		(NH_MESSAGE_WORDS + \
				 NH_PAIR_STRIDE * 2 * (NH_NUM_PASSES - 1))
#define NH_KEY_BYTES		(NH_KEY_WORDS * sizeof(u32))

/**
 * nh() - NH hash function for Adiantum
 * @key: The key.  @message_len + 48 bytes of it are used.  This is NH_KEY_BYTES
 *	 if @message_len has its maximum length of NH_MESSAGE_BYTES.
 * @message: The message
 * @message_len: The message length in bytes.  Must be a multiple of 16
 *		 (NH_MESSAGE_UNIT) and at most 1024 (NH_MESSAGE_BYTES).
 * @hash: (output) The resulting hash value
 *
 * Note: the pseudocode for NH in the Adiantum paper iterates over 1024-byte
 * segments of the message, computes a 32-byte hash for each, and returns all
 * the hashes concatenated together.  In contrast, this function just hashes one
 * segment and returns one hash.  It's the caller's responsibility to call this
 * function for each 1024-byte segment and collect all the hashes.
 *
 * Context: Any context.
 */
void nh(const u32 *key, const u8 *message, size_t message_len,
	__le64 hash[NH_NUM_PASSES]);

#endif /* _CRYPTO_NH_H */
