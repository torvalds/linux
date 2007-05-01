/*
 * include/asm-arm/arch-versatile/entry-macro.S
 *
 * Low-level IRQ helper macros for Versatile platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/hardware.h>
#include <asm/hardware/vic.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		ldr	\base, =IO_ADDRESS(VERSATILE_VIC_BASE)
		ldr	\irqstat, [\base, #VIC_IRQ_STATUS]	@ get masked status
		mov	\irqnr, #0
		teq	\irqstat, #0
		beq	1003f

1001:		tst	\irqstat, #15
		bne	1002f
		add	\irqnr, \irqnr, #4
		movs	\irqstat, \irqstat, lsr #4
		bne	1001b
1002:		tst	\irqstat, #1
		bne	1003f
		add	\irqnr, \irqnr, #1
		movs	\irqstat, \irqstat, lsr #1
		bne	1002b
1003:		/* EQ will be set if no irqs pending */

@		clz	\irqnr, \irqstat
@1003:		/* EQ will be set if we reach MAXIRQNUM */
		.endm

