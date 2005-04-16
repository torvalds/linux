/*
 * include/asm-arm/arch-epxa10db/entry-macro.S
 *
 * Low-level IRQ helper macros for epxa10db platform
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/arch/platform.h>
#undef IRQ_MODE /* same name defined in asm/proc/ptrace.h */
#include <asm/arch/int_ctrl00.h>

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

		ldr	\irqstat, =INT_ID(IO_ADDRESS(EXC_INT_CTRL00_BASE))
		ldr	\irqnr,[\irqstat]
		cmp	\irqnr,#0
		subne	\irqnr,\irqnr,#1

		.endm

