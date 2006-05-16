/* linux/include/asm-arm/arch-pnx4008/debug-macro.S
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

		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		mov	\rx, #0x00090000
		addeq	\rx, \rx, #0x40000000
		addne	\rx, \rx, #0xf4000000
		.endm

		.macro	senduart,rd,rx
		strb	\rd, [\rx, #0x0]
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
