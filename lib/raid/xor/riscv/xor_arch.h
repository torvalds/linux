/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2021 SiFive
 */
#include <asm/vector.h>

extern struct xor_block_template xor_block_rvv;

static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_32regs);
#ifdef CONFIG_RISCV_ISA_V
	if (has_vector())
		xor_register(&xor_block_rvv);
#endif
}
