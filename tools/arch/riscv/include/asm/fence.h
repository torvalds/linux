/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copied from the kernel sources to tools/arch/riscv:
 */

#ifndef _ASM_RISCV_FENCE_H
#define _ASM_RISCV_FENCE_H

#define RISCV_FENCE_ASM(p, s)		"\tfence " #p "," #s "\n"
#define RISCV_FENCE(p, s) \
	({ __asm__ __volatile__ (RISCV_FENCE_ASM(p, s) : : : "memory"); })

#endif	/* _ASM_RISCV_FENCE_H */
