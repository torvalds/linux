/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GHASH and POLYVAL, x86_64 optimized
 *
 * Copyright 2025 Google LLC
 */
#include <asm/fpu/api.h>
#include <linux/cpufeature.h>

#define NUM_H_POWERS 8

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pclmul);
static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_pclmul_avx);

asmlinkage void polyval_mul_pclmul(struct polyval_elem *a,
				   const struct polyval_elem *b);
asmlinkage void polyval_mul_pclmul_avx(struct polyval_elem *a,
				       const struct polyval_elem *b);

asmlinkage void ghash_blocks_pclmul(struct polyval_elem *acc,
				    const struct polyval_elem *key,
				    const u8 *data, size_t nblocks);
asmlinkage void polyval_blocks_pclmul_avx(struct polyval_elem *acc,
					  const struct polyval_key *key,
					  const u8 *data, size_t nblocks);

#define polyval_preparekey_arch polyval_preparekey_arch
static void polyval_preparekey_arch(struct polyval_key *key,
				    const u8 raw_key[POLYVAL_BLOCK_SIZE])
{
	static_assert(ARRAY_SIZE(key->h_powers) == NUM_H_POWERS);
	memcpy(&key->h_powers[NUM_H_POWERS - 1], raw_key, POLYVAL_BLOCK_SIZE);
	if (static_branch_likely(&have_pclmul_avx) && irq_fpu_usable()) {
		kernel_fpu_begin();
		for (int i = NUM_H_POWERS - 2; i >= 0; i--) {
			key->h_powers[i] = key->h_powers[i + 1];
			polyval_mul_pclmul_avx(
				&key->h_powers[i],
				&key->h_powers[NUM_H_POWERS - 1]);
		}
		kernel_fpu_end();
	} else {
		for (int i = NUM_H_POWERS - 2; i >= 0; i--) {
			key->h_powers[i] = key->h_powers[i + 1];
			polyval_mul_generic(&key->h_powers[i],
					    &key->h_powers[NUM_H_POWERS - 1]);
		}
	}
}

static void polyval_mul_x86(struct polyval_elem *a,
			    const struct polyval_elem *b)
{
	if (static_branch_likely(&have_pclmul) && irq_fpu_usable()) {
		kernel_fpu_begin();
		if (static_branch_likely(&have_pclmul_avx))
			polyval_mul_pclmul_avx(a, b);
		else
			polyval_mul_pclmul(a, b);
		kernel_fpu_end();
	} else {
		polyval_mul_generic(a, b);
	}
}

#define ghash_mul_arch ghash_mul_arch
static void ghash_mul_arch(struct polyval_elem *acc,
			   const struct ghash_key *key)
{
	polyval_mul_x86(acc, &key->h);
}

#define polyval_mul_arch polyval_mul_arch
static void polyval_mul_arch(struct polyval_elem *acc,
			     const struct polyval_key *key)
{
	polyval_mul_x86(acc, &key->h_powers[NUM_H_POWERS - 1]);
}

#define ghash_blocks_arch ghash_blocks_arch
static void ghash_blocks_arch(struct polyval_elem *acc,
			      const struct ghash_key *key,
			      const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_pclmul) && irq_fpu_usable()) {
		do {
			/* Allow rescheduling every 4 KiB. */
			size_t n = min_t(size_t, nblocks,
					 4096 / GHASH_BLOCK_SIZE);

			kernel_fpu_begin();
			ghash_blocks_pclmul(acc, &key->h, data, n);
			kernel_fpu_end();
			data += n * GHASH_BLOCK_SIZE;
			nblocks -= n;
		} while (nblocks);
	} else {
		ghash_blocks_generic(acc, &key->h, data, nblocks);
	}
}

#define polyval_blocks_arch polyval_blocks_arch
static void polyval_blocks_arch(struct polyval_elem *acc,
				const struct polyval_key *key,
				const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_pclmul_avx) && irq_fpu_usable()) {
		do {
			/* Allow rescheduling every 4 KiB. */
			size_t n = min_t(size_t, nblocks,
					 4096 / POLYVAL_BLOCK_SIZE);

			kernel_fpu_begin();
			polyval_blocks_pclmul_avx(acc, key, data, n);
			kernel_fpu_end();
			data += n * POLYVAL_BLOCK_SIZE;
			nblocks -= n;
		} while (nblocks);
	} else {
		polyval_blocks_generic(acc, &key->h_powers[NUM_H_POWERS - 1],
				       data, nblocks);
	}
}

#define gf128hash_mod_init_arch gf128hash_mod_init_arch
static void gf128hash_mod_init_arch(void)
{
	if (boot_cpu_has(X86_FEATURE_PCLMULQDQ)) {
		static_branch_enable(&have_pclmul);
		if (boot_cpu_has(X86_FEATURE_AVX))
			static_branch_enable(&have_pclmul_avx);
	}
}
