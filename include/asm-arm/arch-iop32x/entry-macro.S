/*
 * include/asm-arm/arch-iop32x/entry-macro.S
 *
 * Low-level IRQ helper macros for IOP32x-based platforms
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
		ldr	\base, =IOP3XX_REG_ADDR(0x07D8)
		ldr	\irqstat, [\base]		@ Read IINTSRC
		cmp     \irqstat, #0
		beq     1001f
		clz     \irqnr, \irqstat
		mov     \base, #31
		subs    \irqnr,\base,\irqnr
1001:
		.endm
