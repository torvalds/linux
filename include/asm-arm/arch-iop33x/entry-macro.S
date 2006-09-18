/*
 * include/asm-arm/arch-iop33x/entry-macro.S
 *
 * Low-level IRQ helper macros for IOP33x-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/arch/irqs.h>

		.macro  disable_fiq
		.endm

		/*
		 * Note: only deal with normal interrupts, not FIQ
		 */
		.macro  get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov     \irqnr, #0
		ldr	\base, =IOP3XX_REG_ADDR(0x7A0)
		ldr	\irqstat, [\base]		@ Read IINTSRC0
		cmp     \irqstat, #0
		bne     1002f
		ldr	\irqstat, [\base, #4]		@ Read IINTSRC1
		cmp	\irqstat, #0
		beq	1001f
		clz	\irqnr, \irqstat
		rsbs    \irqnr,\irqnr,#31   @ recommend by RMK
		add	\irqnr,\irqnr,#IRQ_IOP331_XINT8
		b	1001f
1002:		clz     \irqnr, \irqstat
		rsbs    \irqnr,\irqnr,#31   @ recommend by RMK
		add     \irqnr,\irqnr,#IRQ_IOP331_DMA0_EOT
1001:
		.endm
