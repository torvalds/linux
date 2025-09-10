/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized for x86_64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/fpu/api.h>
#include <crypto/internal/simd.h>
#include <linux/static_call.h>

DEFINE_STATIC_CALL(sha256_blocks_x86, sha256_blocks_generic);

#define DEFINE_X86_SHA256_FN(c_fn, asm_fn)                                 \
	asmlinkage void asm_fn(struct sha256_block_state *state,           \
			       const u8 *data, size_t nblocks);            \
	static void c_fn(struct sha256_block_state *state, const u8 *data, \
			 size_t nblocks)                                   \
	{                                                                  \
		if (likely(crypto_simd_usable())) {                        \
			kernel_fpu_begin();                                \
			asm_fn(state, data, nblocks);                      \
			kernel_fpu_end();                                  \
		} else {                                                   \
			sha256_blocks_generic(state, data, nblocks);       \
		}                                                          \
	}

DEFINE_X86_SHA256_FN(sha256_blocks_ssse3, sha256_transform_ssse3);
DEFINE_X86_SHA256_FN(sha256_blocks_avx, sha256_transform_avx);
DEFINE_X86_SHA256_FN(sha256_blocks_avx2, sha256_transform_rorx);
DEFINE_X86_SHA256_FN(sha256_blocks_ni, sha256_ni_transform);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	static_call(sha256_blocks_x86)(state, data, nblocks);
}

#define sha256_mod_init_arch sha256_mod_init_arch
static inline void sha256_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI)) {
		static_call_update(sha256_blocks_x86, sha256_blocks_ni);
	} else if (cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				     NULL) &&
		   boot_cpu_has(X86_FEATURE_AVX)) {
		if (boot_cpu_has(X86_FEATURE_AVX2) &&
		    boot_cpu_has(X86_FEATURE_BMI2))
			static_call_update(sha256_blocks_x86,
					   sha256_blocks_avx2);
		else
			static_call_update(sha256_blocks_x86,
					   sha256_blocks_avx);
	} else if (boot_cpu_has(X86_FEATURE_SSSE3)) {
		static_call_update(sha256_blocks_x86, sha256_blocks_ssse3);
	}
}
