/* linux/include/asm-arm/arch-imx/debug-macro.S
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

#include <asm/arch/hardware.h>

#ifdef CONFIG_MACH_MX31ADS
#include <asm/arch/board-mx31ads.h>
#endif
#ifdef CONFIG_MACH_PCM037
#include <asm/arch/board-pcm037.h>
#endif
#ifdef CONFIG_MACH_MX31LITE
#include <asm/arch/board-mx31lite.h>
#endif
#ifdef CONFIG_MACH_MX27ADS
#include <asm/arch/board-mx27ads.h>
#endif
#ifdef CONFIG_MACH_PCM038
#include <asm/arch/board-pcm038.h>
#endif
		.macro	addruart,rx
		mrc	p15, 0, \rx, c1, c0
		tst	\rx, #1			@ MMU enabled?
		ldreq	\rx, =MXC_LL_UART_PADDR	@ physical
		ldrne	\rx, =MXC_LL_UART_VADDR	@ virtual
		.endm

		.macro	senduart,rd,rx
		str	\rd, [\rx, #0x40]	@ TXDATA
		.endm

		.macro	waituart,rd,rx
		.endm

		.macro	busyuart,rd,rx
1002:		ldr	\rd, [\rx, #0x98]	@ SR2
		tst	\rd, #1 << 3		@ TXDC
		beq	1002b			@ wait until transmit done
		.endm
