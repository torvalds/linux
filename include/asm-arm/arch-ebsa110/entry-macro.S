/*
 * include/asm-arm/arch-ebsa110/entry-macro.S
 *
 * Low-level IRQ helper macros for ebsa110 platform.
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */



#define IRQ_STAT		0xff000000	/* read */

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, stat, base, tmp
	mov	\base, #IRQ_STAT
	ldrb	\stat, [\base]			@ get interrupts
	mov	\irqnr, #0
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

