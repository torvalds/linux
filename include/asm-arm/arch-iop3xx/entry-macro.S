/*
 * include/asm-arm/arch-iop3xx/entry-macro.S
 *
 * Low-level IRQ helper macros for IOP3xx-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#if defined(CONFIG_ARCH_IOP321)
		.macro  disable_fiq
		.endm

		/*
		 * Note: only deal with normal interrupts, not FIQ
		 */
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov     \irqnr, #0
		mrc     p6, 0, \irqstat, c8, c0, 0      @ Read IINTSRC
		cmp     \irqstat, #0
		beq     1001f
		clz     \irqnr, \irqstat
		mov     \base, #31
		subs    \irqnr,\base,\irqnr
		add     \irqnr,\irqnr,#IRQ_IOP321_DMA0_EOT
1001:
		.endm

#elif defined(CONFIG_ARCH_IOP331)
		.macro  disable_fiq
		.endm

		/*
		 * Note: only deal with normal interrupts, not FIQ
		 */
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov     \irqnr, #0
		mrc     p6, 0, \irqstat, c4, c0, 0      @ Read IINTSRC0
		cmp     \irqstat, #0
		bne     1002f
		mrc     p6, 0, \irqstat, c5, c0, 0      @ Read IINTSRC1
		cmp	\irqstat, #0
		beq	1001f
		clz	\irqnr, \irqstat
		rsbs    \irqnr,\irqnr,#31   @ recommend by RMK
		add	\irqnr,\irqnr,#IRQ_IOP331_XINT8
		b	1001f
1002:	clz     \irqnr, \irqstat
		rsbs    \irqnr,\irqnr,#31   @ recommend by RMK
		add     \irqnr,\irqnr,#IRQ_IOP331_DMA0_EOT
1001:
		.endm

#endif

