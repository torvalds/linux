/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <asm/simd.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha3);

asmlinkage size_t sha3_ce_transform(struct sha3_state *state, const u8 *data,
				    size_t nblocks, size_t block_size);

static void sha3_absorb_blocks(struct sha3_state *state, const u8 *data,
			       size_t nblocks, size_t block_size)
{
	if (static_branch_likely(&have_sha3) && likely(may_use_simd())) {
		do {
			size_t rem;

			scoped_ksimd()
				rem = sha3_ce_transform(state, data, nblocks,
							block_size);
			data += (nblocks - rem) * block_size;
			nblocks = rem;
		} while (nblocks);
	} else {
		sha3_absorb_blocks_generic(state, data, nblocks, block_size);
	}
}

static void sha3_keccakf(struct sha3_state *state)
{
	if (static_branch_likely(&have_sha3) && likely(may_use_simd())) {
		/*
		 * Passing zeroes into sha3_ce_transform() gives the plain
		 * Keccak-f permutation, which is what we want here.  Any
		 * supported block size may be used.  Use SHA3_512_BLOCK_SIZE
		 * since it's the shortest.
		 */
		static const u8 zeroes[SHA3_512_BLOCK_SIZE];

		scoped_ksimd()
			sha3_ce_transform(state, zeroes, 1, sizeof(zeroes));
	} else {
		sha3_keccakf_generic(state);
	}
}

#define sha3_mod_init_arch sha3_mod_init_arch
static void sha3_mod_init_arch(void)
{
	if (cpu_have_named_feature(SHA3))
		static_branch_enable(&have_sha3);
}
