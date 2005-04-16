/* linux/include/asm-arm/arch-epxa10db/debug-macro.S
 *
 * Debugging macro include header
 *
 *  Copyright (C) 1994-1999 Russell King
 *  Moved from linux/arch/arm/kernel/debug.S by Ben Dooks
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <asm/arch/excalibur.h>
#define UART00_TYPE
#include <asm/arch/uart00.h>

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldr	\rx, =EXC_UART00_BASE	@ physical base address
		orrne	\rx, \rx, #0xff000000	@ virtual base
		orrne	\rx, \rx, #0x00f00000
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #UART_TD(0)]
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #UART_TSR(0)]
		and 	\rd, \rd,  #UART_TSR_TX_LEVEL_MSK
		cmp	\rd, #15
		beq	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #UART_TSR(0)]
		ands 	\rd, \rd,  #UART_TSR_TX_LEVEL_MSK
		bne	1001b
		.endm
