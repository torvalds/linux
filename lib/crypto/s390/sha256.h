/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2025 Google LLC
 */
#include <asm/cpacf.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_sha256);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_cpacf_sha256))
		cpacf_kimd(CPACF_KIMD_SHA_256, state, data,
			   nblocks * SHA256_BLOCK_SIZE);
	else
		sha256_blocks_generic(state, data, nblocks);
}

#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_MSA) &&
	    cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_256))
		static_branch_enable(&have_cpacf_sha256);
}
