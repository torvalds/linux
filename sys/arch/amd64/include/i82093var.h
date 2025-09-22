/*	$OpenBSD: i82093var.h,v 1.8 2025/09/05 16:57:48 kettenis Exp $	*/
/* $NetBSD: i82093var.h,v 1.1 2003/02/26 21:26:10 fvdl Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_I82093VAR_H_
#define _MACHINE_I82093VAR_H_

#include <machine/apicvar.h>

struct ioapic_pin {
	struct ioapic_pin	*ip_next;	/* next pin on this vector */
	struct mp_intr_map 	*ip_map;
	int			ip_vector;	/* IDT vector */
	int			ip_type;
	struct cpu_info		*ip_cpu;	/* target CPU */
};

struct ioapic_softc {
	struct pic		sc_pic;
	struct ioapic_softc	*sc_next;
	int			sc_apicid;
	int			sc_apic_vers;
	int			sc_apic_vecbase; /* global int base if ACPI */
	int			sc_apic_sz;	/* apic size*/
	int			sc_flags;
	bus_space_tag_t		sc_memt;
	bus_space_handle_t	sc_memh;
	struct ioapic_pin	*sc_pins;	/* sc_apic_sz entries */
};      

/*
 * MP: intr_handle_t is bitfielded.
 * ih&0xff -> legacy irq number.
 * ih&0x10000000 -> if 0, old-style isa irq; if 1, routed via ioapic.
 * (ih&0xff0000)>>16 -> ioapic id.
 * (ih&0x00ff00)>>8 -> ioapic pin.
 */

#define APIC_INT_VIA_APIC	0x10000000
#define APIC_INT_VIA_MSG	0x20000000
#define APIC_INT_VIA_MSGX	0x40000000
#define APIC_INT_APIC_MASK	0x00ff0000
#define APIC_INT_APIC_SHIFT	16
#define APIC_INT_PIN_MASK	0x0000ff00
#define APIC_INT_PIN_SHIFT	8

#define APIC_IRQ_APIC(x) ((x & APIC_INT_APIC_MASK) >> APIC_INT_APIC_SHIFT)
#define APIC_IRQ_PIN(x) ((x & APIC_INT_PIN_MASK) >> APIC_INT_PIN_SHIFT)
#define APIC_IRQ_ISLEGACY(x) (!((x) & APIC_INT_VIA_APIC))
#define APIC_IRQ_LEGACY_IRQ(x) ((x) & 0xff)

void ioapic_print_redir(struct ioapic_softc *, char *, int);
struct ioapic_softc *ioapic_find(int);
struct ioapic_softc *ioapic_find_bybase(int);

void ioapic_enable(void);

extern int ioapic_bsp_id;
extern int nioapics;
extern struct ioapic_softc *ioapics;

#endif /* !_MACHINE_I82093VAR_H_ */
