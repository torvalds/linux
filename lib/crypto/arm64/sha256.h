/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized for ARM64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/neon.h>
#include <crypto/internal/simd.h>
#include <linux/cpufeature.h>

asmlinkage void sha256_block_data_order(struct sha256_block_state *state,
					const u8 *data, size_t nblocks);
asmlinkage void sha256_block_neon(struct sha256_block_state *state,
				  const u8 *data, size_t nblocks);
asmlinkage size_t __sha256_ce_transform(struct sha256_block_state *state,
					const u8 *data, size_t nblocks);

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (IS_ENABLED(CONFIG_KERNEL_MODE_NEON) &&
	    static_branch_likely(&have_neon) && crypto_simd_usable()) {
		if (static_branch_likely(&have_ce)) {
			do {
				size_t rem;

				kernel_neon_begin();
				rem = __sha256_ce_transform(state,
							    data, nblocks);
				kernel_neon_end();
				data += (nblocks - rem) * SHA256_BLOCK_SIZE;
				nblocks = rem;
			} while (nblocks);
		} else {
			kernel_neon_begin();
			sha256_block_neon(state, data, nblocks);
			kernel_neon_end();
		}
	} else {
		sha256_block_data_order(state, data, nblocks);
	}
}

#ifdef CONFIG_KERNEL_MODE_NEON
#define sha256_mod_init_arch sha256_mod_init_arch
static inline void sha256_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_neon);
		if (cpu_have_named_feature(SHA2))
			static_branch_enable(&have_ce);
	}
}
#endif /* CONFIG_KERNEL_MODE_NEON */
