/* linux/include/asm-arm/arch-ixp2000/debug-macro.S
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

		.macro  addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0xc0000000	@ Physical base
		movne	\rx, #0xfe000000	@ virtual base
		orrne	\rx, \rx, #0x00f00000
		orr	\rx, \rx, #0x00030000
#ifdef	__ARMEB__
		orr	\rx, \rx, #0x00000003
#endif
		.endm

#define UART_SHIFT	2
#include <asm/hardware/debug-8250.S>
