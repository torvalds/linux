/*
 * include/asm-arm/arch-at91/entry-macro.S
 *
 *  Copyright (C) 2003-2005 SAN People
 *
 * Low-level IRQ helper macros for AT91RM9200 platforms
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <asm/hardware.h>
#include <asm/arch/at91_aic.h>

	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =(AT91_VA_BASE_SYS + AT91_AIC)		@ base virtual address of AIC peripheral
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
	ldr	\irqnr, [\base, #(AT91_AIC_IVR - AT91_AIC)]	@ read IRQ vector register: de-asserts nIRQ to processor (and clears interrupt)
	ldr	\irqstat, [\base, #(AT91_AIC_ISR - AT91_AIC)]	@ read interrupt source number
	teq	\irqstat, #0					@ ISR is 0 when no current interrupt, or spurious interrupt
	streq	\tmp, [\base, #(AT91_AIC_EOICR - AT91_AIC)]	@ not going to be handled further, then ACK it now.
	.endm

