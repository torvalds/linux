/*	$OpenBSD: i8259.h,v 1.4 2015/09/02 13:39:23 mikeb Exp $	*/
/*	$NetBSD: i8259.h,v 1.3 2003/05/04 22:01:56 fvdl Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)icu.h	5.6 (Berkeley) 5/9/91
 */

#ifndef	_MACHINE_I8259_H_
#define	_MACHINE_I8259_H_

#include <dev/isa/isareg.h>

#ifndef	_LOCORE

/*
 * Interrupt "level" mechanism variables, masks, and macros
 */
extern	unsigned i8259_imen;		/* interrupt mask enable */

extern void i8259_default_setup(void);

#endif /* !_LOCORE */

/*
 * Interrupt enable bits -- in order of priority
 */
#define	IRQ_SLAVE	2

/*
 * Interrupt Control offset into Interrupt descriptor table (IDT)
 */
#define	ICU_OFFSET	32		/* 0-31 are processor exceptions */
#define	ICU_LEN		16		/* 32-47 are ISA interrupts */


#define ICU_HARDWARE_MASK

/*
 * These macros are fairly self explanatory.  If ICU_SPECIAL_MASK_MODE is
 * defined, we try to take advantage of the ICU's `special mask mode' by only
 * EOIing the interrupts on return.  This avoids the requirement of masking and
 * unmasking.  We can't do this without special mask mode, because the ICU
 * would also hold interrupts that it thinks are of lower priority.
 *
 * Many machines do not support special mask mode, so by default we don't try
 * to use it.
 */

#define	IRQ_BIT(num)	(1 << ((num) % 8))
#define	IRQ_BYTE(num)	((num) >> 3)

#define i8259_late_ack(num)

#ifdef ICU_SPECIAL_MASK_MODE

#define	i8259_asm_ack1(num)
#define	i8259_asm_ack2(num) \
	movb	$(0x60|IRQ_SLAVE),%al	/* specific EOI for IRQ2 */	;\
	outb	%al,$IO_ICU1
#define	i8259_asm_mask(num)
#define	i8259_asm_unmask(num) \
	movb	$(0x60|(num%8)),%al	/* specific EOI */		;\
	outb	%al,$ICUADDR

#else /* ICU_SPECIAL_MASK_MODE */

#ifndef	AUTO_EOI_1
#define	i8259_asm_ack1(num) \
	movb	$(0x60|(num%8)),%al	/* specific EOI */		;\
	outb	%al,$IO_ICU1
#else
#define	i8259_asm_ack1(num)
#endif

#ifndef AUTO_EOI_2
#define	i8259_asm_ack2(num) \
	movb	$(0x60|(num%8)),%al	/* specific EOI */		;\
	outb	%al,$IO_ICU2		/* do the second ICU first */	;\
	movb	$(0x60|IRQ_SLAVE),%al	/* specific EOI for IRQ2 */	;\
	outb	%al,$IO_ICU1
#else
#define	i8259_asm_ack2(num)
#endif

#ifdef PIC_MASKDELAY
#define MASKDELAY	pushl %eax ; inb $0x84,%al ; popl %eax
#else
#define MASKDELAY
#endif

#ifdef ICU_HARDWARE_MASK

#define	i8259_asm_mask(num) \
	movb	CVAROFF(i8259_imen, IRQ_BYTE(num)),%al			;\
	orb	$IRQ_BIT(num),%al					;\
	movb	%al,CVAROFF(i8259_imen, IRQ_BYTE(num))			;\
	MASKDELAY							;\
	outb	%al,$(ICUADDR+1)
#define	i8259_asm_unmask(num) \
	movb	CVAROFF(i8259_imen, IRQ_BYTE(num)),%al			;\
	andb	$~IRQ_BIT(num),%al					;\
	movb	%al,CVAROFF(i8259_imen, IRQ_BYTE(num))			;\
	MASKDELAY							;\
	outb	%al,$(ICUADDR+1)

#else /* ICU_HARDWARE_MASK */

#define	i8259_asm_mask(num)
#define	i8259_asm_unmask(num)

#endif /* ICU_HARDWARE_MASK */
#endif /* ICU_SPECIAL_MASK_MODE */

#endif /* !_MACHINE_I8259_H_ */
