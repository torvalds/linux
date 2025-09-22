/*	$OpenBSD: pci_up1000.c,v 1.18 2017/09/08 05:36:51 deraadt Exp $	*/
/* $NetBSD: pci_up1000.c,v 1.6 2000/12/28 22:59:07 sommerfeld Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
#include <dev/pci/pciidereg.h>
#include <dev/pci/pciidevar.h>

#include <alpha/pci/irongatevar.h>

#include <alpha/pci/pci_up1000.h>
#include <alpha/pci/siovar.h>

#include "sio.h"

int     api_up1000_intr_map(struct pci_attach_args *, pci_intr_handle_t *);
const char *api_up1000_intr_string(void *, pci_intr_handle_t);
int	api_up1000_intr_line(void *, pci_intr_handle_t);
void    *api_up1000_intr_establish(void *, pci_intr_handle_t,
	    int, int (*func)(void *), void *, const char *);
void    api_up1000_intr_disestablish(void *, void *);

void	*api_up1000_pciide_compat_intr_establish(void *, struct device *,
	    struct pci_attach_args *, int, int (*)(void *), void *);
void    api_up1000_pciide_compat_intr_disestablish(void *, void *);

void
pci_up1000_pickintr(struct irongate_config *icp)
{
	bus_space_tag_t iot = &icp->ic_iot;
	pci_chipset_tag_t pc = &icp->ic_pc;

	pc->pc_intr_v = icp;
	pc->pc_intr_map = api_up1000_intr_map;
	pc->pc_intr_string = api_up1000_intr_string;
	pc->pc_intr_line = api_up1000_intr_line;
	pc->pc_intr_establish = api_up1000_intr_establish;
	pc->pc_intr_disestablish = api_up1000_intr_disestablish;

	pc->pc_pciide_compat_intr_establish =
	    api_up1000_pciide_compat_intr_establish;
	pc->pc_pciide_compat_intr_disestablish =
	    api_up1000_pciide_compat_intr_disestablish;

#if NSIO
	sio_intr_setup(pc, iot);
#else
	panic("pci_up1000_pickintr: no I/O interrupt handler (no sio)");
#endif
}

int
api_up1000_intr_map(struct pci_attach_args *pa, pci_intr_handle_t *ihp)
{
	int buspin, line = pa->pa_intrline;

	/*
	 * The console places the interrupt mapping in the "line" value.
	 * We trust it whenever possible.
	 */
	if (line >= 0 && line <= 15) {
		if (line == 2) {
#ifdef DEBUG
			printf("api_up1000_intr_map: changed IRQ 2 to IRQ 9\n");
#endif
			line = 9;
		}

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
api_up1000_intr_string(void *icv, pci_intr_handle_t ih)
{
#if 0
	struct irongate_config *icp = icv;
#endif

	return sio_intr_string(NULL /*XXX*/, ih);
}

int
api_up1000_intr_line(void *icv, pci_intr_handle_t ih)
{
	return sio_intr_line(NULL /*XXX*/, ih);
}

void *
api_up1000_intr_establish(void *icv, pci_intr_handle_t ih, int level,
    int (*func)(void *), void *arg, const char *name)
{
#if 0
	struct irongate_config *icp = icv;
#endif

	return sio_intr_establish(NULL /*XXX*/, ih, IST_LEVEL, level, func,
	    arg, name);
}

void
api_up1000_intr_disestablish(void *icv, void *cookie)
{
#if 0
	struct irongate_config *icp = icv;
#endif

	sio_intr_disestablish(NULL /*XXX*/, cookie);
}

void *
api_up1000_pciide_compat_intr_establish(void *icv, struct device *dev,
    struct pci_attach_args *pa, int chan, int (*func)(void *), void *arg)
{
	pci_chipset_tag_t pc = pa->pa_pc;
	void *cookie = NULL;
	int bus, irq;

	pci_decompose_tag(pc, pa->pa_tag, &bus, NULL, NULL);

	/*
	 * If this isn't PCI bus #0, all bets are off.
	 */
	if (bus != 0)
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
api_up1000_pciide_compat_intr_disestablish(void *v, void *cookie)
{
	sio_intr_disestablish(NULL, cookie);
}
