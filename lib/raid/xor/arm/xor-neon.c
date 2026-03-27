// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include <linux/raid/xor_impl.h>

#ifndef __ARM_NEON__
#error You should compile this file with '-march=armv7-a -mfloat-abi=softfp -mfpu=neon'
#endif

/*
 * Pull in the reference implementations while instructing GCC (through
 * -ftree-vectorize) to attempt to exploit implicit parallelism and emit
 * NEON instructions. Clang does this by default at O2 so no pragma is
 * needed.
 */
#ifdef CONFIG_CC_IS_GCC
#pragma GCC optimize "tree-vectorize"
#endif

#define NO_TEMPLATE
#include "../xor-8regs.c"

struct xor_block_template const xor_block_neon_inner = {
	.name	= "__inner_neon__",
	.do_2	= xor_8regs_2,
	.do_3	= xor_8regs_3,
	.do_4	= xor_8regs_4,
	.do_5	= xor_8regs_5,
};
