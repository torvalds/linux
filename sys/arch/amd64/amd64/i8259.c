/*	$OpenBSD: i8259.c,v 1.12 2024/01/19 18:38:16 kettenis Exp $	*/
/*	$NetBSD: i8259.c,v 1.2 2003/03/02 18:27:15 fvdl Exp $	*/

/*
 * Copyright 2002 (c) Wasabi Systems, Inc.
 * All rights reserved.
 *
 * Written by Frank van der Linden for Wasabi Systems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed for the NetBSD Project by
 *      Wasabi Systems, Inc.
 * 4. The name of Wasabi Systems, Inc. may not be used to endorse
 *    or promote products derived from this software without specific prior
 *    written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY WASABI SYSTEMS, INC. ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL WASABI SYSTEMS, INC
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*-
 * Copyright (c) 1991 The Regents of the University of California.
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
 *	@(#)isa.c	7.2 (Berkeley) 5/13/91
 */

#include <sys/param.h> 

#include <dev/isa/isareg.h>

#include <machine/pio.h>
#include <machine/cpufunc.h>  
#include <machine/cpu.h>
#include <machine/pic.h>
#include <machine/i8259.h>

static void i8259_hwmask(struct pic *, int);
static void i8259_hwunmask(struct pic *, int);
static void i8259_setup(struct pic *, struct cpu_info *, int, int, int);
static void i8259_reinit_irqs(void);

unsigned i8259_imen;

/*
 * Perhaps this should be made into a real device.
 */
struct pic i8259_pic = {
	{0, {NULL}, NULL, 0, "pic0", NULL, 0, 0},
	PIC_I8259,
#ifdef MULTIPROCESSOR
	{},
#endif
	i8259_hwmask,
	i8259_hwunmask,
	i8259_setup,
	i8259_setup,
	NULL,
	i8259_stubs,
	i8259_stubs,
};

void
i8259_default_setup(void)
{
	outb(IO_ICU1, 0x11);		/* reset; program device, four bytes */

	outb(IO_ICU1+1, ICU_OFFSET);	/* starting at this vector index */
	outb(IO_ICU1+1, 1 << IRQ_SLAVE); /* slave on line 2 */
#ifdef AUTO_EOI_1
	outb(IO_ICU1+1, 2 | 1);		/* auto EOI, 8086 mode */
#else
	outb(IO_ICU1+1, 1);		/* 8086 mode */
#endif
	outb(IO_ICU1+1, 0xff);		/* leave interrupts masked */
	outb(IO_ICU1, 0x68);		/* special mask mode (if available) */
	outb(IO_ICU1, 0x0a);		/* Read IRR by default. */
#ifdef REORDER_IRQ
	outb(IO_ICU1, 0xc0 | (3 - 1));	/* pri order 3-7, 0-2 (com2 first) */
#endif

	outb(IO_ICU2, 0x11);		/* reset; program device, four bytes */

	outb(IO_ICU2+1, ICU_OFFSET+8);	/* staring at this vector index */
	outb(IO_ICU2+1, IRQ_SLAVE);
#ifdef AUTO_EOI_2
	outb(IO_ICU2+1, 2 | 1);		/* auto EOI, 8086 mode */
#else
	outb(IO_ICU2+1, 1);		/* 8086 mode */
#endif
	outb(IO_ICU2+1, 0xff);		/* leave interrupts masked */
	outb(IO_ICU2, 0x68);		/* special mask mode (if available) */
	outb(IO_ICU2, 0x0a);		/* Read IRR by default. */
}

static void
i8259_hwmask(struct pic *pic, int pin)
{
	unsigned port;
	u_int8_t byte;

	i8259_imen |= (1 << pin);
#ifdef PIC_MASKDELAY
	delay(10);
#endif
	if (pin > 7) {
		port = IO_ICU2 + 1;
		byte = i8259_imen >> 8;
	} else {
		port = IO_ICU1 + 1;
		byte = i8259_imen & 0xff;
	}
	outb(port, byte);
}

static void
i8259_hwunmask(struct pic *pic, int pin)
{
	unsigned port;
	u_int8_t byte;
	u_long s;

	s = intr_disable();
	i8259_imen &= ~(1 << pin);
#ifdef PIC_MASKDELAY
	delay(10);
#endif
	if (pin > 7) {
		port = IO_ICU2 + 1;
		byte = i8259_imen >> 8;
	} else {
		port = IO_ICU1 + 1;
		byte = i8259_imen & 0xff;
	}
	outb(port, byte);
	intr_restore(s);
}

static void
i8259_reinit_irqs(void)
{
	int irqs, irq;
	struct cpu_info *ci = &cpu_info_primary;

	irqs = 0;
	for (irq = 0; irq < NUM_LEGACY_IRQS; irq++)
		if (ci->ci_isources[irq] != NULL)
			irqs |= 1 << irq;
	if (irqs >= 0x100) /* any IRQs >= 8 in use */
		irqs |= 1 << IRQ_SLAVE;
	i8259_imen = ~irqs;

	outb(IO_ICU1 + 1, i8259_imen);
	outb(IO_ICU2 + 1, i8259_imen >> 8);
}

static void
i8259_setup(struct pic *pic, struct cpu_info *ci, int pin, int idtvec, int type)
{
	if (CPU_IS_PRIMARY(ci))
		i8259_reinit_irqs();
}
