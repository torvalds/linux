/*	$OpenBSD: if_sf_pci.c,v 1.16 2024/05/24 06:02:56 jsg Exp $	*/
/*	$NetBSD: if_sf_pci.c,v 1.10 2006/06/17 23:34:27 christos Exp $	*/

/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
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

/*
 * PCI bus front-end for the Adaptec AIC-6915 (``Starfire'')
 * 10/100 Ethernet controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <net/if.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net/if_media.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/mii/miivar.h>

#include <dev/ic/aic6915.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

struct sf_pci_softc {
	struct sf_softc sc_starfire;	/* read Starfire softc */

	/* PCI-specific goo. */
	void	*sc_ih;			/* interrupt handle */
};

int	sf_pci_match(struct device *, void *, void *);
void	sf_pci_attach(struct device *, struct device *, void *);

const struct cfattach sf_pci_ca = {
        sizeof(struct sf_pci_softc), sf_pci_match, sf_pci_attach
};

const struct pci_matchid sf_pci_products[] = {
	{ PCI_VENDOR_ADP, PCI_PRODUCT_ADP_AIC6915 },
};

int
sf_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, sf_pci_products,
	    nitems(sf_pci_products)));
}

void
sf_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct sf_pci_softc *psc = (void *) self;
	struct sf_softc *sc = &psc->sc_starfire;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_space_tag_t iot, memt;
	bus_space_handle_t ioh, memh;
	int ioh_valid, memh_valid;
	bus_size_t iosize, memsize;
	pcireg_t reg;

	pci_set_powerstate(pa->pa_pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map the device.
	 */
	reg = pci_mapreg_type(pa->pa_pc, pa->pa_tag, SF_PCI_MEMBA);
	switch (reg) {
	case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT:
	case PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT:
		memh_valid = (pci_mapreg_map(pa, SF_PCI_MEMBA,
		    reg, 0, &memt, &memh, &memsize, NULL, 0) == 0);
		break;
	default:
		memh_valid = 0;
	}

	ioh_valid = (pci_mapreg_map(pa,
	    (reg == (PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_64BIT)) ?
		SF_PCI_IOBA : SF_PCI_IOBA - 0x04,
	    PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, &iosize, NULL, 0) == 0);

	if (memh_valid) {
		sc->sc_st = memt;
		sc->sc_sh = memh;
		sc->sc_iomapped = 0;
	} else if (ioh_valid) {
		sc->sc_st = iot;
		sc->sc_sh = ioh;
		sc->sc_iomapped = 1;
	} else {
		printf(": unable to map device registers\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	/*
	 * Map and establish our interrupt.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": unable to map interrupt\n");
		goto out;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	psc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET, sf_intr, sc,
	    self->dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto out;
	}
	printf(": %s", intrstr);

	/*
	 * Finish off the attach.
	 */
	sf_attach(sc);
	return;

 out:
	if (ioh_valid)
		bus_space_unmap(iot, ioh, iosize);
	if (memh_valid)
		bus_space_unmap(memt, memh, memsize);
}
