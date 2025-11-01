/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-1 optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2025 Google LLC
 */
#include <asm/cpacf.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_sha1);

static void sha1_blocks(struct sha1_block_state *state,
			const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_cpacf_sha1))
		cpacf_kimd(CPACF_KIMD_SHA_1, state, data,
			   nblocks * SHA1_BLOCK_SIZE);
	else
		sha1_blocks_generic(state, data, nblocks);
}

#define sha1_mod_init_arch sha1_mod_init_arch
static void sha1_mod_init_arch(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_MSA) &&
	    cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_1))
		static_branch_enable(&have_cpacf_sha1);
}
