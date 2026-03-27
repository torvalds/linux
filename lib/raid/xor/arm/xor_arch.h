/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright (C) 2001 Russell King
 */
#include <asm/neon.h>

extern struct xor_block_template xor_block_arm4regs;
extern struct xor_block_template xor_block_neon;

void xor_gen_neon_inner(void *dest, void **srcs, unsigned int src_cnt,
		unsigned int bytes);

static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_arm4regs);
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_32regs);
#ifdef CONFIG_KERNEL_MODE_NEON
	if (cpu_has_neon())
		xor_register(&xor_block_neon);
#endif
}
