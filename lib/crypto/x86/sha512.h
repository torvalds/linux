/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * x86-optimized SHA-512 block function
 *
 * Copyright 2025 Google LLC
 */
#include <asm/fpu/api.h>
#include <linux/static_call.h>

DEFINE_STATIC_CALL(sha512_blocks_x86, sha512_blocks_generic);

#define DEFINE_X86_SHA512_FN(c_fn, asm_fn)                                 \
	asmlinkage void asm_fn(struct sha512_block_state *state,           \
			       const u8 *data, size_t nblocks);            \
	static void c_fn(struct sha512_block_state *state, const u8 *data, \
			 size_t nblocks)                                   \
	{                                                                  \
		if (likely(irq_fpu_usable())) {                            \
			kernel_fpu_begin();                                \
			asm_fn(state, data, nblocks);                      \
			kernel_fpu_end();                                  \
		} else {                                                   \
			sha512_blocks_generic(state, data, nblocks);       \
		}                                                          \
	}

DEFINE_X86_SHA512_FN(sha512_blocks_ssse3, sha512_transform_ssse3);
DEFINE_X86_SHA512_FN(sha512_blocks_avx, sha512_transform_avx);
DEFINE_X86_SHA512_FN(sha512_blocks_avx2, sha512_transform_rorx);

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	static_call(sha512_blocks_x86)(state, data, nblocks);
}

#define sha512_mod_init_arch sha512_mod_init_arch
static void sha512_mod_init_arch(void)
{
	if (cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM, NULL) &&
	    boot_cpu_has(X86_FEATURE_AVX)) {
		if (boot_cpu_has(X86_FEATURE_AVX2) &&
		    boot_cpu_has(X86_FEATURE_BMI2))
			static_call_update(sha512_blocks_x86,
					   sha512_blocks_avx2);
		else
			static_call_update(sha512_blocks_x86,
					   sha512_blocks_avx);
	} else if (boot_cpu_has(X86_FEATURE_SSSE3)) {
		static_call_update(sha512_blocks_x86, sha512_blocks_ssse3);
	}
}
