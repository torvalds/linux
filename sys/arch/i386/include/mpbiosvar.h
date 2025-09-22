/*	$OpenBSD: mpbiosvar.h,v 1.13 2024/10/22 21:50:02 jsg Exp $	*/
/* $NetBSD: mpbiosvar.h,v 1.1.2.3 2000/02/29 13:17:20 sommerfeld Exp $ */

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


#ifndef _MACHINE_MPBIOSVAR_H_
#define _MACHINE_MPBIOSVAR_H_

#define MP_TRAMPOLINE  (16 * PAGE_SIZE)
#define MP_TRAMP_DATA  (17 * PAGE_SIZE)

#if !defined(_LOCORE)

#include <machine/mpbiosreg.h>

struct mp_bus {
	char *mb_name;		/* XXX bus name */
	int mb_idx;		/* XXX bus index */
	void (*mb_intr_print) (int);
	void (*mb_intr_cfg)(const struct mpbios_int *, u_int32_t *);
	struct mp_intr_map *mb_intrs;
	u_int32_t mb_data;	/* random bus-specific datum. */
};

struct mp_intr_map {
	struct mp_intr_map *next;
	struct mp_bus *bus;
	int bus_pin;
	struct ioapic_softc *ioapic;
	int ioapic_pin;
	int ioapic_ih;		/* int handle, for apic_intr_est */
	int type;		/* from mp spec intr record */
 	int flags;		/* from mp spec intr record */
	u_int32_t redir;
	int cpu_id;
};

#if defined(_KERNEL)
extern int mp_verbose;
extern struct mp_bus *mp_busses;
extern int mp_nbusses;
extern struct mp_intr_map *mp_intrs;
extern int mp_nintrs;
extern struct mp_bus *mp_isa_bus;
extern struct mp_bus *mp_eisa_bus;

void mpbios_scan(struct device *);
int mpbios_probe(struct device *);
int mpbios_invent(int, int, int);

void mpbios_intr_fixup(void);
#endif

#endif

#endif /* !_MACHINE_MPBIOSVAR_H_ */
