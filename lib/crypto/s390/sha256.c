// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SHA-256 optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2025 Google LLC
 */
#include <asm/cpacf.h>
#include <crypto/internal/sha2.h>
#include <linux/cpufeature.h>
#include <linux/kernel.h>
#include <linux/module.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_sha256);

void sha256_blocks_arch(u32 state[SHA256_STATE_WORDS],
			const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_cpacf_sha256))
		cpacf_kimd(CPACF_KIMD_SHA_256, state, data,
			   nblocks * SHA256_BLOCK_SIZE);
	else
		sha256_blocks_generic(state, data, nblocks);
}
EXPORT_SYMBOL_GPL(sha256_blocks_arch);

bool sha256_is_arch_optimized(void)
{
	return static_key_enabled(&have_cpacf_sha256);
}
EXPORT_SYMBOL_GPL(sha256_is_arch_optimized);

static int __init sha256_s390_mod_init(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_MSA) &&
	    cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_256))
		static_branch_enable(&have_cpacf_sha256);
	return 0;
}
subsys_initcall(sha256_s390_mod_init);

static void __exit sha256_s390_mod_exit(void)
{
}
module_exit(sha256_s390_mod_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SHA-256 using the CP Assist for Cryptographic Functions (CPACF)");
