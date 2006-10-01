/*
 * include/asm-arm/arch-iop32x/entry-macro.S
 *
 * Low-level IRQ helper macros for IOP32x-based platforms
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/arch/iop32x.h>

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =IOP3XX_REG_ADDR(0x07D8)
		ldr	\irqstat, [\base]		@ Read IINTSRC
		cmp	\irqstat, #0
		clzne	\irqnr, \irqstat
		rsbne	\irqnr, \irqnr, #31
		.endm
