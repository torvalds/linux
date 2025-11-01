/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arm32-optimized SHA-512 block function
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

asmlinkage void sha512_block_data_order(struct sha512_block_state *state,
					const u8 *data, size_t nblocks);
asmlinkage void sha512_block_data_order_neon(struct sha512_block_state *state,
					     const u8 *data, size_t nblocks);

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon) && likely(may_use_simd())) {
		kernel_neon_begin();
		sha512_block_data_order_neon(state, data, nblocks);
		kernel_neon_end();
	} else {
		sha512_block_data_order(state, data, nblocks);
	}
}

#ifdef CONFIG_KERNEL_MODE_NEON
#define sha512_mod_init_arch sha512_mod_init_arch
static void sha512_mod_init_arch(void)
{
	if (cpu_has_neon())
		static_branch_enable(&have_neon);
}
#endif /* CONFIG_KERNEL_MODE_NEON */
