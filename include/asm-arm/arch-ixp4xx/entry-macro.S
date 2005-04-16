/*
 * include/asm-arm/arch-ixp4xx/entry-macro.S
 *
 * Low-level IRQ helper macros for IXP4xx-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, =(IXP4XX_INTC_BASE_VIRT+IXP4XX_ICIP_OFFSET)
		ldr	\irqstat, [\irqstat]		@ get interrupts
		cmp	\irqstat, #0
		beq	1001f
		clz     \irqnr, \irqstat
		mov     \base, #31
		subs    \irqnr, \base, \irqnr

1001:
		/*
		 * IXP465 has an upper IRQ status register
		 */
#if defined(CONFIG_CPU_IXP46X)
		bne 	1002f
		ldr	\irqstat, =(IXP4XX_INTC_BASE_VIRT+IXP4XX_ICIP2_OFFSET)
		ldr	\irqstat, [\irqstat]		@ get upper interrupts
		mov	\irqnr, #63
		clz	\irqstat, \irqstat
 		cmp	\irqstat, #32
		subne	\irqnr, \irqnr, \irqstat
1002:
#endif
		.endm


