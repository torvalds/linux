/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 * Copyright (C) IBM Corporation, 2012
 *
 * Author: Anton Blanchard <anton@au.ibm.com>
 */
#include <asm/cpu_has_feature.h>

extern struct xor_block_template xor_block_altivec;

static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_8regs_p);
	xor_register(&xor_block_32regs);
	xor_register(&xor_block_32regs_p);
#ifdef CONFIG_ALTIVEC
	if (cpu_has_feature(CPU_FTR_ALTIVEC))
		xor_register(&xor_block_altivec);
#endif
}
