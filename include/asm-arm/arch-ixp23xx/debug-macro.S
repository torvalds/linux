/*
 * include/asm-arm/arch-ixp23xx/debug-macro.S
 *
 * Debugging macro include header
 *
 * Copyright (C) 1994-1999 Russell King
 * Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/arch/ixp23xx.h>

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1				@ mmu enabled?
		ldreq	\rx, =IXP23XX_PERIPHERAL_PHYS 	@ physical
		ldrne	\rx, =IXP23XX_PERIPHERAL_VIRT	@ virtual
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
