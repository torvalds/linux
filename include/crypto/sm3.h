/*
 * Common values for SM3 algorithm
 */

#ifndef _CRYPTO_SM3_H
#define _CRYPTO_SM3_H

#include <linux/types.h>

#define SM3_DIGEST_SIZE	32
#define SM3_BLOCK_SIZE	64

#define SM3_T1		0x79CC4519
#define SM3_T2		0x7A879D8A

#define SM3_IVA		0x7380166f
#define SM3_IVB		0x4914b2b9
#define SM3_IVC		0x172442d7
#define SM3_IVD		0xda8a0600
#define SM3_IVE		0xa96f30bc
#define SM3_IVF		0x163138aa
#define SM3_IVG		0xe38dee4d
#define SM3_IVH		0xb0fb0e4e

extern const u8 sm3_zero_message_hash[SM3_DIGEST_SIZE];

struct sm3_state {
	u32 state[SM3_DIGEST_SIZE / 4];
	u64 count;
	u8 buffer[SM3_BLOCK_SIZE];
};

struct shash_desc;

extern int crypto_sm3_update(struct shash_desc *desc, const u8 *data,
			      unsigned int len);

extern int crypto_sm3_final(struct shash_desc *desc, u8 *out);

extern int crypto_sm3_finup(struct shash_desc *desc, const u8 *data,
			     unsigned int len, u8 *hash);
#endif
