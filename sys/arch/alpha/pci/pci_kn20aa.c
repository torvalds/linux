/*	$OpenBSD: pci_kn20aa.c,v 1.31 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: pci_kn20aa.c,v 1.21 1996/11/17 02:05:27 cgd Exp $	*/

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

#include <alpha/pci/ciareg.h>
#include <alpha/pci/ciavar.h>

#include <alpha/pci/pci_kn20aa.h>

#include "sio.h"
#if NSIO
#include <alpha/pci/siovar.h>
#endif

int	dec_kn20aa_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *dec_kn20aa_intr_string(void *, pci_intr_handle_t);
int	dec_kn20aa_intr_line(void *, pci_intr_handle_t);
void	*dec_kn20aa_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void	dec_kn20aa_intr_disestablish(void *, void *);

#define	KN20AA_PCEB_IRQ	31
#define	KN20AA_MAX_IRQ	32
#define	PCI_STRAY_MAX	5

struct alpha_shared_intr *kn20aa_pci_intr;
struct evcount kn20aa_intr_count;

void	kn20aa_iointr(void *arg, unsigned long vec);
void	kn20aa_enable_intr(int irq);
void	kn20aa_disable_intr(int irq);

void
pci_kn20aa_pickintr(struct cia_config *ccp)
{
	int i;
	bus_space_tag_t iot = &ccp->cc_iot;
	pci_chipset_tag_t pc = &ccp->cc_pc;

	pc->pc_intr_v = ccp;
	pc->pc_intr_map = dec_kn20aa_intr_map;
	pc->pc_intr_string = dec_kn20aa_intr_string;
	pc->pc_intr_line = dec_kn20aa_intr_line;
	pc->pc_intr_establish = dec_kn20aa_intr_establish;
	pc->pc_intr_disestablish = dec_kn20aa_intr_disestablish;

	/* Not supported on KN20AA. */
	pc->pc_pciide_compat_intr_establish = NULL;
	pc->pc_pciide_compat_intr_disestablish = NULL;

	kn20aa_pci_intr = alpha_shared_intr_alloc(KN20AA_MAX_IRQ);
	for (i = 0; i < KN20AA_MAX_IRQ; i++)
		alpha_shared_intr_set_maxstrays(kn20aa_pci_intr, i,
		    PCI_STRAY_MAX);

#if NSIO
	sio_intr_setup(pc, iot);
	kn20aa_enable_intr(KN20AA_PCEB_IRQ);
#endif
}

int
dec_kn20aa_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pcitag_t bustag = pa->pa_intrtag;
	int buspin = pa->pa_intrpin;
	pci_chipset_tag_t pc = pa->pa_pc;
	int device;
	int kn20aa_irq;

	if (pa->pa_bridgetag) {
		buspin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin,
		    pa->pa_device);
		if (pa->pa_bridgeih[buspin - 1] != 0) {
			*ihp = pa->pa_bridgeih[buspin - 1];
			return 0;
		}

		return 1;
	}

	/*
	 * Slot->interrupt translation.  Appears to work, though it
	 * may not hold up forever.
	 *
	 * The DEC engineers who did this hardware obviously engaged
	 * in random drug testing.
	 */
	pci_decompose_tag(pc, bustag, NULL, &device, NULL);
	switch (device) {
	case 11:
	case 12:
		kn20aa_irq = ((device - 11) + 0) * 4;
		break;

	case 7:
		kn20aa_irq = 8;
		break;

	case 9:
		kn20aa_irq = 12;
		break;

	case 6:					/* 21040 on AlphaStation 500 */
		kn20aa_irq = 13;
		break;

	case 8:
		kn20aa_irq = 16;
		break;

	default:
		printf("dec_kn20aa_intr_map: don't know how to setup %d/%d/%d\n",
		    pa->pa_bus, pa->pa_device, pa->pa_function);
		return 1;
	}

	if (kn20aa_irq != 13)
		kn20aa_irq += buspin - 1;

	if (kn20aa_irq >= KN20AA_MAX_IRQ) {
		printf("dec_kn20aa_intr_map: kn20aa_irq %d too large for %d/%d/%d\n",
		    kn20aa_irq, pa->pa_bus, pa->pa_device, pa->pa_function);
		return 1;
	}

	*ihp = kn20aa_irq;
	return 0;
}

const char *
dec_kn20aa_intr_string(void *ccv, pci_intr_handle_t ih)
{
	static char irqstr[15];	  /* 11 + 2 + NULL + sanity */

	if (ih > KN20AA_MAX_IRQ)
		panic("dec_kn20aa_intr_string: bogus kn20aa IRQ 0x%lx", ih);

	snprintf(irqstr, sizeof irqstr, "kn20aa irq %ld", ih);
	return (irqstr);
}

int
dec_kn20aa_intr_line(void *ccv, pci_intr_handle_t ih)
{
	return (ih);
}

void *
dec_kn20aa_intr_establish(void *ccv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	void *cookie;

	if (ih > KN20AA_MAX_IRQ)
		panic("dec_kn20aa_intr_establish: bogus kn20aa IRQ 0x%lx",
		    ih);

	cookie = alpha_shared_intr_establish(kn20aa_pci_intr, ih, IST_LEVEL,
	    level, func, arg, name);

	if (cookie != NULL &&
	    alpha_shared_intr_firstactive(kn20aa_pci_intr, ih)) {
		scb_set(0x900 + SCB_IDXTOVEC(ih), kn20aa_iointr, NULL);
		kn20aa_enable_intr(ih);
	}
	return (cookie);
}

void
dec_kn20aa_intr_disestablish(void *ccv, void *cookie)
{
	struct alpha_shared_intrhand *ih = cookie;
	unsigned int irq = ih->ih_num;
	int s;

	s = splhigh();

	alpha_shared_intr_disestablish(kn20aa_pci_intr, cookie);
	if (alpha_shared_intr_isactive(kn20aa_pci_intr, irq) == 0) {
		kn20aa_disable_intr(irq);
		alpha_shared_intr_set_dfltsharetype(kn20aa_pci_intr, irq,
		    IST_NONE);
		scb_free(0x900 + SCB_IDXTOVEC(irq));
	}
	splx(s);
}

void
kn20aa_iointr(void *arg, unsigned long vec)
{
	int irq;

	irq = SCB_VECTOIDX(vec - 0x900);

	if (!alpha_shared_intr_dispatch(kn20aa_pci_intr, irq)) {
		alpha_shared_intr_stray(kn20aa_pci_intr, irq,
		    "kn20aa irq");
		if (ALPHA_SHARED_INTR_DISABLE(kn20aa_pci_intr, irq))
			kn20aa_disable_intr(irq);
	} else
		alpha_shared_intr_reset_strays(kn20aa_pci_intr, irq);
}

void
kn20aa_enable_intr(int irq)
{

	/*
	 * From disassembling small bits of the OSF/1 kernel:
	 * the following appears to enable a given interrupt request.
	 * "blech."  I'd give valuable body parts for better docs or
	 * for a good decompiler.
	 */
	alpha_mb();
	REGVAL(0x8780000000L + 0x40L) |= (1 << irq);	/* XXX */
	alpha_mb();
}

void
kn20aa_disable_intr(int irq)
{

	alpha_mb();
	REGVAL(0x8780000000L + 0x40L) &= ~(1 << irq);	/* XXX */
	alpha_mb();
}
