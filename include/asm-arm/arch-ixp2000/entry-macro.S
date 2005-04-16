/*
 * include/asm-arm/arch-ixp2000/entry-macro.S
 *
 * Low-level IRQ helper macros for IXP2000-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

		.macro  disable_fiq
		.endm

		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp

		mov	\irqnr, #0x0              @clear out irqnr as default
                mov	\base, #0xfe000000
		orr	\base, \base, #0x00e00000
		orr	\base, \base, #0x08
		ldr	\irqstat, [\base]         @ get interrupts

		cmp	\irqstat, #0
		beq	1001f

		clz     \irqnr, \irqstat
		mov     \base, #31
		subs    \irqnr, \base, \irqnr

		/*
		 * We handle PCIA and PCIB here so we don't have an
		 * extra layer of code just to check these two bits.
		 */
		cmp	\irqnr, #IRQ_IXP2000_PCI
		bne	1001f

		mov	\base, #0xfe000000
		orr	\base, \base, #0x00c00000
		orr	\base, \base, #0x00000100
		orr	\base, \base, #0x00000058
		ldr	\irqstat, [\base]

		mov	\tmp, #(1<<26)
		tst	\irqstat, \tmp
		movne	\irqnr, #IRQ_IXP2000_PCIA
		bne	1001f

		mov	\tmp, #(1<<27)
		tst	\irqstat, \tmp
		movne	\irqnr, #IRQ_IXP2000_PCIB

1001:
		.endm

