/*
 * include/asm-arm/arch-shark/entry-macro.S
 *
 * Low-level IRQ helper macros for Shark platform
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	r4, #0xe0000000

		mov	\irqstat, #0x0C
		strb	\irqstat, [r4, #0x20]		@outb(0x0C, 0x20) /* Poll command */
		ldrb	\irqnr, [r4, #0x20]		@irq = inb(0x20) & 7
		and	\irqstat, \irqnr, #0x80
		teq	\irqstat, #0
		beq	43f
		and	\irqnr, \irqnr, #7
		teq	\irqnr, #2
		bne	44f
43:		mov	\irqstat, #0x0C
		strb	\irqstat, [r4, #0xa0]		@outb(0x0C, 0xA0) /* Poll command */
		ldrb	\irqnr, [r4, #0xa0]		@irq = (inb(0xA0) & 7) + 8
		and	\irqstat, \irqnr, #0x80
		teq	\irqstat, #0
		beq	44f
		and	\irqnr, \irqnr, #7
		add	\irqnr, \irqnr, #8
44:		teq	\irqstat, #0
		.endm

