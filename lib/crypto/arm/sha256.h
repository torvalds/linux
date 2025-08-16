/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized for ARM
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

asmlinkage void sha256_block_data_order(struct sha256_block_state *state,
					const u8 *data, size_t nblocks);
asmlinkage void sha256_block_data_order_neon(struct sha256_block_state *state,
					     const u8 *data, size_t nblocks);
asmlinkage void sha256_ce_transform(struct sha256_block_state *state,
				    const u8 *data, size_t nblocks);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon) && likely(may_use_simd())) {
		kernel_neon_begin();
		if (static_branch_likely(&have_ce))
			sha256_ce_transform(state, data, nblocks);
		else
			sha256_block_data_order_neon(state, data, nblocks);
		kernel_neon_end();
	} else {
		sha256_block_data_order(state, data, nblocks);
	}
}

#ifdef CONFIG_KERNEL_MODE_NEON
#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	if (elf_hwcap & HWCAP_NEON) {
		static_branch_enable(&have_neon);
		if (elf_hwcap2 & HWCAP2_SHA2)
			static_branch_enable(&have_ce);
	}
}
#endif /* CONFIG_KERNEL_MODE_NEON */
