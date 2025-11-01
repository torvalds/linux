/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-1 optimized for x86_64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/fpu/api.h>
#include <linux/static_call.h>

DEFINE_STATIC_CALL(sha1_blocks_x86, sha1_blocks_generic);

#define DEFINE_X86_SHA1_FN(c_fn, asm_fn)                           \
	asmlinkage void asm_fn(struct sha1_block_state *state,     \
			       const u8 *data, size_t nblocks);    \
	static void c_fn(struct sha1_block_state *state,           \
			 const u8 *data, size_t nblocks)           \
	{                                                          \
		if (likely(irq_fpu_usable())) {                    \
			kernel_fpu_begin();                        \
			asm_fn(state, data, nblocks);              \
			kernel_fpu_end();                          \
		} else {                                           \
			sha1_blocks_generic(state, data, nblocks); \
		}                                                  \
	}

DEFINE_X86_SHA1_FN(sha1_blocks_ssse3, sha1_transform_ssse3);
DEFINE_X86_SHA1_FN(sha1_blocks_avx, sha1_transform_avx);
DEFINE_X86_SHA1_FN(sha1_blocks_ni, sha1_ni_transform);

#define SHA1_AVX2_BLOCK_OPTSIZE 4 /* optimal 4*64 bytes of SHA1 blocks */

asmlinkage void sha1_transform_avx2(struct sha1_block_state *state,
				    const u8 *data, size_t nblocks);
static void sha1_blocks_avx2(struct sha1_block_state *state,
			     const u8 *data, size_t nblocks)
{
	if (likely(irq_fpu_usable())) {
		kernel_fpu_begin();
		/* Select the optimal transform based on the number of blocks */
		if (nblocks >= SHA1_AVX2_BLOCK_OPTSIZE)
			sha1_transform_avx2(state, data, nblocks);
		else
			sha1_transform_avx(state, data, nblocks);
		kernel_fpu_end();
	} else {
		sha1_blocks_generic(state, data, nblocks);
	}
}

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	static_call(sha1_blocks_x86)(state, data, nblocks);
}

#define sha1_mod_init_arch sha1_mod_init_arch
static void sha1_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI)) {
		static_call_update(sha1_blocks_x86, sha1_blocks_ni);
	} else if (cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				     NULL) &&
		   boot_cpu_has(X86_FEATURE_AVX)) {
		if (boot_cpu_has(X86_FEATURE_AVX2) &&
		    boot_cpu_has(X86_FEATURE_BMI1) &&
		    boot_cpu_has(X86_FEATURE_BMI2))
			static_call_update(sha1_blocks_x86, sha1_blocks_avx2);
		else
			static_call_update(sha1_blocks_x86, sha1_blocks_avx);
	} else if (boot_cpu_has(X86_FEATURE_SSSE3)) {
		static_call_update(sha1_blocks_x86, sha1_blocks_ssse3);
	}
}
