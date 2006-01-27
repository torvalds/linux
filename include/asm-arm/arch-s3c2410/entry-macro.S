/*
 * include/asm-arm/arch-s3c2410/entry-macro.S
 *
 * Low-level IRQ helper macros for S3C2410-based platforms
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.

 * Modifications:
 *     10-Mar-2005 LCVR  Changed S3C2410_VA to S3C24XX_VA
 */
#include <asm/hardware.h>
#include <asm/arch/irqs.h>


	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp

		mov	\tmp, #S3C24XX_VA_IRQ
		ldr	\irqnr, [ \tmp, #0x14 ]		@ get irq no
30000:
		teq	\irqnr, #4
		teqne	\irqnr, #5
		beq	1002f				@ external irq reg

		@ debug check to see if interrupt reported is the same
		@ as the offset....

		teq	\irqnr, #0
		beq	20002f
		ldr	\irqstat, [ \tmp, #0x10 ]	@ INTPND
		mov	\irqstat, \irqstat, lsr \irqnr
		tst	\irqstat, #1
		bne	20002f

		/* debug/warning if we get an invalud response from the
		 * INTOFFSET register */
#if 1
		stmfd	r13!, { r0 - r4 , r8-r12, r14 }
		ldr	r1,	[ \tmp, #0x14 ]		@ INTOFFSET
		ldr	r2,	[ \tmp, #0x10 ]		@ INTPND
		ldr	r3,	[ \tmp, #0x00 ]		@ SRCPND
		adr	r0, 20003f
		bl	printk
		b	20004f

20003:
		.ascii	"<7>irq: err - bad offset %d, intpnd=%08x, srcpnd=%08x\n"
		.byte	0
		.align	4
20004:
		mov	r1, #1
		mov	\tmp, #S3C24XX_VA_IRQ
		ldmfd	r13!, { r0 - r4 , r8-r12, r14 }
#endif

		@ try working out interrupt number for ourselves
		mov	\irqnr, #0
		ldr	\irqstat, [ \tmp, #0x10 ]	@ INTPND
10021:
		movs	\irqstat, \irqstat, lsr#1
		bcs	30000b		@ try and re-start the proccess
		add	\irqnr, \irqnr, #1
		cmp	\irqnr, #32
		ble	10021b

		@ found no interrupt, set Z flag and leave
		movs	\irqnr, #0
		b	1001f

20005:
20002:		@ exit
		@ we base the s3c2410x interrupts at 16 and above to allow
		@ isa peripherals to have their standard interrupts, also
		@ ensure that Z flag is un-set on exit

		@ note, we cannot be sure if we get IRQ_EINT0 (0) that
		@ there is simply no interrupt pending, so in all other
		@ cases we jump to say we have found something, otherwise
		@ we check to see if the interrupt really is assrted
		adds	\irqnr, \irqnr, #IRQ_EINT0
		teq	\irqnr, #IRQ_EINT0
		bne	1001f				@ exit
		ldr	\irqstat, [ \tmp, #0x10 ]	@ INTPND
		teq	\irqstat, #0
		moveq	\irqnr, #0
		b	1001f

		@ we get here from no main or external interrupts pending
1002:
		add	\tmp, \tmp, #S3C24XX_VA_GPIO - S3C24XX_VA_IRQ
		ldr	\irqstat, [ \tmp, # 0xa8 ]	@ EXTINTPEND
		ldr	\irqnr, [ \tmp, # 0xa4 ]	@ EXTINTMASK

		bic	\irqstat, \irqstat, \irqnr	@ clear masked irqs

		mov	\irqnr, #IRQ_EINT4		@ start extint nos
		mov	\irqstat, \irqstat, lsr#4	@ ignore bottom 4 bits
10021:
		movs	\irqstat, \irqstat, lsr#1
		bcs	1004f
		add	\irqnr, \irqnr, #1
		cmp	\irqnr, #IRQ_EINT23
		ble	10021b

		@ found no interrupt, set Z flag and leave
		movs	\irqnr, #0

1004:		@ ensure Z flag clear in case our MOVS shifted out the last bit
		teq	\irqnr, #0
1001:
		@ exit irq routine
		.endm


		/* currently don't need an disable_fiq macro */

		.macro	disable_fiq
		.endm


