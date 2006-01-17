/*
 * include/asm-arm/arch-imx/entry-macro.S
 *
 * Low-level IRQ helper macros for iMX-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/hardware.h>

		.macro	disable_fiq
		.endm
#define AITC_NIVECSR   0x40
		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\irqstat, =IO_ADDRESS(IMX_AITC_BASE)
		@ Load offset & priority of the highest priority
		@ interrupt pending.
		ldr	\irqnr, [\irqstat, #AITC_NIVECSR]
		@ Shift off the priority leaving the offset or
		@ "interrupt number"
		mov	\irqnr, \irqnr, lsr #16
 		ldr	\irqstat, =1	@ dummy compare
		ldr	\base, =0xFFFF		// invalid interrupt
		cmp	\irqnr, \base
		bne	1001f
		ldr	\irqstat, =0
1001:
		tst	\irqstat, #1	@ to make the condition code = TRUE
		.endm

