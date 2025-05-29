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
void sha1_init(__u32 *buf);
void sha1_transform(__u32 *digest, const char *data, __u32 *W);

#endif /* _CRYPTO_SHA1_H */
