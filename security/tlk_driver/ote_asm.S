/*
 * Copyright (c) 2014, NVIDIA Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/linkage.h>
#include <linux/init.h>

#ifdef CONFIG_ARM64

ENTRY(tlk_irq_handler)
	mov	x0, #0x5
	movk	x0, #0x3200, lsl #16	// TE_SMC_NS_IRQ_DONE
	smc	#0
	ret
ENDPROC(tlk_irq_handler)

/* uint32_t tlk_generic_smc(uint32_t arg0, uint32_t arg1, uint32_t arg2) */
ENTRY(_tlk_generic_smc)
	smc	#0
	ret
ENDPROC(_tlk_generic_smc)

	/* allows MAX_EXT_SMC_ARGS (r0-r11) to be passed in registers */

/* uint32_t tlk_extended_smc(uint32_t *regs) */
ENTRY(_tlk_extended_smc)
	/*
	 * Allows MAX_EXT_SMC_ARGS (r0-r11) to be passed in registers
	 * (for aarch64, these are scratch, so no need to save them)
	 */
	mov	x12, x0
	ldp	x0, x1, [x12], #16
	ldp	x2, x3, [x12], #16
	ldp	x4, x5, [x12], #16
	ldp	x6, x7, [x12], #16
	ldp	x8, x9, [x12], #16
	ldp	x10, x11, [x12], #16
	smc	#0
	ret
ENDPROC(_tlk_extended_smc)

#else

ENTRY(tlk_irq_handler)
	movw	r0, #0x5
	movt	r0, #0x3200	@ TE_SMC_NS_IRQ_DONE
	mov	r1, #0
	mov	r2, #0
	smc	#0
ENDPROC(tlk_irq_handler)

ENTRY(_tlk_generic_smc)
	smc	#0
	mov	pc, lr
ENDPROC(_tlk_generic_smc)

ENTRY(_tlk_extended_smc)
	stmfd	sp!, {r4-r12}	@ save reg state
	mov	r12, r0		@ reg ptr to r12
	ldmia	r12, {r0-r11}	@ load arg regs
	smc	#0
	ldmfd	sp!, {r4-r12}	@ restore saved regs
ENDPROC(_tlk_extended_smc)

#endif
