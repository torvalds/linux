/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized for x86_64
 *
 * Copyright 2025 Google LLC
 */
#include <asm/fpu/api.h>
#include <linux/static_call.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sha_ni);

DEFINE_STATIC_CALL(sha256_blocks_x86, sha256_blocks_generic);

#define DEFINE_X86_SHA256_FN(c_fn, asm_fn)                                 \
	asmlinkage void asm_fn(struct sha256_block_state *state,           \
			       const u8 *data, size_t nblocks);            \
	static void c_fn(struct sha256_block_state *state, const u8 *data, \
			 size_t nblocks)                                   \
	{                                                                  \
		if (likely(irq_fpu_usable())) {                            \
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

static_assert(offsetof(struct __sha256_ctx, state) == 0);
static_assert(offsetof(struct __sha256_ctx, bytecount) == 32);
static_assert(offsetof(struct __sha256_ctx, buf) == 40);
asmlinkage void sha256_ni_finup2x(const struct __sha256_ctx *ctx,
				  const u8 *data1, const u8 *data2, int len,
				  u8 out1[SHA256_DIGEST_SIZE],
				  u8 out2[SHA256_DIGEST_SIZE]);

#define sha256_finup_2x_arch sha256_finup_2x_arch
static bool sha256_finup_2x_arch(const struct __sha256_ctx *ctx,
				 const u8 *data1, const u8 *data2, size_t len,
				 u8 out1[SHA256_DIGEST_SIZE],
				 u8 out2[SHA256_DIGEST_SIZE])
{
	/*
	 * The assembly requires len >= SHA256_BLOCK_SIZE && len <= INT_MAX.
	 * Further limit len to 65536 to avoid spending too long with preemption
	 * disabled.  (Of course, in practice len is nearly always 4096 anyway.)
	 */
	if (static_branch_likely(&have_sha_ni) && len >= SHA256_BLOCK_SIZE &&
	    len <= 65536 && likely(irq_fpu_usable())) {
		kernel_fpu_begin();
		sha256_ni_finup2x(ctx, data1, data2, len, out1, out2);
		kernel_fpu_end();
		kmsan_unpoison_memory(out1, SHA256_DIGEST_SIZE);
		kmsan_unpoison_memory(out2, SHA256_DIGEST_SIZE);
		return true;
	}
	return false;
}

static bool sha256_finup_2x_is_optimized_arch(void)
{
	return static_key_enabled(&have_sha_ni);
}

#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_SHA_NI)) {
		static_call_update(sha256_blocks_x86, sha256_blocks_ni);
		static_branch_enable(&have_sha_ni);
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
