/* SPDX-License-Identifier: GPL-2.0 */
/*
 * GHASH routines supporting VMX instructions on the Power 8
 *
 * Copyright (C) 2015, 2019 International Business Machines Inc.
 * Copyright (C) 2014 - 2018 Linaro Ltd.
 * Copyright 2026 Google LLC
 */

#include <asm/simd.h>
#include <asm/switch_to.h>
#include <linux/cpufeature.h>
#include <linux/jump_label.h>
#include <linux/preempt.h>
#include <linux/uaccess.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_vec_crypto);

void gcm_init_p8(u64 htable[4][2], const u8 h[16]);
void gcm_gmult_p8(u8 Xi[16], const u64 htable[4][2]);
void gcm_ghash_p8(u8 Xi[16], const u64 htable[4][2], const u8 *in, size_t len);

#define ghash_preparekey_arch ghash_preparekey_arch
static void ghash_preparekey_arch(struct ghash_key *key,
				  const u8 raw_key[GHASH_BLOCK_SIZE])
{
	ghash_key_to_polyval(raw_key, &key->h);

	if (static_branch_likely(&have_vec_crypto) && likely(may_use_simd())) {
		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		gcm_init_p8(key->htable, raw_key);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();
	} else {
		/* This reproduces gcm_init_p8() on both LE and BE systems. */
		key->htable[0][0] = 0;
		key->htable[0][1] = 0xc200000000000000;

		key->htable[1][0] = 0;
		key->htable[1][1] = le64_to_cpu(key->h.lo);

		key->htable[2][0] = le64_to_cpu(key->h.lo);
		key->htable[2][1] = le64_to_cpu(key->h.hi);

		key->htable[3][0] = le64_to_cpu(key->h.hi);
		key->htable[3][1] = 0;
	}
}

#define ghash_mul_arch ghash_mul_arch
static void ghash_mul_arch(struct polyval_elem *acc,
			   const struct ghash_key *key)
{
	if (static_branch_likely(&have_vec_crypto) && likely(may_use_simd())) {
		u8 ghash_acc[GHASH_BLOCK_SIZE];

		polyval_acc_to_ghash(acc, ghash_acc);

		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		gcm_gmult_p8(ghash_acc, key->htable);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();

		ghash_acc_to_polyval(ghash_acc, acc);
		memzero_explicit(ghash_acc, sizeof(ghash_acc));
	} else {
		polyval_mul_generic(acc, &key->h);
	}
}

#define ghash_blocks_arch ghash_blocks_arch
static void ghash_blocks_arch(struct polyval_elem *acc,
			      const struct ghash_key *key,
			      const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_vec_crypto) && likely(may_use_simd())) {
		u8 ghash_acc[GHASH_BLOCK_SIZE];

		polyval_acc_to_ghash(acc, ghash_acc);

		preempt_disable();
		pagefault_disable();
		enable_kernel_vsx();
		gcm_ghash_p8(ghash_acc, key->htable, data,
			     nblocks * GHASH_BLOCK_SIZE);
		disable_kernel_vsx();
		pagefault_enable();
		preempt_enable();

		ghash_acc_to_polyval(ghash_acc, acc);
		memzero_explicit(ghash_acc, sizeof(ghash_acc));
	} else {
		ghash_blocks_generic(acc, &key->h, data, nblocks);
	}
}

#define gf128hash_mod_init_arch gf128hash_mod_init_arch
static void gf128hash_mod_init_arch(void)
{
	if (cpu_has_feature(CPU_FTR_ARCH_207S) &&
	    (cur_cpu_spec->cpu_user_features2 & PPC_FEATURE2_VEC_CRYPTO))
		static_branch_enable(&have_vec_crypto);
}
