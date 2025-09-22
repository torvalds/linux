/* $OpenBSD: pci_1000a.c,v 1.16 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pci_1000a.c,v 1.14 2001/07/27 00:25:20 thorpej Exp $ */

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is based on pci_kn20aa.c, written by Chris G. Demetriou at
 * Carnegie-Mellon University. Platform support for Noritake, Pintake, and
 * Corelle by Ross Harvey with copyright assignment by permission of Avalon
 * Computer Systems, Inc.
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

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Chris G. Demetriou
 * 
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS" 
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND 
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <alpha/pci/pci_1000a.h>

#include "sio.h"
#if NSIO > 0 || NPCEB > 0
#include <alpha/pci/siovar.h>
#endif

#define	PCI_NIRQ	32
#define	PCI_STRAY_MAX	5

#define IMR2IRQ(bn) ((bn) - 1)
#define IRQ2IMR(irq) ((irq) + 1)

static bus_space_tag_t mystery_icu_iot;
static bus_space_handle_t mystery_icu_ioh[2];

int	dec_1000a_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *dec_1000a_intr_string(void *, pci_intr_handle_t);
int	dec_1000a_intr_line(void *, pci_intr_handle_t);
void	*dec_1000a_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void	dec_1000a_intr_disestablish(void *, void *);

struct alpha_shared_intr *dec_1000a_pci_intr;

void dec_1000a_iointr(void *arg, unsigned long vec);
void dec_1000a_enable_intr(int irq);
void dec_1000a_disable_intr(int irq);
void pci_1000a_imi(void);

void
pci_1000a_pickintr(void *core, bus_space_tag_t iot, bus_space_tag_t memt,
    pci_chipset_tag_t pc)
{
	int i;

	mystery_icu_iot = iot;

	if (bus_space_map(iot, 0x54a, 2, 0, mystery_icu_ioh + 0)
	||  bus_space_map(iot, 0x54c, 2, 0, mystery_icu_ioh + 1))
		panic("pci_1000a_pickintr");
	pc->pc_intr_v = core;
	pc->pc_intr_map = dec_1000a_intr_map;
	pc->pc_intr_string = dec_1000a_intr_string;
	pc->pc_intr_line = dec_1000a_intr_line;
	pc->pc_intr_establish = dec_1000a_intr_establish;
	pc->pc_intr_disestablish = dec_1000a_intr_disestablish;

	pc->pc_pciide_compat_intr_establish = NULL;
	pc->pc_pciide_compat_intr_disestablish = NULL;

	dec_1000a_pci_intr = alpha_shared_intr_alloc(PCI_NIRQ);
	for (i = 0; i < PCI_NIRQ; i++) {
		alpha_shared_intr_set_maxstrays(dec_1000a_pci_intr, i,
		    PCI_STRAY_MAX);
	}

	pci_1000a_imi();
#if NSIO > 0 || NPCEB > 0
	sio_intr_setup(pc, iot);
#endif
}

int
dec_1000a_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pcitag_t bustag = pa->pa_intrtag;
	int buspin, line = pa->pa_intrline;
	int imrbit = 0, bus, device;
	/*
	 * Get bit number in mystery ICU imr
	 */
	static const signed char imrmap[][4] = {
#		define	IRQSPLIT(o) { (o), (o)+1, (o)+16, (o)+16+1 }
#		define	IRQNONE		 { 0, 0, 0, 0 }
		/*  0  */ { 1, 0, 0, 0 },	/* Noritake and Pintake */
		/*  1  */ IRQSPLIT(8),
		/*  2  */ IRQSPLIT(10),
		/*  3  */ IRQSPLIT(12),
		/*  4  */ IRQSPLIT(14),
		/*  5  */ { 1, 0, 0, 0 },	/* Corelle */
		/*  6  */ { 10, 0, 0, 0 },	/* Corelle */
		/*  7  */ IRQNONE,
		/*  8  */ IRQNONE,		/* see imrmap2[] below */
		/*  9  */ IRQNONE,
		/* 10  */ IRQNONE,
		/* 11  */ IRQSPLIT(2),
		/* 12  */ IRQSPLIT(4),
		/* 13  */ IRQSPLIT(6),
		/* 14  */ IRQSPLIT(8)		/* Corelle */
	}, imrmap2[][4] = {
		/*  0 */ { 1, 0, 0, 0 },	/* isp */
		/*  1 */  IRQSPLIT(8),
		/*  2 */  IRQSPLIT(10),
		/*  3 */  IRQSPLIT(12),
		/*  4 */  IRQSPLIT(14)
	};

	/*
	 * The console places the interrupt mapping in the "line" value.
	 * We trust it whenever possible.
	 */
	if (line >= 0 && line < PCI_NIRQ) {
		imrbit = line + 1;
	} else {
		if (pa->pa_bridgetag) {
			buspin = pa->pa_rawintrpin;
			bus = pa->pa_bus;
			device = pa->pa_device;
		
			if (bus == 2) {
				/*
				 * Devices behind ppb1 (pci bus #2).
				 * Those use fixed per-slot assignments.
				 */
				if (0 <= device && device <
				    sizeof imrmap2 / sizeof imrmap2[0]) {
					imrbit = imrmap2[device][buspin - 1];
				}
			} else {
				/*
				 * Devices behind further ppb.
				 * Those reuse ppb configured interrupts.
				 */
				buspin = PPB_INTERRUPT_SWIZZLE(buspin, device);
				if (pa->pa_bridgeih[buspin - 1] != 0) {
					imrbit =
					   IRQ2IMR(pa->pa_bridgeih[buspin - 1]);
				}
			}
		} else {
			pci_decompose_tag(pa->pa_pc, bustag, &bus, &device,
			    NULL);
			buspin = pa->pa_intrpin;
			if (0 <= device &&
			    device < sizeof imrmap / sizeof imrmap[0])
				imrbit = imrmap[device][buspin - 1];
		}
	}

	if (imrbit) {
		*ihp = IMR2IRQ(imrbit);
		return 0;
	}

	return 1;
}

const char *
dec_1000a_intr_string(void *ccv, pci_intr_handle_t ih)
{
	static const char irqmsg_fmt[] = "dec_1000a irq %ld";
	static char irqstr[sizeof irqmsg_fmt];


	if (ih >= PCI_NIRQ)
		panic("dec_1000a_intr_string: bogus dec_1000a IRQ 0x%lx", ih);

	snprintf(irqstr, sizeof irqstr, irqmsg_fmt, ih);
	return (irqstr);
}

int
dec_1000a_intr_line(void *ccv, pci_intr_handle_t ih)
{
#if NSIO > 0
	return sio_intr_line(NULL /*XXX*/, ih);
#else
	return (ih);
#endif
}

void *
dec_1000a_intr_establish(void *ccv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	void *cookie;

	if (ih >= PCI_NIRQ)
		panic("dec_1000a_intr_establish: IRQ too high, 0x%lx", ih);

	cookie = alpha_shared_intr_establish(dec_1000a_pci_intr, ih, IST_LEVEL,
	    level, func, arg, name);

	if (cookie != NULL &&
	    alpha_shared_intr_firstactive(dec_1000a_pci_intr, ih)) {
		scb_set(0x900 + SCB_IDXTOVEC(ih), dec_1000a_iointr, NULL);
		dec_1000a_enable_intr(ih);

	}
	return (cookie);
}

void
dec_1000a_intr_disestablish(void *ccv, void *cookie)
{
	struct alpha_shared_intrhand *ih = cookie;
	unsigned int irq = ih->ih_num;
	int s;

	s = splhigh();

	alpha_shared_intr_disestablish(dec_1000a_pci_intr, cookie);
	if (alpha_shared_intr_isactive(dec_1000a_pci_intr, irq) == 0) {
		dec_1000a_disable_intr(irq);
		alpha_shared_intr_set_dfltsharetype(dec_1000a_pci_intr, irq,
		    IST_NONE);
		scb_free(0x900 + SCB_IDXTOVEC(irq));
	}

	splx(s);
}

void
dec_1000a_iointr(void *framep, unsigned long vec)
{
	int irq;

	irq = SCB_VECTOIDX(vec - 0x900);

	if (!alpha_shared_intr_dispatch(dec_1000a_pci_intr, irq)) {
		alpha_shared_intr_stray(dec_1000a_pci_intr, irq,
		    "dec_1000a irq");
		if (ALPHA_SHARED_INTR_DISABLE(dec_1000a_pci_intr, irq))
			dec_1000a_disable_intr(irq);
	} else
		alpha_shared_intr_reset_strays(dec_1000a_pci_intr, irq);
}

/*
 * Read and write the mystery ICU IMR registers
 */

#define	IR(h) bus_space_read_2(mystery_icu_iot, mystery_icu_ioh[h], 0)
#define	IW(h, v) bus_space_write_2(mystery_icu_iot, mystery_icu_ioh[h], 0, (v))

/*
 * Enable and disable interrupts at the ICU level
 */

void
dec_1000a_enable_intr(int irq)
{
	int imrval = IRQ2IMR(irq);
	int i = imrval >= 16;

	IW(i, IR(i) | 1 << (imrval & 0xf));
}

void
dec_1000a_disable_intr(int irq)
{
	int imrval = IRQ2IMR(irq);
	int i = imrval >= 16;

	IW(i, IR(i) & ~(1 << (imrval & 0xf)));
}
/*
 * Initialize mystery ICU
 */
void
pci_1000a_imi(void)
{
	IW(0, IR(0) & 1);
	IW(1, IR(0) & 3);
}
