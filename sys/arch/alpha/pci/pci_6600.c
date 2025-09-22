/* $OpenBSD: pci_6600.c,v 1.23 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pci_6600.c,v 1.5 2000/06/06 00:50:15 thorpej Exp $ */

/*-
 * Copyright (c) 1999 by Ross Harvey.  All rights reserved.
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
 *	This product includes software developed by Ross Harvey.
 * 4. The name of Ross Harvey may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ROSS HARVEY ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURP0SE
 * ARE DISCLAIMED.  IN NO EVENT SHALL ROSS HARVEY BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#define _ALPHA_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/rpb.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>
#include <dev/pci/pciidereg.h>
#include <dev/pci/pciidevar.h>

#include <alpha/pci/tsreg.h>
#include <alpha/pci/tsvar.h>
#include <alpha/pci/pci_6600.h>

#define pci_6600() { Generate ctags(1) key. }

#include "sio.h"
#if NSIO
#include <alpha/pci/siovar.h>
#endif

#define	PCI_NIRQ		64
#define	PCI_STRAY_MAX		5

/*
 * Some Tsunami models have a PCI device (the USB controller) with interrupts
 * tied to ISA IRQ lines.  The IRQ is encoded as:
 *
 *	line = 0xe0 | isa_irq;
 */
#define	DEC_6600_LINE_IS_ISA(line)	((line) >= 0xe0 && (line) <= 0xef)
#define	DEC_6600_LINE_ISA_IRQ(line)	((line) & 0x0f)

static struct tsp_config *sioprimary;

void dec_6600_intr_disestablish(void *, void *);
void *dec_6600_intr_establish(void *, pci_intr_handle_t, int,
    int (*func)(void *), void *, const char *);
const char *dec_6600_intr_string(void *, pci_intr_handle_t);
int dec_6600_intr_line(void *, pci_intr_handle_t);
int dec_6600_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
void *dec_6600_pciide_compat_intr_establish(void *, struct device *,
    struct pci_attach_args *, int, int (*)(void *), void *);
void  dec_6600_pciide_compat_intr_disestablish(void *, void *);

struct alpha_shared_intr *dec_6600_pci_intr;

void dec_6600_iointr(void *arg, unsigned long vec);
extern void dec_6600_intr_enable(int irq);
extern void dec_6600_intr_disable(int irq);

void
pci_6600_pickintr(struct tsp_config *pcp)
{
	bus_space_tag_t iot = &pcp->pc_iot;
	pci_chipset_tag_t pc = &pcp->pc_pc;
#if 0
	char *cp;
#endif
	int i;

	pc->pc_intr_v = pcp;
	pc->pc_intr_map = dec_6600_intr_map;
	pc->pc_intr_string = dec_6600_intr_string;
	pc->pc_intr_line = dec_6600_intr_line;
	pc->pc_intr_establish = dec_6600_intr_establish;
	pc->pc_intr_disestablish = dec_6600_intr_disestablish;
	pc->pc_pciide_compat_intr_establish = NULL;

	/*
	 * System-wide and Pchip-0-only logic...
	 */
	if (dec_6600_pci_intr == NULL) {
		sioprimary = pcp;
		pc->pc_pciide_compat_intr_establish =
		    dec_6600_pciide_compat_intr_establish;
		dec_6600_pci_intr = alpha_shared_intr_alloc(PCI_NIRQ);
		for (i = 0; i < PCI_NIRQ; i++) {
			alpha_shared_intr_set_maxstrays(dec_6600_pci_intr, i,
			    PCI_STRAY_MAX);
			alpha_shared_intr_set_private(dec_6600_pci_intr, i,
			    sioprimary);
		}
#if NSIO
		sio_intr_setup(pc, iot);
		dec_6600_intr_enable(55);	/* irq line for sio */
#endif
	}
}

int
dec_6600_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int buspin, line = pa->pa_intrline;

	/*
	 * The console places the interrupt mapping in the "line" value.
	 * We trust it whenever possible.
	 */
	if (line >= 0 && line < PCI_NIRQ) {
		*ihp = line;
		return 0;
	}
	if (DEC_6600_LINE_IS_ISA(line)) {
#if NSIO > 0
		*ihp = line;
		return 0;
#else
		printf("dec_6600_intr_map: ISA IRQ %d for %d/%d/%d\n",
		    DEC_6600_LINE_ISA_IRQ(line),
		    pa->pa_bus, pa->pa_device, pa->pa_function);
		return 1;
#endif
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
dec_6600_intr_string(void *acv, pci_intr_handle_t ih)
{

	static const char irqfmt[] = "dec 6600 irq %ld";
	static char irqstr[sizeof irqfmt];

#if NSIO
	if (DEC_6600_LINE_IS_ISA(ih))
		return (sio_intr_string(NULL /*XXX*/,
		    DEC_6600_LINE_ISA_IRQ(ih)));
#endif

	snprintf(irqstr, sizeof irqstr, irqfmt, ih);
	return (irqstr);
}

int
dec_6600_intr_line(void *acv, pci_intr_handle_t ih)
{

#if NSIO
	if (DEC_6600_LINE_IS_ISA(ih))
		return (sio_intr_line(NULL /*XXX*/,
		    DEC_6600_LINE_ISA_IRQ(ih)));
#endif

	return (ih);
}

void *
dec_6600_intr_establish(void *acv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	void *cookie;

#if NSIO
	if (DEC_6600_LINE_IS_ISA(ih))
		return (sio_intr_establish(NULL /*XXX*/,
		    DEC_6600_LINE_ISA_IRQ(ih), IST_LEVEL, level, func, arg,
		    name));
#endif

	if (ih >= PCI_NIRQ)
		panic("dec_6600_intr_establish: bogus dec 6600 IRQ 0x%lx",
		    ih);

	cookie = alpha_shared_intr_establish(dec_6600_pci_intr, ih, IST_LEVEL,
	    level, func, arg, name);

	if (cookie != NULL &&
	    alpha_shared_intr_firstactive(dec_6600_pci_intr, ih)) {
		scb_set(0x900 + SCB_IDXTOVEC(ih), dec_6600_iointr, NULL);
		dec_6600_intr_enable(ih);
	}
	return (cookie);
}

void
dec_6600_intr_disestablish(void *acv, void *cookie)
{
	struct alpha_shared_intrhand *ih = cookie;
	unsigned int irq = ih->ih_num;
	int s;

#if NSIO
	/*
	 * We have to determine if this is an ISA IRQ or not!  We do this
	 * by checking to see if the intrhand points back to an intrhead
	 * that points to the sioprimary TSP.  If not, it's an ISA IRQ.
	 * Pretty disgusting, eh?
	 */
	if (ih->ih_intrhead->intr_private != sioprimary) {
		sio_intr_disestablish(NULL /*XXX*/, cookie);
		return;
	}
#endif

	s = splhigh();

	alpha_shared_intr_disestablish(dec_6600_pci_intr, cookie);
	if (alpha_shared_intr_isactive(dec_6600_pci_intr, irq) == 0) {
		dec_6600_intr_disable(irq);
		alpha_shared_intr_set_dfltsharetype(dec_6600_pci_intr, irq,
		    IST_NONE);
		scb_free(0x900 + SCB_IDXTOVEC(irq));
	}

	splx(s);
}

void
dec_6600_iointr(void *arg, unsigned long vec)
{
	int irq;

	irq = SCB_VECTOIDX(vec - 0x900);

	if (irq >= PCI_NIRQ)
		panic("dec_6600_iointr: irq %d is too high", irq);

	if (!alpha_shared_intr_dispatch(dec_6600_pci_intr, irq)) {
		alpha_shared_intr_stray(dec_6600_pci_intr, irq, "6600 irq");
		if (ALPHA_SHARED_INTR_DISABLE(dec_6600_pci_intr, irq))
			dec_6600_intr_disable(irq);
	} else
		alpha_shared_intr_reset_strays(dec_6600_pci_intr, irq);
}

void
dec_6600_intr_enable(int irq)
{
	alpha_mb();
	STQP(TS_C_DIM0) |= 1UL << irq;
	alpha_mb();
}

void
dec_6600_intr_disable(int irq)
{
	alpha_mb();
	STQP(TS_C_DIM0) &= ~(1UL << irq);
	alpha_mb();
}

void *
dec_6600_pciide_compat_intr_establish(void *v, struct device *dev,
    struct pci_attach_args *pa, int chan, int (*func)(void *), void *arg)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	void *cookie = NULL;
	int bus, irq;

	pci_decompose_tag(pc, pa->pa_tag, &bus, NULL, NULL);

	/*
	 * If this isn't PCI bus #0 on the TSP that holds the PCI-ISA
	 * bridge, all bets are off.
	 */
	if (bus != 0 || pc->pc_intr_v != sioprimary)
		return (NULL);

	irq = PCIIDE_COMPAT_IRQ(chan);
#if NSIO
	cookie = sio_intr_establish(NULL /*XXX*/, irq, IST_EDGE, IPL_BIO,
	    func, arg, dev->dv_xname);

	if (cookie == NULL)
		return (NULL);
#endif
	return (cookie);
}

void
dec_6600_pciide_compat_intr_disestablish(void *v, void *cookie)
{
	sio_intr_disestablish(NULL, cookie);
}
