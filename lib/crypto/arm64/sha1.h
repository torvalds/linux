/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-1 optimized for ARM64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

asmlinkage size_t __sha1_ce_transform(struct sha1_block_state *state,
				      const u8 *data, size_t nblocks);

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_ce) && likely(may_use_simd())) {
		do {
			size_t rem;

			kernel_neon_begin();
			rem = __sha1_ce_transform(state, data, nblocks);
			kernel_neon_end();
			data += (nblocks - rem) * SHA1_BLOCK_SIZE;
			nblocks = rem;
		} while (nblocks);
	} else {
		sha1_blocks_generic(state, data, nblocks);
	}
}

#define sha1_mod_init_arch sha1_mod_init_arch
static void sha1_mod_init_arch(void)
{
	if (cpu_have_named_feature(SHA1))
		static_branch_enable(&have_ce);
}
