/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * BLAKE2b digest algorithm, NEON accelerated
 *
 * Copyright 2020 Google LLC
 */

#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

asmlinkage void blake2b_compress_neon(struct blake2b_ctx *ctx,
				      const u8 *data, size_t nblocks, u32 inc);

static void blake2b_compress(struct blake2b_ctx *ctx,
			     const u8 *data, size_t nblocks, u32 inc)
{
	if (!static_branch_likely(&have_neon) || !may_use_simd()) {
		blake2b_compress_generic(ctx, data, nblocks, inc);
		return;
	}
	do {
		const size_t blocks = min_t(size_t, nblocks,
					    SZ_4K / BLAKE2B_BLOCK_SIZE);

		scoped_ksimd()
			blake2b_compress_neon(ctx, data, blocks, inc);

		data += blocks * BLAKE2B_BLOCK_SIZE;
		nblocks -= blocks;
	} while (nblocks);
}

#define blake2b_mod_init_arch blake2b_mod_init_arch
static void blake2b_mod_init_arch(void)
{
	if (elf_hwcap & HWCAP_NEON)
		static_branch_enable(&have_neon);
}
