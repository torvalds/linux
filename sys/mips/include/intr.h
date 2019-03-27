/* 	$NetBSD: intr.h,v 1.7 2003/06/16 20:01:00 thorpej Exp $	*/

/*-
 * Copyright (c) 1997 Mark Brinicombe.
 * All rights reserved.
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
 *	This product includes software developed by Mark Brinicombe
 *	for the NetBSD Project.
 * 4. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 *
 */

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#ifdef INTRNG

#ifdef FDT
#include <dev/ofw/openfirm.h>
#endif

#include <sys/intr.h>

#ifndef	MIPS_NIRQ
#define	MIPS_NIRQ		128
#endif

#ifndef	NIRQ
#define	NIRQ			MIPS_NIRQ
#endif

#ifndef FDT
#define	MIPS_PIC_XREF		1	/**< unique xref */
#endif

#define NHARD_IRQS		6
#define NSOFT_IRQS		2
#define NREAL_IRQS		(NHARD_IRQS + NSOFT_IRQS)

#define INTR_IRQ_NSPC_SWI	4

/* MIPS32 PIC APIs */
int mips_pic_map_fixed_intrs(void);
int mips_pic_activate_intr(device_t child, struct resource *r);
int mips_pic_deactivate_intr(device_t child, struct resource *r);

/* MIPS compatibility for legacy mips code */
void cpu_init_interrupts(void);
void cpu_establish_hardintr(const char *, driver_filter_t *, driver_intr_t *,
    void *, int, int, void **);
void cpu_establish_softintr(const char *, driver_filter_t *, void (*)(void*),
    void *, int, int, void **);
/* MIPS interrupt C entry point */
void cpu_intr(struct trapframe *);

#endif /* INTRNG */

#endif	/* _MACHINE_INTR_H */
