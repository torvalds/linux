/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * POLYVAL library functions, arm64 optimized
 *
 * Copyright 2025 Google LLC
 */
#include <asm/simd.h>
#include <linux/cpufeature.h>

#define NUM_H_POWERS 8

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pmull);

asmlinkage void polyval_mul_pmull(struct polyval_elem *a,
				  const struct polyval_elem *b);
asmlinkage void polyval_blocks_pmull(struct polyval_elem *acc,
				     const struct polyval_key *key,
				     const u8 *data, size_t nblocks);

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

static void polyval_mul_arch(struct polyval_elem *acc,
			     const struct polyval_key *key)
{
	if (static_branch_likely(&have_pmull) && may_use_simd()) {
		scoped_ksimd()
			polyval_mul_pmull(acc, &key->h_powers[NUM_H_POWERS - 1]);
	} else {
		polyval_mul_generic(acc, &key->h_powers[NUM_H_POWERS - 1]);
	}
}

static void polyval_blocks_arch(struct polyval_elem *acc,
				const struct polyval_key *key,
				const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_pmull) && may_use_simd()) {
		do {
			/* Allow rescheduling every 4 KiB. */
			size_t n = min_t(size_t, nblocks,
					 4096 / POLYVAL_BLOCK_SIZE);

			scoped_ksimd()
				polyval_blocks_pmull(acc, key, data, n);
			data += n * POLYVAL_BLOCK_SIZE;
			nblocks -= n;
		} while (nblocks);
	} else {
		polyval_blocks_generic(acc, &key->h_powers[NUM_H_POWERS - 1],
				       data, nblocks);
	}
}

#define polyval_mod_init_arch polyval_mod_init_arch
static void polyval_mod_init_arch(void)
{
	if (cpu_have_named_feature(PMULL))
		static_branch_enable(&have_pmull);
}
