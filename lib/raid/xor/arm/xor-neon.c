// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 */

#include "xor_impl.h"
#include "xor_arch.h"

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

__DO_XOR_BLOCKS(neon_inner, xor_8regs_2, xor_8regs_3, xor_8regs_4, xor_8regs_5);
