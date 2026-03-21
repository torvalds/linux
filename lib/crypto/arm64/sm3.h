/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM3 optimized for ARM64
 *
 * Copyright 2026 Google LLC
 */
#include <asm/simd.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_ce);

asmlinkage void sm3_neon_transform(struct sm3_block_state *state,
				   const u8 *data, size_t nblocks);
asmlinkage void sm3_ce_transform(struct sm3_block_state *state,
				 const u8 *data, size_t nblocks);

static void sm3_blocks(struct sm3_block_state *state,
		       const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_neon) && likely(may_use_simd())) {
		scoped_ksimd() {
			if (static_branch_likely(&have_ce))
				sm3_ce_transform(state, data, nblocks);
			else
				sm3_neon_transform(state, data, nblocks);
		}
	} else {
		sm3_blocks_generic(state, data, nblocks);
	}
}

#define sm3_mod_init_arch sm3_mod_init_arch
static void sm3_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_neon);
		if (cpu_have_named_feature(SM3))
			static_branch_enable(&have_ce);
	}
}
