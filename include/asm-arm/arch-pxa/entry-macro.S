/*
 * include/asm-arm/arch-pxa/entry-macro.S
 *
 * Low-level IRQ helper macros for PXA-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/hardware.h>
#include <asm/arch/irqs.h>

		.macro	disable_fiq
		.endm

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro  arch_ret_to_user, tmp1, tmp2
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mrc	p15, 0, \tmp, c0, c0, 0		@ CPUID
		mov	\tmp, \tmp, lsr #13
		and	\tmp, \tmp, #0x7		@ Core G
		cmp	\tmp, #1
		bhi	1004f

		mov	\base, #io_p2v(0x40000000)	@ IIR Ctl = 0x40d00000
		add	\base, \base, #0x00d00000
		ldr	\irqstat, [\base, #0]		@ ICIP
		ldr	\irqnr, [\base, #4]		@ ICMR
		b	1002f

1004:
		mrc	p6, 0, \irqstat, c6, c0, 0	@ ICIP2
		mrc	p6, 0, \irqnr, c7, c0, 0	@ ICMR2
		ands	\irqnr, \irqstat, \irqnr
		beq	1003f
		rsb	\irqstat, \irqnr, #0
		and	\irqstat, \irqstat, \irqnr
		clz	\irqnr, \irqstat
		rsb	\irqnr, \irqnr, #31
		add	\irqnr, \irqnr, #32
		b	1001f
1003:
		mrc	p6, 0, \irqstat, c0, c0, 0	@ ICIP
		mrc	p6, 0, \irqnr, c1, c0, 0	@ ICMR
1002:
		ands	\irqnr, \irqstat, \irqnr
		beq	1001f
		rsb	\irqstat, \irqnr, #0
		and	\irqstat, \irqstat, \irqnr
		clz	\irqnr, \irqstat
		rsb	\irqnr, \irqnr, #31
1001:
		.endm
