/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arm64-optimized SHA-512 block function
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <asm/simd.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha512_insns);

asmlinkage void sha512_block_data_order(struct sha512_block_state *state,
					const u8 *data, size_t nblocks);
asmlinkage size_t __sha512_ce_transform(struct sha512_block_state *state,
					const u8 *data, size_t nblocks);

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_sha512_insns) &&
	    likely(may_use_simd())) {
		do {
			size_t rem;

			kernel_neon_begin();
			rem = __sha512_ce_transform(state, data, nblocks);
			kernel_neon_end();
			data += (nblocks - rem) * SHA512_BLOCK_SIZE;
			nblocks = rem;
		} while (nblocks);
	} else {
		sha512_block_data_order(state, data, nblocks);
	}
}

#ifdef CONFIG_KERNEL_MODE_NEON
#define sha512_mod_init_arch sha512_mod_init_arch
static void sha512_mod_init_arch(void)
{
	if (cpu_have_named_feature(SHA512))
		static_branch_enable(&have_sha512_insns);
}
#endif /* CONFIG_KERNEL_MODE_NEON */
