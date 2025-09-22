/*	$OpenBSD: xspd.c,v 1.7 2022/03/11 18:00:52 mpi Exp $	*/

/*
 * Copyright (c) 2015 Mike Belopuhov
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "xen.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <sys/task.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <machine/i82093var.h>

#include <dev/pv/xenreg.h>
#include <dev/pv/xenvar.h>

struct xspd_softc {
	struct device		sc_dev;
	void *			sc_ih;
};

int 	xspd_match(struct device *, void *, void *);
void	xspd_attach(struct device *, struct device *, void *);
int	xspd_intr(void *);

struct cfdriver xspd_cd = {
	NULL, "xspd", DV_DULL
};

const struct cfattach xspd_ca = {
	sizeof(struct xspd_softc), xspd_match, xspd_attach, NULL, NULL
};

const struct pci_matchid xspd_devices[] = {
	{ PCI_VENDOR_XENSOURCE, PCI_PRODUCT_XENSOURCE_PLATFORMDEV }
};

int
xspd_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid(aux, xspd_devices, nitems(xspd_devices)));
}

#if NXEN > 0
void
xspd_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct xspd_softc *sc = (struct xspd_softc *)self;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;
	struct xen_hvm_param xhp;
	extern struct xen_softc *xen_sc;

	if (xen_sc == NULL || (xen_sc->sc_flags & XSF_CBVEC)) {
		printf("\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET | IPL_MPSAFE,
	    xspd_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	xhp.domid = DOMID_SELF;
	xhp.index = HVM_PARAM_CALLBACK_IRQ;

	if (ih.line & APIC_INT_VIA_APIC)
		xhp.value = HVM_CALLBACK_PCI_INTX(pa->pa_device,
		    pa->pa_intrpin - 1);
	else
		xhp.value = HVM_CALLBACK_GSI(pci_intr_line(pa->pa_pc, ih));

	if (xen_hypercall(xen_sc, XC_HVM, 2, HVMOP_set_param, &xhp)) {
		printf("%s: failed to register callback PCI vector\n",
		    sc->sc_dev.dv_xname);
		pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
		return;
	}

	xen_sc->sc_flags |= XSF_CBVEC;
}

int
xspd_intr(void *arg)
{
	xen_intr();

	return (1);
}
#else
void
xspd_attach(struct device *parent, struct device *self, void *aux)
{
	printf("\n");
}
#endif	/* NXEN > 0 */
