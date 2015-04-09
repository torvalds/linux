/*
 * Common values for SHA algorithms
 */

#ifndef _CRYPTO_SHA_H
#define _CRYPTO_SHA_H

#include <linux/types.h>

#define SHA1_DIGEST_SIZE        20
#define SHA1_BLOCK_SIZE         64

#define SHA224_DIGEST_SIZE	28
#define SHA224_BLOCK_SIZE	64

#define SHA256_DIGEST_SIZE      32
#define SHA256_BLOCK_SIZE       64

#define SHA384_DIGEST_SIZE      48
#define SHA384_BLOCK_SIZE       128

#define SHA512_DIGEST_SIZE      64
#define SHA512_BLOCK_SIZE       128

#define SHA1_H0		0x67452301UL
#define SHA1_H1		0xefcdab89UL
#define SHA1_H2		0x98badcfeUL
#define SHA1_H3		0x10325476UL
#define SHA1_H4		0xc3d2e1f0UL

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

struct sha1_state {
	u32 state[SHA1_DIGEST_SIZE / 4];
	u64 count;
	u8 buffer[SHA1_BLOCK_SIZE];
};

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

extern int crypto_sha1_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len);

extern int crypto_sha1_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *hash);

extern int crypto_sha256_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len);

extern int crypto_sha256_finup(struct shash_desc *desc, const u8 *data,
			       unsigned int len, u8 *hash);

extern int crypto_sha512_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len);
#endif
