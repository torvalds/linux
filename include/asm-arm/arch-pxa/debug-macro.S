/* linux/include/asm-arm/arch-pxa/debug-macro.S
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
		moveq	\rx, #0x40000000		@ physical
		movne	\rx, #io_p2v(0x40000000)	@ virtual
		orr	\rx, \rx, #0x00100000
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0]
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x14]
		tst	\rd, #(1 << 6)
		beq	1002b
		.endm

		.macro	waituart,rd,rx
1001:		ldr	\rd, [\rx, #0x14]
		tst	\rd, #(1 << 5)
		beq	1001b
		.endm
