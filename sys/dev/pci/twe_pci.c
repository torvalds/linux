/*	$OpenBSD: twe_pci.c,v 1.16 2024/05/24 06:02:58 jsg Exp $	*/

/*
 * Copyright (c) 2000 Michael Shalayeff
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <machine/bus.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/twereg.h>
#include <dev/ic/twevar.h>

#define	TWE_BAR	0x10

int	twe_pci_match(struct device *, void *, void *);
void	twe_pci_attach(struct device *, struct device *, void *);

const struct cfattach twe_pci_ca = {
	sizeof(struct twe_softc), twe_pci_match, twe_pci_attach
};

const struct pci_matchid twe_pci_devices[] = {
	{ PCI_VENDOR_3WARE, PCI_PRODUCT_3WARE_ESCALADE },
	{ PCI_VENDOR_3WARE, PCI_PRODUCT_3WARE_ESCALADE_ASIC }
};

int
twe_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, twe_pci_devices,
	    nitems(twe_pci_devices)));
}

void
twe_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct twe_softc *sc = (struct twe_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_intr_handle_t ih;
	const char *intrstr;
	bus_size_t size;

	if (pci_mapreg_map(pa, TWE_BAR, PCI_MAPREG_TYPE_IO, 0,
	    &sc->iot, &sc->ioh, NULL, &size, 0)) {
		printf(": can't map controller i/o space\n");
		return;
	}
	sc->dmat = pa->pa_dmat;

	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		bus_space_unmap(sc->iot, sc->ioh, size);
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_BIO, twe_intr, sc,
	    sc->sc_dev.dv_xname);
	if (!sc->sc_ih) {
		printf(": can't establish interrupt");
		if (intrstr)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->iot, sc->ioh, size);
		return;
	}

	printf(": %s\n%s", intrstr, sc->sc_dev.dv_xname);

	if (twe_attach(sc)) {
		pci_intr_disestablish(pa->pa_pc, sc->sc_ih);
		sc->sc_ih = NULL;
		bus_space_unmap(sc->iot, sc->ioh, size);
	}
}
