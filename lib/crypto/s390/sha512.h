/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-512 optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2025 Google LLC
 */
#include <asm/cpacf.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_sha512);

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_cpacf_sha512))
		cpacf_kimd(CPACF_KIMD_SHA_512, state, data,
			   nblocks * SHA512_BLOCK_SIZE);
	else
		sha512_blocks_generic(state, data, nblocks);
}

#define sha512_mod_init_arch sha512_mod_init_arch
static void sha512_mod_init_arch(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_MSA) &&
	    cpacf_query_func(CPACF_KIMD, CPACF_KIMD_SHA_512))
		static_branch_enable(&have_cpacf_sha512);
}
