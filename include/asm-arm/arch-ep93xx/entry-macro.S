/*
 * linux/include/asm-arm/arch-ep93xx/entry-macro.S
 * IRQ demultiplexing for EP93xx
 *
 * Copyright (C) 2006 Lennert Buytenhek <buytenh@wantstofly.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */
#include <asm/arch/ep93xx-regs.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =(EP93XX_AHB_VIRT_BASE)
		orr	\base, \base, #0x000b0000
		mov	\irqnr, #0
		ldr	\irqstat, [\base]		@ lower 32 interrupts
		cmp	\irqstat, #0
		bne	1001f

		eor	\base, \base, #0x00070000
		ldr	\irqstat, [\base]		@ upper 32 interrupts
		cmp	\irqstat, #0
		beq	1002f
		mov	\irqnr, #0x20

1001:
		movs	\tmp, \irqstat, lsl #16
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #16

		movs	\tmp, \irqstat, lsl #8
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #8

		movs	\tmp, \irqstat, lsl #4
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #4

		movs	\tmp, \irqstat, lsl #2
		movne	\irqstat, \tmp
		addeq	\irqnr, \irqnr, #2

		movs	\tmp, \irqstat, lsl #1
		addeq	\irqnr, \irqnr, #1
		orrs	\base, \base, #1

1002:
		.endm
