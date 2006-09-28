/*
 * include/asm-arm/arch-iop33x/entry-macro.S
 *
 * Low-level IRQ helper macros for IOP33x-based platforms
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/arch/iop33x.h>

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =IOP3XX_REG_ADDR(0x07C8)
		ldr	\irqstat, [\base]		@ Read IINTVEC
		cmp	\irqstat, #0
		ldreq	\irqstat, [\base]		@ erratum 63 workaround
		adds	\irqnr, \irqstat, #1
		movne	\irqnr, \irqstat, lsr #2
		.endm
