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

#define SHA384_DIGEST_SIZE      48
#define SHA384_BLOCK_SIZE       128

#define SHA512_DIGEST_SIZE      64
#define SHA512_BLOCK_SIZE       128

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

struct sha256_state {
	u32 state[SHA256_DIGEST_SIZE / 4];
	u64 count;
	u8 buf[SHA256_BLOCK_SIZE];
};

struct sha512_state {
	u64 state[SHA512_DIGEST_SIZE / 8];
	u64 count[2];
	u8 buf[SHA512_BLOCK_SIZE];
};

struct shash_desc;

extern int crypto_sha256_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len);

extern int crypto_sha256_finup(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *hash);

extern int crypto_sha512_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len);

extern int crypto_sha512_finup(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *hash);

/*
 * Stand-alone implementation of the SHA256 algorithm. It is designed to
 * have as little dependencies as possible so it can be used in the
 * kexec_file purgatory. In other cases you should generally use the
 * hash APIs from include/crypto/hash.h. Especially when hashing large
 * amounts of data as those APIs may be hw-accelerated.
 *
 * For details see lib/crypto/sha256.c
 */

static inline void sha256_init(struct sha256_state *sctx)
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
void sha256_update(struct sha256_state *sctx, const u8 *data, unsigned int len);
void sha256_final(struct sha256_state *sctx, u8 *out);
void sha256(const u8 *data, unsigned int len, u8 *out);

static inline void sha224_init(struct sha256_state *sctx)
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
void sha224_update(struct sha256_state *sctx, const u8 *data, unsigned int len);
void sha224_final(struct sha256_state *sctx, u8 *out);

#endif /* _CRYPTO_SHA2_H */
