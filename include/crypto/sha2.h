/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common values for SHA-2 algorithms
 */

#ifndef _CRYPTO_SHA2_H
#define _CRYPTO_SHA2_H

#include <linux/types.h>

#define SHA224_DIGEST_SIZE	28
#define SHA224_BLOCK_SIZE	64

#define SHA256_DIGEST_SIZE      32
#define SHA256_BLOCK_SIZE       64
#define SHA256_STATE_WORDS      8

#define SHA384_DIGEST_SIZE      48
#define SHA384_BLOCK_SIZE       128

#define SHA512_DIGEST_SIZE      64
#define SHA512_BLOCK_SIZE       128
#define SHA512_STATE_SIZE       80

#define SHA224_H0	0xc1059ed8UL
#define SHA224_H1	0x367cd507UL
#define SHA224_H2	0x3070dd17UL
#define SHA224_H3	0xf70e5939UL
#define SHA224_H4	0xffc00b31UL
#define SHA224_H5	0x68581511UL
#define SHA224_H6	0x64f98fa7UL
#define SHA224_H7	0xbefa4fa4UL

#define SHA256_H0	0x6a09e667UL
#define SHA256_H1	0xbb67ae85UL
#define SHA256_H2	0x3c6ef372UL
#define SHA256_H3	0xa54ff53aUL
#define SHA256_H4	0x510e527fUL
#define SHA256_H5	0x9b05688cUL
#define SHA256_H6	0x1f83d9abUL
#define SHA256_H7	0x5be0cd19UL

#define SHA384_H0	0xcbbb9d5dc1059ed8ULL
#define SHA384_H1	0x629a292a367cd507ULL
#define SHA384_H2	0x9159015a3070dd17ULL
#define SHA384_H3	0x152fecd8f70e5939ULL
#define SHA384_H4	0x67332667ffc00b31ULL
#define SHA384_H5	0x8eb44a8768581511ULL
#define SHA384_H6	0xdb0c2e0d64f98fa7ULL
#define SHA384_H7	0x47b5481dbefa4fa4ULL

#define SHA512_H0	0x6a09e667f3bcc908ULL
#define SHA512_H1	0xbb67ae8584caa73bULL
#define SHA512_H2	0x3c6ef372fe94f82bULL
#define SHA512_H3	0xa54ff53a5f1d36f1ULL
#define SHA512_H4	0x510e527fade682d1ULL
#define SHA512_H5	0x9b05688c2b3e6c1fULL
#define SHA512_H6	0x1f83d9abfb41bd6bULL
#define SHA512_H7	0x5be0cd19137e2179ULL

extern const u8 sha224_zero_message_hash[SHA224_DIGEST_SIZE];

extern const u8 sha256_zero_message_hash[SHA256_DIGEST_SIZE];

extern const u8 sha384_zero_message_hash[SHA384_DIGEST_SIZE];

extern const u8 sha512_zero_message_hash[SHA512_DIGEST_SIZE];

struct crypto_sha256_state {
	u32 state[SHA256_STATE_WORDS];
	u64 count;
};

static inline void sha224_block_init(struct crypto_sha256_state *sctx)
{
	sctx->state[0] = SHA224_H0;
	sctx->state[1] = SHA224_H1;
	sctx->state[2] = SHA224_H2;
	sctx->state[3] = SHA224_H3;
	sctx->state[4] = SHA224_H4;
	sctx->state[5] = SHA224_H5;
	sctx->state[6] = SHA224_H6;
	sctx->state[7] = SHA224_H7;
	sctx->count = 0;
}

static inline void sha256_block_init(struct crypto_sha256_state *sctx)
{
	sctx->state[0] = SHA256_H0;
	sctx->state[1] = SHA256_H1;
	sctx->state[2] = SHA256_H2;
	sctx->state[3] = SHA256_H3;
	sctx->state[4] = SHA256_H4;
	sctx->state[5] = SHA256_H5;
	sctx->state[6] = SHA256_H6;
	sctx->state[7] = SHA256_H7;
	sctx->count = 0;
}

struct sha256_state {
	union {
		struct crypto_sha256_state ctx;
		struct {
			u32 state[SHA256_STATE_WORDS];
			u64 count;
		};
	};
	u8 buf[SHA256_BLOCK_SIZE];
};

struct sha512_state {
	u64 state[SHA512_DIGEST_SIZE / 8];
	u64 count[2];
	u8 buf[SHA512_BLOCK_SIZE];
};

/* State for the SHA-256 (and SHA-224) compression function */
struct sha256_block_state {
	u32 h[SHA256_STATE_WORDS];
};

/*
 * Context structure, shared by SHA-224 and SHA-256.  The sha224_ctx and
 * sha256_ctx structs wrap this one so that the API has proper typing and
 * doesn't allow mixing the SHA-224 and SHA-256 functions arbitrarily.
 */
struct __sha256_ctx {
	struct sha256_block_state state;
	u64 bytecount;
	u8 buf[SHA256_BLOCK_SIZE] __aligned(__alignof__(__be64));
};
void __sha256_update(struct __sha256_ctx *ctx, const u8 *data, size_t len);

/*
 * HMAC key and message context structs, shared by HMAC-SHA224 and HMAC-SHA256.
 * The hmac_sha224_* and hmac_sha256_* structs wrap this one so that the API has
 * proper typing and doesn't allow mixing the functions arbitrarily.
 */
struct __hmac_sha256_key {
	struct sha256_block_state istate;
	struct sha256_block_state ostate;
};
struct __hmac_sha256_ctx {
	struct __sha256_ctx sha_ctx;
	struct sha256_block_state ostate;
};
void __hmac_sha256_init(struct __hmac_sha256_ctx *ctx,
			const struct __hmac_sha256_key *key);

/**
 * struct sha224_ctx - Context for hashing a message with SHA-224
 * @ctx: private
 */
struct sha224_ctx {
	struct __sha256_ctx ctx;
};

/**
 * sha224_init() - Initialize a SHA-224 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider sha224() instead.
 *
 * Context: Any context.
 */
void sha224_init(struct sha224_ctx *ctx);

/**
 * sha224_update() - Update a SHA-224 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void sha224_update(struct sha224_ctx *ctx,
				 const u8 *data, size_t len)
{
	__sha256_update(&ctx->ctx, data, len);
}

/**
 * sha224_final() - Finish computing a SHA-224 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting SHA-224 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void sha224_final(struct sha224_ctx *ctx, u8 out[SHA224_DIGEST_SIZE]);

/**
 * sha224() - Compute SHA-224 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting SHA-224 message digest
 *
 * Context: Any context.
 */
void sha224(const u8 *data, size_t len, u8 out[SHA224_DIGEST_SIZE]);

/**
 * struct hmac_sha224_key - Prepared key for HMAC-SHA224
 * @key: private
 */
struct hmac_sha224_key {
	struct __hmac_sha256_key key;
};

/**
 * struct hmac_sha224_ctx - Context for computing HMAC-SHA224 of a message
 * @ctx: private
 */
struct hmac_sha224_ctx {
	struct __hmac_sha256_ctx ctx;
};

/**
 * hmac_sha224_preparekey() - Prepare a key for HMAC-SHA224
 * @key: (output) the key structure to initialize
 * @raw_key: the raw HMAC-SHA224 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * Note: the caller is responsible for zeroizing both the struct hmac_sha224_key
 * and the raw key once they are no longer needed.
 *
 * Context: Any context.
 */
void hmac_sha224_preparekey(struct hmac_sha224_key *key,
			    const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha224_init() - Initialize an HMAC-SHA224 context for a new message
 * @ctx: (output) the HMAC context to initialize
 * @key: the prepared HMAC key
 *
 * If you don't need incremental computation, consider hmac_sha224() instead.
 *
 * Context: Any context.
 */
static inline void hmac_sha224_init(struct hmac_sha224_ctx *ctx,
				    const struct hmac_sha224_key *key)
{
	__hmac_sha256_init(&ctx->ctx, &key->key);
}

/**
 * hmac_sha224_init_usingrawkey() - Initialize an HMAC-SHA224 context for a new
 *				    message, using a raw key
 * @ctx: (output) the HMAC context to initialize
 * @raw_key: the raw HMAC-SHA224 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * If you don't need incremental computation, consider hmac_sha224_usingrawkey()
 * instead.
 *
 * Context: Any context.
 */
void hmac_sha224_init_usingrawkey(struct hmac_sha224_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha224_update() - Update an HMAC-SHA224 context with message data
 * @ctx: the HMAC context to update; must have been initialized
 * @data: the message data
 * @data_len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void hmac_sha224_update(struct hmac_sha224_ctx *ctx,
				      const u8 *data, size_t data_len)
{
	__sha256_update(&ctx->ctx.sha_ctx, data, data_len);
}

/**
 * hmac_sha224_final() - Finish computing an HMAC-SHA224 value
 * @ctx: the HMAC context to finalize; must have been initialized
 * @out: (output) the resulting HMAC-SHA224 value
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void hmac_sha224_final(struct hmac_sha224_ctx *ctx, u8 out[SHA224_DIGEST_SIZE]);

/**
 * hmac_sha224() - Compute HMAC-SHA224 in one shot, using a prepared key
 * @key: the prepared HMAC key
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA224 value
 *
 * If you're using the key only once, consider using hmac_sha224_usingrawkey().
 *
 * Context: Any context.
 */
void hmac_sha224(const struct hmac_sha224_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA224_DIGEST_SIZE]);

/**
 * hmac_sha224_usingrawkey() - Compute HMAC-SHA224 in one shot, using a raw key
 * @raw_key: the raw HMAC-SHA224 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA224 value
 *
 * If you're using the key multiple times, prefer to use
 * hmac_sha224_preparekey() followed by multiple calls to hmac_sha224() instead.
 *
 * Context: Any context.
 */
void hmac_sha224_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA224_DIGEST_SIZE]);

/**
 * struct sha256_ctx - Context for hashing a message with SHA-256
 * @ctx: private
 */
struct sha256_ctx {
	struct __sha256_ctx ctx;
};

/**
 * sha256_init() - Initialize a SHA-256 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider sha256() instead.
 *
 * Context: Any context.
 */
void sha256_init(struct sha256_ctx *ctx);

/**
 * sha256_update() - Update a SHA-256 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void sha256_update(struct sha256_ctx *ctx,
				 const u8 *data, size_t len)
{
	__sha256_update(&ctx->ctx, data, len);
}

/**
 * sha256_final() - Finish computing a SHA-256 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting SHA-256 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void sha256_final(struct sha256_ctx *ctx, u8 out[SHA256_DIGEST_SIZE]);

/**
 * sha256() - Compute SHA-256 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting SHA-256 message digest
 *
 * Context: Any context.
 */
void sha256(const u8 *data, size_t len, u8 out[SHA256_DIGEST_SIZE]);

/**
 * struct hmac_sha256_key - Prepared key for HMAC-SHA256
 * @key: private
 */
struct hmac_sha256_key {
	struct __hmac_sha256_key key;
};

/**
 * struct hmac_sha256_ctx - Context for computing HMAC-SHA256 of a message
 * @ctx: private
 */
struct hmac_sha256_ctx {
	struct __hmac_sha256_ctx ctx;
};

/**
 * hmac_sha256_preparekey() - Prepare a key for HMAC-SHA256
 * @key: (output) the key structure to initialize
 * @raw_key: the raw HMAC-SHA256 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * Note: the caller is responsible for zeroizing both the struct hmac_sha256_key
 * and the raw key once they are no longer needed.
 *
 * Context: Any context.
 */
void hmac_sha256_preparekey(struct hmac_sha256_key *key,
			    const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha256_init() - Initialize an HMAC-SHA256 context for a new message
 * @ctx: (output) the HMAC context to initialize
 * @key: the prepared HMAC key
 *
 * If you don't need incremental computation, consider hmac_sha256() instead.
 *
 * Context: Any context.
 */
static inline void hmac_sha256_init(struct hmac_sha256_ctx *ctx,
				    const struct hmac_sha256_key *key)
{
	__hmac_sha256_init(&ctx->ctx, &key->key);
}

/**
 * hmac_sha256_init_usingrawkey() - Initialize an HMAC-SHA256 context for a new
 *				    message, using a raw key
 * @ctx: (output) the HMAC context to initialize
 * @raw_key: the raw HMAC-SHA256 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * If you don't need incremental computation, consider hmac_sha256_usingrawkey()
 * instead.
 *
 * Context: Any context.
 */
void hmac_sha256_init_usingrawkey(struct hmac_sha256_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha256_update() - Update an HMAC-SHA256 context with message data
 * @ctx: the HMAC context to update; must have been initialized
 * @data: the message data
 * @data_len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void hmac_sha256_update(struct hmac_sha256_ctx *ctx,
				      const u8 *data, size_t data_len)
{
	__sha256_update(&ctx->ctx.sha_ctx, data, data_len);
}

/**
 * hmac_sha256_final() - Finish computing an HMAC-SHA256 value
 * @ctx: the HMAC context to finalize; must have been initialized
 * @out: (output) the resulting HMAC-SHA256 value
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void hmac_sha256_final(struct hmac_sha256_ctx *ctx, u8 out[SHA256_DIGEST_SIZE]);

/**
 * hmac_sha256() - Compute HMAC-SHA256 in one shot, using a prepared key
 * @key: the prepared HMAC key
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA256 value
 *
 * If you're using the key only once, consider using hmac_sha256_usingrawkey().
 *
 * Context: Any context.
 */
void hmac_sha256(const struct hmac_sha256_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA256_DIGEST_SIZE]);

/**
 * hmac_sha256_usingrawkey() - Compute HMAC-SHA256 in one shot, using a raw key
 * @raw_key: the raw HMAC-SHA256 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA256 value
 *
 * If you're using the key multiple times, prefer to use
 * hmac_sha256_preparekey() followed by multiple calls to hmac_sha256() instead.
 *
 * Context: Any context.
 */
void hmac_sha256_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA256_DIGEST_SIZE]);

/* State for the SHA-512 (and SHA-384) compression function */
struct sha512_block_state {
	u64 h[8];
};

/*
 * Context structure, shared by SHA-384 and SHA-512.  The sha384_ctx and
 * sha512_ctx structs wrap this one so that the API has proper typing and
 * doesn't allow mixing the SHA-384 and SHA-512 functions arbitrarily.
 */
struct __sha512_ctx {
	struct sha512_block_state state;
	u64 bytecount_lo;
	u64 bytecount_hi;
	u8 buf[SHA512_BLOCK_SIZE] __aligned(__alignof__(__be64));
};
void __sha512_update(struct __sha512_ctx *ctx, const u8 *data, size_t len);

/*
 * HMAC key and message context structs, shared by HMAC-SHA384 and HMAC-SHA512.
 * The hmac_sha384_* and hmac_sha512_* structs wrap this one so that the API has
 * proper typing and doesn't allow mixing the functions arbitrarily.
 */
struct __hmac_sha512_key {
	struct sha512_block_state istate;
	struct sha512_block_state ostate;
};
struct __hmac_sha512_ctx {
	struct __sha512_ctx sha_ctx;
	struct sha512_block_state ostate;
};
void __hmac_sha512_init(struct __hmac_sha512_ctx *ctx,
			const struct __hmac_sha512_key *key);

/**
 * struct sha384_ctx - Context for hashing a message with SHA-384
 * @ctx: private
 */
struct sha384_ctx {
	struct __sha512_ctx ctx;
};

/**
 * sha384_init() - Initialize a SHA-384 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider sha384() instead.
 *
 * Context: Any context.
 */
void sha384_init(struct sha384_ctx *ctx);

/**
 * sha384_update() - Update a SHA-384 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void sha384_update(struct sha384_ctx *ctx,
				 const u8 *data, size_t len)
{
	__sha512_update(&ctx->ctx, data, len);
}

/**
 * sha384_final() - Finish computing a SHA-384 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting SHA-384 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void sha384_final(struct sha384_ctx *ctx, u8 out[SHA384_DIGEST_SIZE]);

/**
 * sha384() - Compute SHA-384 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting SHA-384 message digest
 *
 * Context: Any context.
 */
void sha384(const u8 *data, size_t len, u8 out[SHA384_DIGEST_SIZE]);

/**
 * struct hmac_sha384_key - Prepared key for HMAC-SHA384
 * @key: private
 */
struct hmac_sha384_key {
	struct __hmac_sha512_key key;
};

/**
 * struct hmac_sha384_ctx - Context for computing HMAC-SHA384 of a message
 * @ctx: private
 */
struct hmac_sha384_ctx {
	struct __hmac_sha512_ctx ctx;
};

/**
 * hmac_sha384_preparekey() - Prepare a key for HMAC-SHA384
 * @key: (output) the key structure to initialize
 * @raw_key: the raw HMAC-SHA384 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * Note: the caller is responsible for zeroizing both the struct hmac_sha384_key
 * and the raw key once they are no longer needed.
 *
 * Context: Any context.
 */
void hmac_sha384_preparekey(struct hmac_sha384_key *key,
			    const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha384_init() - Initialize an HMAC-SHA384 context for a new message
 * @ctx: (output) the HMAC context to initialize
 * @key: the prepared HMAC key
 *
 * If you don't need incremental computation, consider hmac_sha384() instead.
 *
 * Context: Any context.
 */
static inline void hmac_sha384_init(struct hmac_sha384_ctx *ctx,
				    const struct hmac_sha384_key *key)
{
	__hmac_sha512_init(&ctx->ctx, &key->key);
}

/**
 * hmac_sha384_init_usingrawkey() - Initialize an HMAC-SHA384 context for a new
 *				    message, using a raw key
 * @ctx: (output) the HMAC context to initialize
 * @raw_key: the raw HMAC-SHA384 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * If you don't need incremental computation, consider hmac_sha384_usingrawkey()
 * instead.
 *
 * Context: Any context.
 */
void hmac_sha384_init_usingrawkey(struct hmac_sha384_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha384_update() - Update an HMAC-SHA384 context with message data
 * @ctx: the HMAC context to update; must have been initialized
 * @data: the message data
 * @data_len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void hmac_sha384_update(struct hmac_sha384_ctx *ctx,
				      const u8 *data, size_t data_len)
{
	__sha512_update(&ctx->ctx.sha_ctx, data, data_len);
}

/**
 * hmac_sha384_final() - Finish computing an HMAC-SHA384 value
 * @ctx: the HMAC context to finalize; must have been initialized
 * @out: (output) the resulting HMAC-SHA384 value
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void hmac_sha384_final(struct hmac_sha384_ctx *ctx, u8 out[SHA384_DIGEST_SIZE]);

/**
 * hmac_sha384() - Compute HMAC-SHA384 in one shot, using a prepared key
 * @key: the prepared HMAC key
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA384 value
 *
 * If you're using the key only once, consider using hmac_sha384_usingrawkey().
 *
 * Context: Any context.
 */
void hmac_sha384(const struct hmac_sha384_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA384_DIGEST_SIZE]);

/**
 * hmac_sha384_usingrawkey() - Compute HMAC-SHA384 in one shot, using a raw key
 * @raw_key: the raw HMAC-SHA384 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA384 value
 *
 * If you're using the key multiple times, prefer to use
 * hmac_sha384_preparekey() followed by multiple calls to hmac_sha384() instead.
 *
 * Context: Any context.
 */
void hmac_sha384_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA384_DIGEST_SIZE]);

/**
 * struct sha512_ctx - Context for hashing a message with SHA-512
 * @ctx: private
 */
struct sha512_ctx {
	struct __sha512_ctx ctx;
};

/**
 * sha512_init() - Initialize a SHA-512 context for a new message
 * @ctx: the context to initialize
 *
 * If you don't need incremental computation, consider sha512() instead.
 *
 * Context: Any context.
 */
void sha512_init(struct sha512_ctx *ctx);

/**
 * sha512_update() - Update a SHA-512 context with message data
 * @ctx: the context to update; must have been initialized
 * @data: the message data
 * @len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void sha512_update(struct sha512_ctx *ctx,
				 const u8 *data, size_t len)
{
	__sha512_update(&ctx->ctx, data, len);
}

/**
 * sha512_final() - Finish computing a SHA-512 message digest
 * @ctx: the context to finalize; must have been initialized
 * @out: (output) the resulting SHA-512 message digest
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void sha512_final(struct sha512_ctx *ctx, u8 out[SHA512_DIGEST_SIZE]);

/**
 * sha512() - Compute SHA-512 message digest in one shot
 * @data: the message data
 * @len: the data length in bytes
 * @out: (output) the resulting SHA-512 message digest
 *
 * Context: Any context.
 */
void sha512(const u8 *data, size_t len, u8 out[SHA512_DIGEST_SIZE]);

/**
 * struct hmac_sha512_key - Prepared key for HMAC-SHA512
 * @key: private
 */
struct hmac_sha512_key {
	struct __hmac_sha512_key key;
};

/**
 * struct hmac_sha512_ctx - Context for computing HMAC-SHA512 of a message
 * @ctx: private
 */
struct hmac_sha512_ctx {
	struct __hmac_sha512_ctx ctx;
};

/**
 * hmac_sha512_preparekey() - Prepare a key for HMAC-SHA512
 * @key: (output) the key structure to initialize
 * @raw_key: the raw HMAC-SHA512 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * Note: the caller is responsible for zeroizing both the struct hmac_sha512_key
 * and the raw key once they are no longer needed.
 *
 * Context: Any context.
 */
void hmac_sha512_preparekey(struct hmac_sha512_key *key,
			    const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha512_init() - Initialize an HMAC-SHA512 context for a new message
 * @ctx: (output) the HMAC context to initialize
 * @key: the prepared HMAC key
 *
 * If you don't need incremental computation, consider hmac_sha512() instead.
 *
 * Context: Any context.
 */
static inline void hmac_sha512_init(struct hmac_sha512_ctx *ctx,
				    const struct hmac_sha512_key *key)
{
	__hmac_sha512_init(&ctx->ctx, &key->key);
}

/**
 * hmac_sha512_init_usingrawkey() - Initialize an HMAC-SHA512 context for a new
 *				    message, using a raw key
 * @ctx: (output) the HMAC context to initialize
 * @raw_key: the raw HMAC-SHA512 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 *
 * If you don't need incremental computation, consider hmac_sha512_usingrawkey()
 * instead.
 *
 * Context: Any context.
 */
void hmac_sha512_init_usingrawkey(struct hmac_sha512_ctx *ctx,
				  const u8 *raw_key, size_t raw_key_len);

/**
 * hmac_sha512_update() - Update an HMAC-SHA512 context with message data
 * @ctx: the HMAC context to update; must have been initialized
 * @data: the message data
 * @data_len: the data length in bytes
 *
 * This can be called any number of times.
 *
 * Context: Any context.
 */
static inline void hmac_sha512_update(struct hmac_sha512_ctx *ctx,
				      const u8 *data, size_t data_len)
{
	__sha512_update(&ctx->ctx.sha_ctx, data, data_len);
}

/**
 * hmac_sha512_final() - Finish computing an HMAC-SHA512 value
 * @ctx: the HMAC context to finalize; must have been initialized
 * @out: (output) the resulting HMAC-SHA512 value
 *
 * After finishing, this zeroizes @ctx.  So the caller does not need to do it.
 *
 * Context: Any context.
 */
void hmac_sha512_final(struct hmac_sha512_ctx *ctx, u8 out[SHA512_DIGEST_SIZE]);

/**
 * hmac_sha512() - Compute HMAC-SHA512 in one shot, using a prepared key
 * @key: the prepared HMAC key
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA512 value
 *
 * If you're using the key only once, consider using hmac_sha512_usingrawkey().
 *
 * Context: Any context.
 */
void hmac_sha512(const struct hmac_sha512_key *key,
		 const u8 *data, size_t data_len, u8 out[SHA512_DIGEST_SIZE]);

/**
 * hmac_sha512_usingrawkey() - Compute HMAC-SHA512 in one shot, using a raw key
 * @raw_key: the raw HMAC-SHA512 key
 * @raw_key_len: the key length in bytes.  All key lengths are supported.
 * @data: the message data
 * @data_len: the data length in bytes
 * @out: (output) the resulting HMAC-SHA512 value
 *
 * If you're using the key multiple times, prefer to use
 * hmac_sha512_preparekey() followed by multiple calls to hmac_sha512() instead.
 *
 * Context: Any context.
 */
void hmac_sha512_usingrawkey(const u8 *raw_key, size_t raw_key_len,
			     const u8 *data, size_t data_len,
			     u8 out[SHA512_DIGEST_SIZE]);

#endif /* _CRYPTO_SHA2_H */
