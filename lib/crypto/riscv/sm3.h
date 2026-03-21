/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SM3 using the RISC-V vector crypto extensions
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

asmlinkage void sm3_transform_zvksh_zvkb(struct sm3_block_state *state,
					 const u8 *data, size_t nblocks);

static void sm3_blocks(struct sm3_block_state *state,
		       const u8 *data, size_t nblocks)
{
	if (static_branch_likely(&have_extensions) && likely(may_use_simd())) {
		kernel_vector_begin();
		sm3_transform_zvksh_zvkb(state, data, nblocks);
		kernel_vector_end();
	} else {
		sm3_blocks_generic(state, data, nblocks);
	}
}

#define sm3_mod_init_arch sm3_mod_init_arch
static void sm3_mod_init_arch(void)
{
	if (riscv_isa_extension_available(NULL, ZVKSH) &&
	    riscv_isa_extension_available(NULL, ZVKB) &&
	    riscv_vector_vlen() >= 128)
		static_branch_enable(&have_extensions);
}
