/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GHASH, RISC-V optimized
 *
 * Copyright (C) 2023 VRULL GmbH
 * Copyright (C) 2023 SiFive, Inc.
 * Copyright 2026 Google LLC
 */

#include <asm/simd.h>
#include <asm/vector.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_zvkg);

asmlinkage void ghash_zvkg(u8 accumulator[GHASH_BLOCK_SIZE],
			   const u8 key[GHASH_BLOCK_SIZE],
			   const u8 *data, size_t nblocks);

#define ghash_preparekey_arch ghash_preparekey_arch
static void ghash_preparekey_arch(struct ghash_key *key,
				  const u8 raw_key[GHASH_BLOCK_SIZE])
{
	/* Save key in POLYVAL format for fallback */
	ghash_key_to_polyval(raw_key, &key->h);

	/* Save key in GHASH format for zvkg */
	memcpy(key->h_raw, raw_key, GHASH_BLOCK_SIZE);
}

#define ghash_blocks_arch ghash_blocks_arch
static void ghash_blocks_arch(struct polyval_elem *acc,
			      const struct ghash_key *key,
			      const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_zvkg) && likely(may_use_simd())) {
		u8 ghash_acc[GHASH_BLOCK_SIZE];

		polyval_acc_to_ghash(acc, ghash_acc);

		kernel_vector_begin();
		ghash_zvkg(ghash_acc, key->h_raw, data, nblocks);
		kernel_vector_end();

		ghash_acc_to_polyval(ghash_acc, acc);
		memzero_explicit(ghash_acc, sizeof(ghash_acc));
	} else {
		ghash_blocks_generic(acc, &key->h, data, nblocks);
	}
}

#define gf128hash_mod_init_arch gf128hash_mod_init_arch
static void gf128hash_mod_init_arch(void)
{
	if (riscv_isa_extension_available(NULL, ZVKG) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&have_zvkg);
}
