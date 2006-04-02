/*
 * include/asm-arm/arch-ixp23xx/entry-macro.S
 */

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqnr, =(IXP23XX_INTC_VIRT + IXP23XX_INTR_IRQ_ENC_ST_OFFSET)
		ldr	\irqnr, [\irqnr]	@ get interrupt number
		cmp	\irqnr, #0x0		@ suprious interrupt ?
		movne	\irqnr, \irqnr, lsr #2	@ skip unwanted low order bits
		subne	\irqnr, \irqnr, #1	@ convert to 0 based

#if 0
		cmp	\irqnr, #IRQ_IXP23XX_PCI_INT_RPH
		bne	1001f
		mov	\irqnr, #IRQ_IXP23XX_INTA

		ldr	\irqnr, =0xf5000030

		mov	\tmp, #(1<<26)
		tst	\irqnr, \tmp
		movne	\irqnr, #IRQ_IXP23XX_INTB

		mov	\tmp, #(1<<27)
		tst	\irqnr, \tmp
		movne	\irqnr, #IRQ_IXP23XX_INTA
1001:
#endif
		.endm
