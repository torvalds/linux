/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRYPTO_MD5_H
#define _CRYPTO_MD5_H

#include <crypto/hash.h>
#include <linux/types.h>

#define MD5_DIGEST_SIZE		16
#define MD5_HMAC_BLOCK_SIZE	64
#define MD5_BLOCK_SIZE		64
#define MD5_BLOCK_WORDS		16
#define MD5_HASH_WORDS		4
#define MD5_STATE_SIZE		24

#define MD5_H0	0x67452301UL
#define MD5_H1	0xefcdab89UL
#define MD5_H2	0x98badcfeUL
#define MD5_H3	0x10325476UL

#define CRYPTO_MD5_STATESIZE \
	CRYPTO_HASH_STATESIZE(MD5_STATE_SIZE, MD5_HMAC_BLOCK_SIZE)

extern const u8 md5_zero_message_hash[MD5_DIGEST_SIZE];

struct md5_state {
	u32 hash[MD5_HASH_WORDS];
	u64 byte_count;
	u32 block[MD5_BLOCK_WORDS];
};

/* State for the MD5 compression function */
struct md5_block_state {
	u32 h[MD5_HASH_WORDS];
};

/**
 * struct md5_ctx - Context for hashing a message with MD5
 * @state: the compression function state
 * @bytecount: number of bytes processed so far
 * @buf: partial block buffer; bytecount % MD5_BLOCK_SIZE bytes are valid
 */
struct md5_ctx {
	struct md5_block_state state;
	u64 bytecount;
	u8 buf[MD5_BLOCK_SIZE] __aligned(__alignof__(__le64));
};

/**
 * md5_init() - Initialize an MD5 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider md5() instead.
 *
 * Context: Any context.
 */
void md5_init(struct md5_ctx *ctx);

/**
 * md5_update() - Update an MD5 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
void md5_update(struct md5_ctx *ctx, const u8 *data, size_t len);

/**
 * md5_final() - Finish computing an MD5 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting MD5 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void md5_final(struct md5_ctx *ctx, u8 out[MD5_DIGEST_SIZE]);

/**
 * md5() - Compute MD5 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting MD5 message digest
 *
 * Context: Any context.
 */
void md5(const u8 *data, size_t len, u8 out[MD5_DIGEST_SIZE]);

/**
 * struct hmac_md5_key - Prepared key for HMAC-MD5
 * @istate: private
 * @ostate: private
 */
struct hmac_md5_key {
	struct md5_block_state istate;
	struct md5_block_state ostate;
};

/**
 * struct hmac_md5_ctx - Context for computing HMAC-MD5 of a message
 * @hash_ctx: private
 * @ostate: private
 */
struct hmac_md5_ctx {
	struct md5_ctx hash_ctx;
	struct md5_block_state ostate;
};

/**
 * hmac_md5_preparekey() - Prepare a key for HMAC-MD5
 * @key: (output) the key structure to initialize
 * @raw_key: the raw HMAC-MD5 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * Note: the caller is responsible for zeroizing both the struct hmac_md5_key
 * and the raw key once they are no longer needed.
 *
 * Context: Any context.
 */
void hmac_md5_preparekey(struct hmac_md5_key *key,
			 const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_md5_init() - Initialize an HMAC-MD5 context for a new message
 * @ctx: (output) the HMAC context to initialize
 * @key: the prepared HMAC key
 *
 * If you don't need incremental computation, consider hmac_md5() instead.
 *
 * Context: Any context.
 */
void hmac_md5_init(struct hmac_md5_ctx *ctx, const struct hmac_md5_key *key);

/**
 * hmac_md5_init_usingrawkey() - Initialize an HMAC-MD5 context for a new
 *				  message, using a raw key
 * @ctx: (output) the HMAC context to initialize
 * @raw_key: the raw HMAC-MD5 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * If you don't need incremental computation, consider hmac_md5_usingrawkey()
 * instead.
 *
 * Context: Any context.
 */
void hmac_md5_init_usingrawkey(struct hmac_md5_ctx *ctx,
			       const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_md5_update() - Update an HMAC-MD5 context with message data
 * @ctx: the HMAC context to update; must have been initialized
 * @data: the message data
 * @data_len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void hmac_md5_update(struct hmac_md5_ctx *ctx,
				   const u8 *data, size_t data_len)
{
	md5_update(&ctx->hash_ctx, data, data_len);
}

/**
 * hmac_md5_final() - Finish computing an HMAC-MD5 value
 * @ctx: the HMAC context to finalize; must have been initialized
 * @out: (output) the resulting HMAC-MD5 value
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void hmac_md5_final(struct hmac_md5_ctx *ctx, u8 out[MD5_DIGEST_SIZE]);

/**
 * hmac_md5() - Compute HMAC-MD5 in one shot, using a prepared key
 * @key: the prepared HMAC key
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-MD5 value
 *
 * If you're using the key only once, consider using hmac_md5_usingrawkey().
 *
 * Context: Any context.
 */
void hmac_md5(const struct hmac_md5_key *key,
	      const u8 *data, size_t data_len, u8 out[MD5_DIGEST_SIZE]);

/**
 * hmac_md5_usingrawkey() - Compute HMAC-MD5 in one shot, using a raw key
 * @raw_key: the raw HMAC-MD5 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-MD5 value
 *
 * If you're using the key multiple times, prefer to use hmac_md5_preparekey()
 * followed by multiple calls to hmac_md5() instead.
 *
 * Context: Any context.
 */
void hmac_md5_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			  const u8 *data, size_t data_len,
			  u8 out[MD5_DIGEST_SIZE]);

#endif /* _CRYPTO_MD5_H */
