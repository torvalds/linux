/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023 WANG Xuerui <git@xen0n.name>
 */
#include <asm/cpu-features.h>

/*
 * For grins, also test the generic routines.
 *
 * More importantly: it cannot be ruled out at this point of time, that some
 * future (maybe reduced) models could run the vector algorithms slower than
 * the scalar ones, maybe for errata or micro-op reasons. It may be
 * appropriate to revisit this after one or two more uarch generations.
 */

extern struct xor_block_template xor_block_lsx;
extern struct xor_block_template xor_block_lasx;

static __always_inline void __init arch_xor_init(void)
{
	xor_register(&xor_block_8regs);
	xor_register(&xor_block_8regs_p);
	xor_register(&xor_block_32regs);
	xor_register(&xor_block_32regs_p);
#ifdef CONFIG_CPU_HAS_LSX
	if (cpu_has_lsx)
		xor_register(&xor_block_lsx);
#endif
#ifdef CONFIG_CPU_HAS_LASX
	if (cpu_has_lasx)
		xor_register(&xor_block_lasx);
#endif
}
