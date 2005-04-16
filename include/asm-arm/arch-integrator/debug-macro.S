/* linux/include/asm-arm/arch-integrator/debug-macro.S
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

#include <asm/hardware/amba_serial.h>

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x16000000	@ physical base address
		movne	\rx, #0xf0000000	@ virtual base
		addne	\rx, \rx, #0x16000000 >> 4
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #UART01x_DR]
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]	@ UARTFLG
		tst	\rd, #1 << 5		@ UARTFLGUTXFF - 1 when full
		bne	1001b
		.endm

		.macro	busyuart,rd,rx
1001:		ldr	\rd, [\rx, #0x18]	@ UARTFLG
		tst	\rd, #1 << 3		@ UARTFLGUBUSY - 1 when busy
		bne	1001b
		.endm
