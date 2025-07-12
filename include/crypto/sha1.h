/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for SHA-1 algorithms
 */

#ifndef _CRYPTO_SHA1_H
#define _CRYPTO_SHA1_H

#include <linux/types.h>

#define SHA1_DIGEST_SIZE        20
#define SHA1_BLOCK_SIZE         64
#define SHA1_STATE_SIZE         offsetof(struct sha1_state, buffer)

#define SHA1_H0		0x67452301UL
#define SHA1_H1		0xefcdab89UL
#define SHA1_H2		0x98badcfeUL
#define SHA1_H3		0x10325476UL
#define SHA1_H4		0xc3d2e1f0UL

extern const u8 sha1_zero_message_hash[SHA1_DIGEST_SIZE];

struct sha1_state {
	u32 state[SHA1_DIGEST_SIZE / 4];
	u64 count;
	u8 buffer[SHA1_BLOCK_SIZE];
};

/*
 * An implementation of SHA-1's compression function.  Don't use in new code!
 * You shouldn't be using SHA-1, and even if you *have* to use SHA-1, this isn't
 * the correct way to hash something with SHA-1 (use crypto_shash instead).
 */
#define SHA1_DIGEST_WORDS	(SHA1_DIGEST_SIZE / 4)
#define SHA1_WORKSPACE_WORDS	16
void sha1_init_raw(__u32 *buf);
void sha1_transform(__u32 *digest, const char *data, __u32 *W);

/* State for the SHA-1 compression function */
struct sha1_block_state {
	u32 h[SHA1_DIGEST_SIZE / 4];
};

/**
 * struct sha1_ctx - Context for hashing a message with SHA-1
 * @state: the compression function state
 * @bytecount: number of bytes processed so far
 * @buf: partial block buffer; bytecount % SHA1_BLOCK_SIZE bytes are valid
 */
struct sha1_ctx {
	struct sha1_block_state state;
	u64 bytecount;
	u8 buf[SHA1_BLOCK_SIZE];
};

/**
 * sha1_init() - Initialize a SHA-1 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider sha1() instead.
 *
 * Context: Any context.
 */
void sha1_init(struct sha1_ctx *ctx);

/**
 * sha1_update() - Update a SHA-1 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void sha1_update(struct sha1_ctx *ctx, const u8 *data, size_t len);

/**
 * sha1_final() - Finish computing a SHA-1 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting SHA-1 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void sha1_final(struct sha1_ctx *ctx, u8 out[SHA1_DIGEST_SIZE]);

/**
 * sha1() - Compute SHA-1 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting SHA-1 message digest
 *
 * Context: Any context.
 */
void sha1(const u8 *data, size_t len, u8 out[SHA1_DIGEST_SIZE]);

#endif /* _CRYPTO_SHA1_H */
