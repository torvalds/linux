// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is an implementation of the BLAKE2s hash and PRF functions.
 *
 * Information: https://blake2.net/
 *
 */

#include <crypto/internal/blake2s.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bug.h>
#include <asm/unaligned.h>

void blake2s_update(struct blake2s_state *state, const u8 *in, size_t inlen)
{
	const size_t fill = BLAKE2S_BLOCK_SIZE - state->buflen;

	if (unlikely(!inlen))
		return;
	if (inlen > fill) {
		memcpy(state->buf + state->buflen, in, fill);
		if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_BLAKE2S))
			blake2s_compress_arch(state, state->buf, 1,
					      BLAKE2S_BLOCK_SIZE);
		else
			blake2s_compress_generic(state, state->buf, 1,
						 BLAKE2S_BLOCK_SIZE);
		state->buflen = 0;
		in += fill;
		inlen -= fill;
	}
	if (inlen > BLAKE2S_BLOCK_SIZE) {
		const size_t nblocks = DIV_ROUND_UP(inlen, BLAKE2S_BLOCK_SIZE);
		/* Hash one less (full) block than strictly possible */
		if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_BLAKE2S))
			blake2s_compress_arch(state, in, nblocks - 1,
					      BLAKE2S_BLOCK_SIZE);
		else
			blake2s_compress_generic(state, in, nblocks - 1,
						 BLAKE2S_BLOCK_SIZE);
		in += BLAKE2S_BLOCK_SIZE * (nblocks - 1);
		inlen -= BLAKE2S_BLOCK_SIZE * (nblocks - 1);
	}
	memcpy(state->buf + state->buflen, in, inlen);
	state->buflen += inlen;
}
EXPORT_SYMBOL(blake2s_update);

void blake2s_final(struct blake2s_state *state, u8 *out)
{
	WARN_ON(IS_ENABLED(DEBUG) && !out);
	blake2s_set_lastblock(state);
	memset(state->buf + state->buflen, 0,
	       BLAKE2S_BLOCK_SIZE - state->buflen); /* Padding */
	if (IS_ENABLED(CONFIG_CRYPTO_ARCH_HAVE_LIB_BLAKE2S))
		blake2s_compress_arch(state, state->buf, 1, state->buflen);
	else
		blake2s_compress_generic(state, state->buf, 1, state->buflen);
	cpu_to_le32_array(state->h, ARRAY_SIZE(state->h));
	memcpy(out, state->h, state->outlen);
	memzero_explicit(state, sizeof(*state));
}
EXPORT_SYMBOL(blake2s_final);

void blake2s256_hmac(u8 *out, const u8 *in, const u8 *key, const size_t inlen,
		     const size_t keylen)
{
	struct blake2s_state state;
	u8 x_key[BLAKE2S_BLOCK_SIZE] __aligned(__alignof__(u32)) = { 0 };
	u8 i_hash[BLAKE2S_HASH_SIZE] __aligned(__alignof__(u32));
	int i;

	if (keylen > BLAKE2S_BLOCK_SIZE) {
		blake2s_init(&state, BLAKE2S_HASH_SIZE);
		blake2s_update(&state, key, keylen);
		blake2s_final(&state, x_key);
	} else
		memcpy(x_key, key, keylen);

	for (i = 0; i < BLAKE2S_BLOCK_SIZE; ++i)
		x_key[i] ^= 0x36;

	blake2s_init(&state, BLAKE2S_HASH_SIZE);
	blake2s_update(&state, x_key, BLAKE2S_BLOCK_SIZE);
	blake2s_update(&state, in, inlen);
	blake2s_final(&state, i_hash);

	for (i = 0; i < BLAKE2S_BLOCK_SIZE; ++i)
		x_key[i] ^= 0x5c ^ 0x36;

	blake2s_init(&state, BLAKE2S_HASH_SIZE);
	blake2s_update(&state, x_key, BLAKE2S_BLOCK_SIZE);
	blake2s_update(&state, i_hash, BLAKE2S_HASH_SIZE);
	blake2s_final(&state, i_hash);

	memcpy(out, i_hash, BLAKE2S_HASH_SIZE);
	memzero_explicit(x_key, BLAKE2S_BLOCK_SIZE);
	memzero_explicit(i_hash, BLAKE2S_HASH_SIZE);
}
EXPORT_SYMBOL(blake2s256_hmac);

static int __init mod_init(void)
{
	if (!IS_ENABLED(CONFIG_CRYPTO_MANAGER_DISABLE_TESTS) &&
	    WARN_ON(!blake2s_selftest()))
		return -ENODEV;
	return 0;
}

static void __exit mod_exit(void)
{
}

module_init(mod_init);
module_exit(mod_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("BLAKE2s hash function");
MODULE_AUTHOR("Jason A. Donenfeld <Jason@zx2c4.com>");
