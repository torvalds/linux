/* linux/include/asm-arm/arch-iop3xx/debug-macro.S
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
		mov	\rx, #0xfe000000	@ physical
#if defined(CONFIG_ARCH_IQ80321) || defined(CONFIG_ARCH_IQ31244)
		orr	\rx, \rx, #0x00800000	@ location of the UART
#elif defined(CONFIG_ARCH_IOP331)
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		moveq	\rx, #0x000fe000	@ Physical Base
		movne	\rx, #0
		orr	\rx, \rx, #0xfe000000
		orr	\rx, \rx, #0x00f00000	@ Virtual Base
		orr	\rx, \rx, #0x00001700   @ location of the UART
#else
#error Unknown IOP3XX implementation
#endif
		.endm

#if !defined(CONFIG_ARCH_IQ80321) || !defined(CONFIG_ARCH_IQ31244) || !defined(CONFIG_ARCH_IQ80331)
#define FLOW_CONTROL
#endif
#define UART_SHIFT	0
#include <asm/hardware/debug-8250.S>
