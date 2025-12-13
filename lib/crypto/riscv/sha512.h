/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SHA-512 and SHA-384 using the RISC-V vector crypto extensions
 *
 * Copyright (C) 2023 VRULL GmbH
 * Author: Heiko Stuebner <heiko.stuebner@vrull.eu>
 *
 * Copyright (C) 2023 SiFive, Inc.
 * Author: Jerry Shih <jerry.shih@sifive.com>
 */

#include <asm/simd.h>
#include <asm/vector.h>

static __ro_after_init DEFINE_STATIC_KEY_FALSE(have_extensions);

asmlinkage void sha512_transform_zvknhb_zvkb(struct sha512_block_state *state,
					     const u8 *data, size_t nblocks);

static void sha512_blocks(struct sha512_block_state *state,
			  const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_extensions) && likely(may_use_simd())) {
		kernel_vector_begin();
		sha512_transform_zvknhb_zvkb(state, data, nblocks);
		kernel_vector_end();
	} else {
		sha512_blocks_generic(state, data, nblocks);
	}
}

#define sha512_mod_init_arch sha512_mod_init_arch
static void sha512_mod_init_arch(void)
{
	if (riscv_isa_extension_available(NULL, ZVKNHB) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&have_extensions);
}
