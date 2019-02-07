/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values and helper functions for the NHPoly1305 hash function.
 */

#ifndef _NHPOLY1305_H
#define _NHPOLY1305_H

#include <crypto/hash.h>
#include <crypto/poly1305.h>

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

#define NHPOLY1305_KEY_SIZE	(POLY1305_BLOCK_SIZE + NH_KEY_BYTES)

struct nhpoly1305_key {
	struct poly1305_key poly_key;
	u32 nh_key[NH_KEY_WORDS];
};

struct nhpoly1305_state {

	/* Running total of polynomial evaluation */
	struct poly1305_state poly_state;

	/* Partial block buffer */
	u8 buffer[NH_MESSAGE_UNIT];
	unsigned int buflen;

	/*
	 * Number of bytes remaining until the current NH message reaches
	 * NH_MESSAGE_BYTES.  When nonzero, 'nh_hash' holds the partial NH hash.
	 */
	unsigned int nh_remaining;

	__le64 nh_hash[NH_NUM_PASSES];
};

typedef void (*nh_t)(const u32 *key, const u8 *message, size_t message_len,
		     __le64 hash[NH_NUM_PASSES]);

int crypto_nhpoly1305_setkey(struct crypto_shash *tfm,
			     const u8 *key, unsigned int keylen);

int crypto_nhpoly1305_init(struct shash_desc *desc);
int crypto_nhpoly1305_update(struct shash_desc *desc,
			     const u8 *src, unsigned int srclen);
int crypto_nhpoly1305_update_helper(struct shash_desc *desc,
				    const u8 *src, unsigned int srclen,
				    nh_t nh_fn);
int crypto_nhpoly1305_final(struct shash_desc *desc, u8 *dst);
int crypto_nhpoly1305_final_helper(struct shash_desc *desc, u8 *dst,
				   nh_t nh_fn);

#endif /* _NHPOLY1305_H */
