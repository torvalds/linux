/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Common values for ARC4 Cipher Algorithm
 */

#ifndef _CIFS_MD4_H
#define _CIFS_MD4_H

#include <linux/types.h>

#define MD4_DIGEST_SIZE		16
#define MD4_HMAC_BLOCK_SIZE	64
#define MD4_BLOCK_WORDS		16
#define MD4_HASH_WORDS		4

struct md4_ctx {
	u32 hash[MD4_HASH_WORDS];
	u32 block[MD4_BLOCK_WORDS];
	u64 byte_count;
};


int cifs_md4_init(struct md4_ctx *mctx);
int cifs_md4_update(struct md4_ctx *mctx, const u8 *data, unsigned int len);
int cifs_md4_final(struct md4_ctx *mctx, u8 *out);

#endif /* _CIFS_MD4_H */
