/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _CRYPTO_BLAKE2S_H
#define _CRYPTO_BLAKE2S_H

#include <linux/bug.h>
#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/string.h>

enum blake2s_lengths {
	BLAKE2S_BLOCK_SIZE = 64,
	BLAKE2S_HASH_SIZE = 32,
	BLAKE2S_KEY_SIZE = 32,

	BLAKE2S_128_HASH_SIZE = 16,
	BLAKE2S_160_HASH_SIZE = 20,
	BLAKE2S_224_HASH_SIZE = 28,
	BLAKE2S_256_HASH_SIZE = 32,
};

struct blake2s_state {
	/* 'h', 't', and 'f' are used in assembly code, so keep them as-is. */
	u32 h[8];
	u32 t[2];
	u32 f[2];
	u8 buf[BLAKE2S_BLOCK_SIZE];
	unsigned int buflen;
	unsigned int outlen;
};

enum blake2s_iv {
	BLAKE2S_IV0 = 0x6A09E667UL,
	BLAKE2S_IV1 = 0xBB67AE85UL,
	BLAKE2S_IV2 = 0x3C6EF372UL,
	BLAKE2S_IV3 = 0xA54FF53AUL,
	BLAKE2S_IV4 = 0x510E527FUL,
	BLAKE2S_IV5 = 0x9B05688CUL,
	BLAKE2S_IV6 = 0x1F83D9ABUL,
	BLAKE2S_IV7 = 0x5BE0CD19UL,
};

static inline void __blake2s_init(struct blake2s_state *state, size_t outlen,
				  const void *key, size_t keylen)
{
	state->h[0] = BLAKE2S_IV0 ^ (0x01010000 | keylen << 8 | outlen);
	state->h[1] = BLAKE2S_IV1;
	state->h[2] = BLAKE2S_IV2;
	state->h[3] = BLAKE2S_IV3;
	state->h[4] = BLAKE2S_IV4;
	state->h[5] = BLAKE2S_IV5;
	state->h[6] = BLAKE2S_IV6;
	state->h[7] = BLAKE2S_IV7;
	state->t[0] = 0;
	state->t[1] = 0;
	state->f[0] = 0;
	state->f[1] = 0;
	state->buflen = 0;
	state->outlen = outlen;
	if (keylen) {
		memcpy(state->buf, key, keylen);
		memset(&state->buf[keylen], 0, BLAKE2S_BLOCK_SIZE - keylen);
		state->buflen = BLAKE2S_BLOCK_SIZE;
	}
}

static inline void blake2s_init(struct blake2s_state *state,
				const size_t outlen)
{
	__blake2s_init(state, outlen, NULL, 0);
}

static inline void blake2s_init_key(struct blake2s_state *state,
				    const size_t outlen, const void *key,
				    const size_t keylen)
{
	WARN_ON(IS_ENABLED(DEBUG) && (!outlen || outlen > BLAKE2S_HASH_SIZE ||
		!key || !keylen || keylen > BLAKE2S_KEY_SIZE));

	__blake2s_init(state, outlen, key, keylen);
}

void blake2s_update(struct blake2s_state *state, const u8 *in, size_t inlen);
void blake2s_final(struct blake2s_state *state, u8 *out);

static inline void blake2s(u8 *out, const u8 *in, const u8 *key,
			   const size_t outlen, const size_t inlen,
			   const size_t keylen)
{
	struct blake2s_state state;

	WARN_ON(IS_ENABLED(DEBUG) && ((!in && inlen > 0) || !out || !outlen ||
		outlen > BLAKE2S_HASH_SIZE || keylen > BLAKE2S_KEY_SIZE ||
		(!key && keylen)));

	__blake2s_init(&state, outlen, key, keylen);
	blake2s_update(&state, in, inlen);
	blake2s_final(&state, out);
}

#endif /* _CRYPTO_BLAKE2S_H */
