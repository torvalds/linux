/*
 *  linux/include/asm-arm/arch-aaec2000/entry-macro.S
 *
 *  Low-level IRQ helper for aaec-2000 based platforms
 *
 *  Copyright (c) 2005 Nicolas Bellido Y Ortega
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <asm/arch/irqs.h>

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	r4, #0xf8000000
		add	r4, r4, #0x00000500
		mov	\base, r4
		ldr	\irqstat, [\base, #0]
		cmp	\irqstat, #0
		bne	1001f
		ldr	\irqnr, =NR_IRQS+1
		b       1003f
1001:		mov	\irqnr, #0
1002:		ands	\tmp, \irqstat, #1
		mov	\irqstat, \irqstat, LSR #1
		add	\irqnr, \irqnr, #1
		beq	1002b
		sub	\irqnr, \irqnr, #1
1003:
		.endm
