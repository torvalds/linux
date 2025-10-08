/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _CRYPTO_MD5_H
#define _CRYPTO_MD5_H

#include <crypto/hash.h>
#include <linux/types.h>

#define MD5_DIGEST_SIZE		16
#define MD5_HMAC_BLOCK_SIZE	64
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

#endif
