/*
 * include/asm-arm/arch-CLPS711x/entry-macro.S
 *
 * Low-level IRQ helper macros for CLPS711X-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/hardware/clps7111.h>

		.macro	disable_fiq
		.endm

#if (INTSR2 - INTSR1) != (INTMR2 - INTMR1)
#error INTSR stride != INTMR stride
#endif

		.macro	get_irqnr_and_base, irqnr, stat, base, mask
		mov	\base, #CLPS7111_BASE
		ldr	\stat, [\base, #INTSR1]
		ldr	\mask, [\base, #INTMR1]
		mov	\irqnr, #4
		mov	\mask, \mask, lsl #16
		and	\stat, \stat, \mask, lsr #16
		movs	\stat, \stat, lsr #4
		bne	1001f

		add	\base, \base, #INTSR2 - INTSR1
		ldr	\stat, [\base, #INTSR1]
		ldr	\mask, [\base, #INTMR1]
		mov	\irqnr, #16
		mov	\mask, \mask, lsl #16
		and	\stat, \stat, \mask, lsr #16

1001:		tst	\stat, #255
		addeq	\irqnr, \irqnr, #8
		moveq	\stat, \stat, lsr #8
		tst	\stat, #15
		addeq	\irqnr, \irqnr, #4
		moveq	\stat, \stat, lsr #4
		tst	\stat, #3
		addeq	\irqnr, \irqnr, #2
		moveq	\stat, \stat, lsr #2
		tst	\stat, #1
		addeq	\irqnr, \irqnr, #1
		moveq	\stat, \stat, lsr #1
		tst	\stat, #1			@ bit 0 should be set
		.endm


