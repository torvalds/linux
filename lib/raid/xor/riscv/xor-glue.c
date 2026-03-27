// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021 SiFive
 */

#include <asm/vector.h>
#include <asm/switch_to.h>
#include <asm/asm-prototypes.h>
#include "xor_impl.h"
#include "xor_arch.h"

DO_XOR_BLOCKS(vector_inner, xor_regs_2_, xor_regs_3_, xor_regs_4_, xor_regs_5_);

static void xor_gen_vector(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes)
{
	kernel_vector_begin();
	xor_gen_vector_inner(dest, srcs, src_cnt, bytes);
	kernel_vector_end();
}

struct xor_block_template xor_block_rvv = {
	.name		= "rvv",
	.xor_gen	= xor_gen_vector,
};
