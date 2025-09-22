/*	$OpenBSD: pci_axppci_33.c,v 1.25 2025/06/29 15:55:21 miod Exp $	*/
/*	$NetBSD: pci_axppci_33.c,v 1.10 1996/11/13 21:13:29 cgd Exp $	*/

/*
 * Copyright (c) 1995, 1996 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Authors: Jeffrey Hsu and Chris G. Demetriou
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
#include <sys/device.h>
#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/isa/isavar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <alpha/pci/lcavar.h>

#include <alpha/pci/pci_axppci_33.h>
#include <alpha/pci/siovar.h>
#include <alpha/pci/sioreg.h>

#include "sio.h"

int     dec_axppci_33_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *dec_axppci_33_intr_string(void *, pci_intr_handle_t);
int	dec_axppci_33_intr_line(void *, pci_intr_handle_t);
void    *dec_axppci_33_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void    dec_axppci_33_intr_disestablish(void *, void *);

#define	LCA_SIO_DEVICE	7	/* XXX */

void
pci_axppci_33_pickintr(struct lca_config *lcp)
{
	bus_space_tag_t iot = &lcp->lc_iot;
	pci_chipset_tag_t pc = &lcp->lc_pc;
	pcireg_t sioclass;
	int sioII;

	/* XXX MAGIC NUMBER */
	sioclass = pci_conf_read(pc, pci_make_tag(pc, 0, LCA_SIO_DEVICE, 0),
	    PCI_CLASS_REG);
	sioII = (sioclass & 0xff) >= 3;

	if (!sioII)
		printf("WARNING: SIO NOT SIO II... NO BETS...\n");

	pc->pc_intr_v = lcp;
	pc->pc_intr_map = dec_axppci_33_intr_map;
	pc->pc_intr_string = dec_axppci_33_intr_string;
	pc->pc_intr_line = dec_axppci_33_intr_line;
	pc->pc_intr_establish = dec_axppci_33_intr_establish;
	pc->pc_intr_disestablish = dec_axppci_33_intr_disestablish;

	/* Not supported on AXPpci33. */
	pc->pc_pciide_compat_intr_establish = NULL;
	pc->pc_pciide_compat_intr_disestablish = NULL;

#if NSIO > 0
	sio_intr_setup(pc, iot);
#else
	panic("pci_axppci_33_pickintr: no I/O interrupt handler (no sio)");
#endif
}

int
dec_axppci_33_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pcitag_t bustag = pa->pa_intrtag;
	pci_chipset_tag_t pc = pa->pa_pc;
	int buspin, device, pirq;
	pcireg_t pirqreg;
	u_int8_t pirqline;

	if (pa->pa_bridgetag) {
		buspin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin,
		    pa->pa_device);
		if (pa->pa_bridgeih[buspin - 1] != 0) {
			*ihp = pa->pa_bridgeih[buspin - 1];
			return 0;
		}

		return 1;
	}

	buspin = pa->pa_intrpin;
	pci_decompose_tag(pc, bustag, NULL, &device, NULL);

	switch (device) {
	case 6:					/* NCR SCSI */
		pirq = 3;
		break;

	case 11:				/* slot 1 */
		switch (buspin) {
		default:
		case PCI_INTERRUPT_PIN_A:
		case PCI_INTERRUPT_PIN_D:
			pirq = 0;
			break;
		case PCI_INTERRUPT_PIN_B:
			pirq = 2;
			break;
		case PCI_INTERRUPT_PIN_C:
			pirq = 1;
			break;
		}
		break;

	case 12:				/* slot 2 */
		switch (buspin) {
		default:
		case PCI_INTERRUPT_PIN_A:
		case PCI_INTERRUPT_PIN_D:
			pirq = 1;
			break;
		case PCI_INTERRUPT_PIN_B:
			pirq = 0;
			break;
		case PCI_INTERRUPT_PIN_C:
			pirq = 2;
			break;
		}
		break;

	case 8:				/* slot 3 */
		switch (buspin) {
		default:
		case PCI_INTERRUPT_PIN_A:
		case PCI_INTERRUPT_PIN_D:
			pirq = 2;
			break;
		case PCI_INTERRUPT_PIN_B:
			pirq = 1;
			break;
		case PCI_INTERRUPT_PIN_C:
			pirq = 0;
			break;
		}
		break;

	default:
		return 1;
	}

	pirqreg = pci_conf_read(pc, pci_make_tag(pc, 0, LCA_SIO_DEVICE, 0),
	    SIO_PCIREG_PIRQ_RTCTRL);

	pirqline = (pirqreg >> (pirq * 8)) & 0xff;
	if ((pirqline & 0x80) != 0)
		return 1;			/* not routed? */
	pirqline &= 0xf;

	*ihp = pirqline;
	return (0);
}

const char *
dec_axppci_33_intr_string(void *lcv, pci_intr_handle_t ih)
{
	return sio_intr_string(NULL /*XXX*/, ih);
}

int
dec_axppci_33_intr_line(void *lcv, pci_intr_handle_t ih)
{
	return sio_intr_line(NULL /*XXX*/, ih);
}

void *
dec_axppci_33_intr_establish(void *lcv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
	return sio_intr_establish(NULL /*XXX*/, ih, IST_LEVEL, level, func,
	    arg, name);
}

void
dec_axppci_33_intr_disestablish(void *lcv, void *cookie)
{
	sio_intr_disestablish(NULL /*XXX*/, cookie);
}
