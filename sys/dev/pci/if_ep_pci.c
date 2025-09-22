/*	$OpenBSD: if_ep_pci.c,v 1.38 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: if_ep_pci.c,v 1.13 1996/10/21 22:56:38 thorpej Exp $	*/

/*
 * Copyright (c) 1994 Herb Peyerl <hpeyerl@beer.org>
 * All rights reserved.
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
 *      This product includes software developed by Herb Peyerl.
 * 4. The name of Herb Peyerl may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>
 
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/ic/elink3var.h>
#include <dev/ic/elink3reg.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

/*
 * PCI constants.
 * XXX These should be in a common file!
 */
#define PCI_CONN		0x48    /* Connector type */
#define PCI_CBIO		0x10    /* Configuration Base IO Address */

int ep_pci_match(struct device *, void *, void *);
void ep_pci_attach(struct device *, struct device *, void *);

const struct cfattach ep_pci_ca = {
	sizeof(struct ep_softc), ep_pci_match, ep_pci_attach
};

const struct pci_matchid ep_pci_devices[] = {
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C590 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C595MII },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C595T4 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C595TX },
};

int
ep_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, ep_pci_devices,
	    nitems(ep_pci_devices)));
}

void
ep_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ep_softc *sc = (void *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	bus_size_t iosize;
	pci_intr_handle_t ih;
	pcireg_t i;
	const char *intrstr = NULL;

	if (pci_mapreg_map(pa, PCI_CBIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &iosize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	sc->bustype = EP_BUS_PCI;

	i = pci_conf_read(pc, pa->pa_tag, PCI_CONN);

	GO_WINDOW(0);

	printf(":");

	epconfig(sc, EP_CHIPSET_VORTEX, NULL);

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(", couldn't map interrupt\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, iosize);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, epintr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->sc_iot, sc->sc_ioh, iosize);
		return;
	}
	printf(" %s\n", intrstr);
}
