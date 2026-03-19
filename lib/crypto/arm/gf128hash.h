/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * GHASH, arm optimized
 *
 * Copyright 2026 Google LLC
 */

#include <asm/hwcap.h>
#include <asm/neon.h>
#include <asm/simd.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_neon);

void pmull_ghash_update_p8(size_t blocks, struct polyval_elem *dg,
			   const u8 *src, const struct polyval_elem *h);

#define ghash_blocks_arch ghash_blocks_arch
static void ghash_blocks_arch(struct polyval_elem *acc,
			      const struct ghash_key *key,
			      const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_neon) && may_use_simd()) {
		do {
			/* Allow rescheduling every 4 KiB. */
			size_t n =
				min_t(size_t, nblocks, 4096 / GHASH_BLOCK_SIZE);

			scoped_ksimd()
				pmull_ghash_update_p8(n, acc, data, &key->h);
			data += n * GHASH_BLOCK_SIZE;
			nblocks -= n;
		} while (nblocks);
	} else {
		ghash_blocks_generic(acc, &key->h, data, nblocks);
	}
}

#define gf128hash_mod_init_arch gf128hash_mod_init_arch
static void gf128hash_mod_init_arch(void)
{
	if (elf_hwcap & HWCAP_NEON)
		static_branch_enable(&have_neon);
}
