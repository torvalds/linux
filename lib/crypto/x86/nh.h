/* SPDX-License-Identifier: GPL-2.0 */
/*
 * x86_64 accelerated implementation of NH
 *
 * Copyright 2018 Google LLC
 */

#include <asm/fpu/api.h>
#include <linux/static_call.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_sse2);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_avx2);

asmlinkage void nh_sse2(const u32 *key, const u8 *message, size_t message_len,
			__le64 hash[NH_NUM_PASSES]);
asmlinkage void nh_avx2(const u32 *key, const u8 *message, size_t message_len,
			__le64 hash[NH_NUM_PASSES]);

static bool nh_arch(const u32 *key, const u8 *message, size_t message_len,
		    __le64 hash[NH_NUM_PASSES])
{
	if (message_len >= 64 && static_branch_likely(&have_sse2) &&
	    irq_fpu_usable()) {
		kernel_fpu_begin();
		if (static_branch_likely(&have_avx2))
			nh_avx2(key, message, message_len, hash);
		else
			nh_sse2(key, message, message_len, hash);
		kernel_fpu_end();
		return true;
	}
	return false;
}

#define nh_mod_init_arch nh_mod_init_arch
static void nh_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_XMM2)) {
		static_branch_enable(&have_sse2);
		if (boot_cpu_has(X86_FEATURE_AVX2) &&
		    cpu_has_xfeatures(XFEATURE_MASK_SSE | XFEATURE_MASK_YMM,
				      NULL))
			static_branch_enable(&have_avx2);
	}
}
