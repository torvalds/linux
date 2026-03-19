/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GHASH optimized using the CP Assist for Cryptographic Functions (CPACF)
 *
 * Copyright 2026 Google LLC
 */
#include <asm/cpacf.h>
#include <linux/cpufeature.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_cpacf_ghash);

#define ghash_preparekey_arch ghash_preparekey_arch
static void ghash_preparekey_arch(struct ghash_key *key,
				  const u8 raw_key[GHASH_BLOCK_SIZE])
{
	/* Save key in POLYVAL format for fallback */
	ghash_key_to_polyval(raw_key, &key->h);

	/* Save key in GHASH format for CPACF_KIMD_GHASH */
	memcpy(key->h_raw, raw_key, GHASH_BLOCK_SIZE);
}

#define ghash_blocks_arch ghash_blocks_arch
static void ghash_blocks_arch(struct polyval_elem *acc,
			      const struct ghash_key *key,
			      const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_cpacf_ghash)) {
		/*
		 * CPACF_KIMD_GHASH requires the accumulator and key in a single
		 * buffer, each using the GHASH convention.
		 */
		u8 ctx[2][GHASH_BLOCK_SIZE] __aligned(8);

		polyval_acc_to_ghash(acc, ctx[0]);
		memcpy(ctx[1], key->h_raw, GHASH_BLOCK_SIZE);

		cpacf_kimd(CPACF_KIMD_GHASH, ctx, data,
			   nblocks * GHASH_BLOCK_SIZE);

		ghash_acc_to_polyval(ctx[0], acc);
		memzero_explicit(ctx, sizeof(ctx));
	} else {
		ghash_blocks_generic(acc, &key->h, data, nblocks);
	}
}

#define gf128hash_mod_init_arch gf128hash_mod_init_arch
static void gf128hash_mod_init_arch(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_MSA) &&
	    cpacf_query_func(CPACF_KIMD, CPACF_KIMD_GHASH))
		static_branch_enable(&have_cpacf_ghash);
}
