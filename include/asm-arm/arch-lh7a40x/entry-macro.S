/*
 * include/asm-arm/arch-lh7a40x/entry-macro.S
 *
 * Low-level IRQ helper macros for LH7A40x platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */
#include <asm/hardware.h>
#include <asm/arch/irqs.h>

/* In order to allow there to be support for both of the processor
   classes at the same time, we make a hack here that isn't very
   pretty.  At startup, the link pointed to with the
   branch_irq_lh7a400 symbol is replaced with a NOP when the CPU is
   detected as a lh7a404.

   *** FIXME: we should clean this up so that there is only one
	      implementation for each CPU's design.

*/

#if defined (CONFIG_ARCH_LH7A400) && defined (CONFIG_ARCH_LH7A404)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

branch_irq_lh7a400: b 1000f

@ Implementation of the LH7A404 get_irqnr_and_base.

		mov	\irqnr, #0			@ VIC1 irq base
		mov	\base, #io_p2v(0x80000000)	@ APB registers
		add	\base, \base, #0x8000
		ldr	\tmp, [\base, #0x0030]		@ VIC1_VECTADDR
		tst	\tmp, #VA_VECTORED		@ Direct vectored
		bne	1002f
		tst	\tmp, #VA_VIC1DEFAULT		@ Default vectored VIC1
		ldrne	\irqstat, [\base, #0]		@ VIC1_IRQSTATUS
		bne	1001f
		add	\base, \base, #(0xa000 - 0x8000)
		ldr	\tmp, [\base, #0x0030]		@ VIC2_VECTADDR
		tst	\tmp, #VA_VECTORED		@ Direct vectored
		bne	1002f
		ldr	\irqstat, [\base, #0]		@ VIC2_IRQSTATUS
		mov	\irqnr, #32			@ VIC2 irq base

1001:		movs	\irqstat, \irqstat, lsr #1	@ Shift into carry
		bcs	1008f				@ Bit set; irq found
		add	\irqnr, \irqnr, #1
		bne	1001b				@ Until no bits
		b	1009f				@ Nothing?  Hmm.
1002:		and	\irqnr, \tmp, #0x3f		@ Mask for valid bits
1008:		movs	\irqstat, #1			@ Force !Z
		str	\tmp, [\base, #0x0030]		@ Clear vector
		b	1009f

@ Implementation of the LH7A400 get_irqnr_and_base.

1000:		mov	\irqnr, #0
		mov	\base, #io_p2v(0x80000000)	@ APB registers
		ldr	\irqstat, [\base, #0x500]	@ PIC INTSR

1001:		movs	\irqstat, \irqstat, lsr #1	@ Shift into carry
		bcs	1008f				@ Bit set; irq found
		add	\irqnr, \irqnr, #1
		bne	1001b				@ Until no bits
		b	1009f				@ Nothing?  Hmm.
1008:		movs	\irqstat, #1			@ Force !Z

1009:
               .endm



#elif defined (CONFIG_ARCH_LH7A400)
		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	\irqnr, #0
		mov	\base, #io_p2v(0x80000000)	@ APB registers
		ldr	\irqstat, [\base, #0x500]	@ PIC INTSR

1001:		movs	\irqstat, \irqstat, lsr #1	@ Shift into carry
		bcs	1008f				@ Bit set; irq found
		add	\irqnr, \irqnr, #1
		bne	1001b				@ Until no bits
		b	1009f				@ Nothing?  Hmm.
1008:		movs	\irqstat, #1			@ Force !Z
1009:
               .endm

#elif defined(CONFIG_ARCH_LH7A404)

		.macro	disable_fiq
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
		mov	\irqnr, #0			@ VIC1 irq base
		mov	\base, #io_p2v(0x80000000)	@ APB registers
		add	\base, \base, #0x8000
		ldr	\tmp, [\base, #0x0030]		@ VIC1_VECTADDR
		tst	\tmp, #VA_VECTORED		@ Direct vectored
		bne	1002f
		tst	\tmp, #VA_VIC1DEFAULT		@ Default vectored VIC1
		ldrne	\irqstat, [\base, #0]		@ VIC1_IRQSTATUS
		bne	1001f
		add	\base, \base, #(0xa000 - 0x8000)
		ldr	\tmp, [\base, #0x0030]		@ VIC2_VECTADDR
		tst	\tmp, #VA_VECTORED		@ Direct vectored
		bne	1002f
		ldr	\irqstat, [\base, #0]		@ VIC2_IRQSTATUS
		mov	\irqnr, #32			@ VIC2 irq base

1001:		movs	\irqstat, \irqstat, lsr #1	@ Shift into carry
		bcs	1008f				@ Bit set; irq found
		add	\irqnr, \irqnr, #1
		bne	1001b				@ Until no bits
		b	1009f				@ Nothing?  Hmm.
1002:		and	\irqnr, \tmp, #0x3f		@ Mask for valid bits
1008:		movs	\irqstat, #1			@ Force !Z
		str	\tmp, [\base, #0x0030]		@ Clear vector
1009:
               .endm
#endif


