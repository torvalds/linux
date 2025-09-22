/* $OpenBSD: pci_eb64plus.c,v 1.18 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pci_eb64plus.c,v 1.10 2001/07/27 00:25:20 thorpej Exp $ */

/*-
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.
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
#include <sys/syslog.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <alpha/pci/apecsreg.h>
#include <alpha/pci/apecsvar.h>

#include <alpha/pci/pci_eb64plus.h>

#include "sio.h"
#if NSIO
#include <alpha/pci/siovar.h>
#endif

int	dec_eb64plus_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *dec_eb64plus_intr_string(void *, pci_intr_handle_t);
void	*dec_eb64plus_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void	dec_eb64plus_intr_disestablish(void *, void *);

#define	EB64PLUS_MAX_IRQ	32
#define	PCI_STRAY_MAX		5

struct alpha_shared_intr *eb64plus_pci_intr;

bus_space_tag_t eb64plus_intrgate_iot;
bus_space_handle_t eb64plus_intrgate_ioh;

void	eb64plus_iointr(void *arg, unsigned long vec);
extern void	eb64plus_intr_enable(int irq);  /* pci_eb64plus_intr.S */
extern void	eb64plus_intr_disable(int irq); /* pci_eb64plus_intr.S */

void
pci_eb64plus_pickintr(struct apecs_config *acp)
{
	bus_space_tag_t iot = &acp->ac_iot;
	pci_chipset_tag_t pc = &acp->ac_pc;
	int i;

	pc->pc_intr_v = acp;
	pc->pc_intr_map = dec_eb64plus_intr_map;
	pc->pc_intr_string = dec_eb64plus_intr_string;
	pc->pc_intr_establish = dec_eb64plus_intr_establish;
	pc->pc_intr_disestablish = dec_eb64plus_intr_disestablish;

	/* Not supported on the EB64+. */
	pc->pc_pciide_compat_intr_establish = NULL;

	eb64plus_intrgate_iot = iot;
	if (bus_space_map(eb64plus_intrgate_iot, 0x804, 3, 0,
	    &eb64plus_intrgate_ioh) != 0)
		panic("pci_eb64plus_pickintr: couldn't map interrupt PLD");
	for (i = 0; i < EB64PLUS_MAX_IRQ; i++)
		eb64plus_intr_disable(i);	

	eb64plus_pci_intr = alpha_shared_intr_alloc(EB64PLUS_MAX_IRQ);
	for (i = 0; i < EB64PLUS_MAX_IRQ; i++) {
		alpha_shared_intr_set_maxstrays(eb64plus_pci_intr, i,
			PCI_STRAY_MAX);
	}

#if NSIO > 0
	sio_intr_setup(pc, iot);
#endif
}

int
dec_eb64plus_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int buspin, line = pa->pa_intrline;

	/*
	 * The console places the interrupt mapping in the "line" value.
	 * We trust it whenever possible.
	 */
	if (line >= 0 && line < EB64PLUS_MAX_IRQ) {
		*ihp = line;
		return 0;
	}

	if (pa->pa_bridgetag) {
		buspin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin,
		    pa->pa_device);
		if (pa->pa_bridgeih[buspin - 1] != 0) {
			*ihp = pa->pa_bridgeih[buspin - 1];
			return 0;
		}
	}

	return 1;
}

const char *
dec_eb64plus_intr_string(void *acv, pci_intr_handle_t ih)
{
	static char irqstr[15];	  /* 11 + 2 + NULL + sanity */

	if (ih >= EB64PLUS_MAX_IRQ)
		panic("dec_eb64plus_intr_string: bogus eb64+ IRQ 0x%lx", ih);
	snprintf(irqstr, sizeof irqstr, "eb64+ irq %ld", ih);
	return (irqstr);
}

void *
dec_eb64plus_intr_establish(void *acv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	void *cookie;

	if (ih >= EB64PLUS_MAX_IRQ)
		panic("dec_eb64plus_intr_establish: bogus eb64+ IRQ 0x%lx",
		    ih);

	cookie = alpha_shared_intr_establish(eb64plus_pci_intr, ih, IST_LEVEL,
	    level, func, arg, name);

	if (cookie != NULL &&
	    alpha_shared_intr_firstactive(eb64plus_pci_intr, ih)) {
		scb_set(0x900 + SCB_IDXTOVEC(ih), eb64plus_iointr, NULL);
		eb64plus_intr_enable(ih);
	}
	return (cookie);
}

void
dec_eb64plus_intr_disestablish(void *acv, void *cookie)
{
	struct alpha_shared_intrhand *ih = cookie;
	unsigned int irq = ih->ih_num;
	int s;

	s = splhigh();

	alpha_shared_intr_disestablish(eb64plus_pci_intr, cookie);
	if (alpha_shared_intr_isactive(eb64plus_pci_intr, irq) == 0) {
		eb64plus_intr_disable(irq);
		alpha_shared_intr_set_dfltsharetype(eb64plus_pci_intr, irq,
		    IST_NONE);
		scb_free(0x900 + SCB_IDXTOVEC(irq));
	}

	splx(s);
}

void
eb64plus_iointr(void *arg, unsigned long vec)
{
	int irq;

	irq = SCB_VECTOIDX(vec - 0x900);

	if (!alpha_shared_intr_dispatch(eb64plus_pci_intr, irq)) {
		alpha_shared_intr_stray(eb64plus_pci_intr, irq,
		    "eb64+ irq");
		if (ALPHA_SHARED_INTR_DISABLE(eb64plus_pci_intr, irq))
			eb64plus_intr_disable(irq);
	} else
		alpha_shared_intr_reset_strays(eb64plus_pci_intr, irq);
}

#if 0		/* THIS DOES NOT WORK!  see pci_eb64plus_intr.S. */
u_int8_t eb64plus_intr_mask[3] = { 0xff, 0xff, 0xff };

void
eb64plus_intr_enable(int irq)
{
	int byte = (irq / 8), bit = (irq % 8);

#if 1
	printf("eb64plus_intr_enable: enabling %d (%d:%d)\n", irq, byte, bit);
#endif
	eb64plus_intr_mask[byte] &= ~(1 << bit);

	bus_space_write_1(eb64plus_intrgate_iot, eb64plus_intrgate_ioh, byte,
	    eb64plus_intr_mask[byte]);
}

void
eb64plus_intr_disable(int irq)
{
	int byte = (irq / 8), bit = (irq % 8);

#if 1
	printf("eb64plus_intr_disable: disabling %d (%d:%d)\n", irq, byte, bit);
#endif
	eb64plus_intr_mask[byte] |= (1 << bit);

	bus_space_write_1(eb64plus_intrgate_iot, eb64plus_intrgate_ioh, byte,
	    eb64plus_intr_mask[byte]);
}
#endif
