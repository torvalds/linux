/* SPDX-License-Identifier: GPL-2.0 OR MIT */

#ifndef _CRYPTO_BLAKE2B_H
#define _CRYPTO_BLAKE2B_H

#include <linux/bug.h>
#include <linux/types.h>
#include <linux/string.h>

enum blake2b_lengths {
	BLAKE2B_BLOCK_SIZE = 128,
	BLAKE2B_HASH_SIZE = 64,
	BLAKE2B_KEY_SIZE = 64,

	BLAKE2B_160_HASH_SIZE = 20,
	BLAKE2B_256_HASH_SIZE = 32,
	BLAKE2B_384_HASH_SIZE = 48,
	BLAKE2B_512_HASH_SIZE = 64,
};

struct blake2b_state {
	/* 'h', 't', and 'f' are used in assembly code, so keep them as-is. */
	u64 h[8];
	u64 t[2];
	u64 f[2];
	u8 buf[BLAKE2B_BLOCK_SIZE];
	unsigned int buflen;
	unsigned int outlen;
};

enum blake2b_iv {
	BLAKE2B_IV0 = 0x6A09E667F3BCC908ULL,
	BLAKE2B_IV1 = 0xBB67AE8584CAA73BULL,
	BLAKE2B_IV2 = 0x3C6EF372FE94F82BULL,
	BLAKE2B_IV3 = 0xA54FF53A5F1D36F1ULL,
	BLAKE2B_IV4 = 0x510E527FADE682D1ULL,
	BLAKE2B_IV5 = 0x9B05688C2B3E6C1FULL,
	BLAKE2B_IV6 = 0x1F83D9ABFB41BD6BULL,
	BLAKE2B_IV7 = 0x5BE0CD19137E2179ULL,
};

static inline void __blake2b_init(struct blake2b_state *state, size_t outlen,
				  const void *key, size_t keylen)
{
	state->h[0] = BLAKE2B_IV0 ^ (0x01010000 | keylen << 8 | outlen);
	state->h[1] = BLAKE2B_IV1;
	state->h[2] = BLAKE2B_IV2;
	state->h[3] = BLAKE2B_IV3;
	state->h[4] = BLAKE2B_IV4;
	state->h[5] = BLAKE2B_IV5;
	state->h[6] = BLAKE2B_IV6;
	state->h[7] = BLAKE2B_IV7;
	state->t[0] = 0;
	state->t[1] = 0;
	state->f[0] = 0;
	state->f[1] = 0;
	state->buflen = 0;
	state->outlen = outlen;
	if (keylen) {
		memcpy(state->buf, key, keylen);
		memset(&state->buf[keylen], 0, BLAKE2B_BLOCK_SIZE - keylen);
		state->buflen = BLAKE2B_BLOCK_SIZE;
	}
}

#endif /* _CRYPTO_BLAKE2B_H */
