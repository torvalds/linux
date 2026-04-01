/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GHASH and POLYVAL, arm64 optimized
 *
 * Copyright 2025 Google LLC
 */
#include <asm/simd.h>
#include <linux/cpufeature.h>

#define NUM_H_POWERS 8

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_asimd);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pmull);

asmlinkage void pmull_ghash_update_p8(size_t blocks, struct polyval_elem *dg,
				      const u8 *src,
				      const struct polyval_elem *h);
asmlinkage void polyval_mul_pmull(struct polyval_elem *a,
				  const struct polyval_elem *b);
asmlinkage void polyval_blocks_pmull(struct polyval_elem *acc,
				     const struct polyval_key *key,
				     const u8 *data, size_t nblocks);

#define polyval_preparekey_arch polyval_preparekey_arch
static void polyval_preparekey_arch(struct polyval_key *key,
				    const u8 raw_key[POLYVAL_BLOCK_SIZE])
{
	static_assert(ARRAY_SIZE(key->h_powers) == NUM_H_POWERS);
	memcpy(&key->h_powers[NUM_H_POWERS - 1], raw_key, POLYVAL_BLOCK_SIZE);
	if (static_branch_likely(&have_pmull) && may_use_simd()) {
		scoped_ksimd() {
			for (int i = NUM_H_POWERS - 2; i >= 0; i--) {
				key->h_powers[i] = key->h_powers[i + 1];
				polyval_mul_pmull(
					&key->h_powers[i],
					&key->h_powers[NUM_H_POWERS - 1]);
			}
		}
	} else {
		for (int i = NUM_H_POWERS - 2; i >= 0; i--) {
			key->h_powers[i] = key->h_powers[i + 1];
			polyval_mul_generic(&key->h_powers[i],
					    &key->h_powers[NUM_H_POWERS - 1]);
		}
	}
}

static void polyval_mul_arm64(struct polyval_elem *a,
			      const struct polyval_elem *b)
{
	if (static_branch_likely(&have_asimd) && may_use_simd()) {
		static const u8 zeroes[GHASH_BLOCK_SIZE];

		scoped_ksimd() {
			if (static_branch_likely(&have_pmull)) {
				polyval_mul_pmull(a, b);
			} else {
				/*
				 * Note that this is indeed equivalent to a
				 * POLYVAL multiplication, since it takes the
				 * accumulator and key in POLYVAL format, and
				 * byte-swapping a block of zeroes is a no-op.
				 */
				pmull_ghash_update_p8(1, a, zeroes, b);
			}
		}
	} else {
		polyval_mul_generic(a, b);
	}
}

#define ghash_mul_arch ghash_mul_arch
static void ghash_mul_arch(struct polyval_elem *acc,
			   const struct ghash_key *key)
{
	polyval_mul_arm64(acc, &key->h);
}

#define polyval_mul_arch polyval_mul_arch
static void polyval_mul_arch(struct polyval_elem *acc,
			     const struct polyval_key *key)
{
	polyval_mul_arm64(acc, &key->h_powers[NUM_H_POWERS - 1]);
}

#define ghash_blocks_arch ghash_blocks_arch
static void ghash_blocks_arch(struct polyval_elem *acc,
			      const struct ghash_key *key,
			      const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_asimd) && may_use_simd()) {
		scoped_ksimd()
			pmull_ghash_update_p8(nblocks, acc, data, &key->h);
	} else {
		ghash_blocks_generic(acc, &key->h, data, nblocks);
	}
}

#define polyval_blocks_arch polyval_blocks_arch
static void polyval_blocks_arch(struct polyval_elem *acc,
				const struct polyval_key *key,
				const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_pmull) && may_use_simd()) {
		scoped_ksimd()
			polyval_blocks_pmull(acc, key, data, nblocks);
	} else {
		polyval_blocks_generic(acc, &key->h_powers[NUM_H_POWERS - 1],
				       data, nblocks);
	}
}

#define gf128hash_mod_init_arch gf128hash_mod_init_arch
static void gf128hash_mod_init_arch(void)
{
	if (cpu_have_named_feature(ASIMD)) {
		static_branch_enable(&have_asimd);
		if (cpu_have_named_feature(PMULL))
			static_branch_enable(&have_pmull);
	}
}
