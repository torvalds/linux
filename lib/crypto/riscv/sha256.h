/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-256 (RISC-V accelerated)
 *
 * Copyright (C) 2022 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_extensions);

asmlinkage void
sha256_transform_zvknha_or_zvknhb_zvkb(struct sha256_block_state *state,
				       const u8 *data, size_t nblocks);

static void sha256_blocks(struct sha256_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_extensions) && likely(may_use_simd())) {
		kernel_vector_begin();
		sha256_transform_zvknha_or_zvknhb_zvkb(state, data, nblocks);
		kernel_vector_end();
	} else {
		sha256_blocks_generic(state, data, nblocks);
	}
}

#define sha256_mod_init_arch sha256_mod_init_arch
static void sha256_mod_init_arch(void)
{
	/* Both zvknha and zvknhb provide the SHA-256 instructions. */
	if ((riscv_isa_extension_available(NULL, ZVKNHA) ||
	     riscv_isa_extension_available(NULL, ZVKNHB)) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&have_extensions);
}
