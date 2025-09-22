/* $OpenBSD: pci_alphabook1.c,v 1.6 2025/06/29 15:55:21 miod Exp $ */
/* $NetBSD: pci_alphabook1.c,v 1.16 2012/02/06 02:14:15 matt Exp $ */

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

#include <machine/intr.h>

#include <dev/isa/isavar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/ppbreg.h>

#include <alpha/pci/lcavar.h>

#include <alpha/pci/pci_alphabook1.h>
#include <alpha/pci/siovar.h>

#include "sio.h"

int     dec_alphabook1_intr_map(struct pci_attach_args *,
	    pci_intr_handle_t *);
const char *dec_alphabook1_intr_string(void *, pci_intr_handle_t);
int	 dec_alphabook1_intr_line(void *, pci_intr_handle_t);
void    *dec_alphabook1_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void    dec_alphabook1_intr_disestablish(void *, void *);

#define	LCA_SIO_DEVICE	7	/* XXX */

void
pci_alphabook1_pickintr(struct lca_config *lcp)
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
	pc->pc_intr_map = dec_alphabook1_intr_map;
	pc->pc_intr_string = dec_alphabook1_intr_string;
	pc->pc_intr_line = dec_alphabook1_intr_line;
	pc->pc_intr_establish = dec_alphabook1_intr_establish;
	pc->pc_intr_disestablish = dec_alphabook1_intr_disestablish;

	/* Not supported on AlphaBook. */
	pc->pc_pciide_compat_intr_establish = NULL;

#if NSIO
	sio_intr_setup(pc, iot);
#else
	panic("pci_alphabook1_pickintr: no I/O interrupt handler (no sio)");
#endif
}

int
dec_alphabook1_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	pcitag_t bustag = pa->pa_intrtag;
	int buspin, device;

#ifdef notyet
	if (pa->pa_bridgetag) {
		buspin = PPB_INTERRUPT_SWIZZLE(pa->pa_rawintrpin,
		    pa->pa_device);
		if (pa->pa_bridgeih[buspin - 1] == 0)
			return 1;

		*ihp = pa->pa_bridgeih[buspin - 1];
		return 0;
	}
#endif

	buspin = pa->pa_intrpin;
	pci_decompose_tag(pa->pa_pc, bustag, NULL, &device, NULL);

	/*
	 * There are only two interrupting PCI devices on the AlphaBook:
	 * the SCSI and PCMCIA controllers. The other PCI device is the
	 * SIO, and there are no option slots available.
	 *
	 * NOTE!  Apparently, there was a later AlphaBook which uses
	 * a different interrupt scheme, and has a built-in Tulip Ethernet
	 * interface!  We do not handle that here!
	 */

	switch (device) {
	case 6:					/* NCR SCSI */
		*ihp = 14;
		return 0;
	case 8:					/* Cirrus CL-PD6729 */
		*ihp = 15;
		return 0;
	default:
		return 1;
	}
}

const char *
dec_alphabook1_intr_string(void *lcv, pci_intr_handle_t ih)
{
	return sio_intr_string(NULL /*XXX*/, ih);
}

int
dec_alphabook1_intr_line(void *lcv, pci_intr_handle_t ih)
{
	return sio_intr_line(NULL /*XXX*/, ih);
}

void *
dec_alphabook1_intr_establish(void *lcv, pci_intr_handle_t ih,
    int level, int (*func)(void *), void *arg, const char *name)
{
	/*
	 * PCI interrupts on that platform are ISA interrupts in disguise,
	 * and are edge- rather than level-triggered.
	 */
	return sio_intr_establish(NULL /*XXX*/, ih, IST_EDGE, level, func,
	    arg, name);
}

void
dec_alphabook1_intr_disestablish(void *lcv, void *cookie)
{
	sio_intr_disestablish(NULL /*XXX*/, cookie);
}
