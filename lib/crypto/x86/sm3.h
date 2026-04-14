/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM3 optimized for x86_64
 *
 * Copyright 2026 Google LLC
 */
#include <asm/fpu/api.h>
#include <linux/static_call.h>

asmlinkage void sm3_transform_avx(struct sm3_block_state *state,
				  const u8 *data, size_t nblocks);

static void sm3_blocks_avx(struct sm3_block_state *state,
			   const u8 *data, size_t nblocks)
{
	if (likely(irq_fpu_usable())) {
		kernel_fpu_begin();
		sm3_transform_avx(state, data, nblocks);
		kernel_fpu_end();
	} else {
		sm3_blocks_generic(state, data, nblocks);
	}
}

DEFINE_STATIC_CALL(sm3_blocks_x86, sm3_blocks_generic);

static void sm3_blocks(struct sm3_block_state *state,
		       const u8 *data, size_t nblocks)
{
	static_call(sm3_blocks_x86)(state, data, nblocks);
}

#define sm3_mod_init_arch sm3_mod_init_arch
static void sm3_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_AVX) && boot_cpu_has(X86_FEATURE_BMI2) &&
	    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL))
		static_call_update(sm3_blocks_x86, sm3_blocks_avx);
}
