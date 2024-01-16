/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/*
 * Helper functions for BLAKE2s implementations.
 * Keep this in sync with the corresponding BLAKE2b header.
 */

#ifndef _CRYPTO_INTERNAL_BLAKE2S_H
#define _CRYPTO_INTERNAL_BLAKE2S_H

#include <crypto/blake2s.h>
#include <linux/string.h>

void blake2s_compress_generic(struct blake2s_state *state, const u8 *block,
			      size_t nblocks, const u32 inc);

void blake2s_compress(struct blake2s_state *state, const u8 *block,
		      size_t nblocks, const u32 inc);

bool blake2s_selftest(void);

#endif /* _CRYPTO_INTERNAL_BLAKE2S_H */
