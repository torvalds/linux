/*	$OpenBSD: cy_pci.c,v 1.16 2022/03/11 18:00:45 mpi Exp $	*/
/*
 * Copyright (c) 1996 Timo Rossi.
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
 * 3. Neither the name of the author nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * cy_pci.c
 *
 * Driver for Cyclades Cyclom-8/16/32 multiport serial cards
 * (currently not tested with Cyclom-32 cards)
 *
 * Timo Rossi, 1996
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/ic/cd1400reg.h>
#include <dev/ic/cyreg.h>

int cy_pci_match(struct device *, void *, void *);
void cy_pci_attach(struct device *, struct device *, void *);

struct cy_pci_softc {
	struct cy_softc 	sc_cy;		/* real softc */

	bus_space_tag_t		sc_iot;		/* PLX i/o tag */
	bus_space_handle_t	sc_ioh;		/* PLX i/o handle */
};

const struct cfattach cy_pci_ca = {
	sizeof(struct cy_pci_softc), cy_pci_match, cy_pci_attach
};

#define CY_PLX_9050_ICS_IENABLE		0x040
#define CY_PLX_9050_ICS_LOCAL_IENABLE	0x001
#define CY_PLX_9050_ICS_LOCAL_IPOLARITY	0x002
#define CY_PLX_9060_ICS_IENABLE		0x100
#define CY_PLX_9060_ICS_LOCAL_IENABLE	0x800

const struct pci_matchid cy_pci_devices[] = {
	{ PCI_VENDOR_CYCLADES, PCI_PRODUCT_CYCLADES_CYCLOMY_1 },
	{ PCI_VENDOR_CYCLADES, PCI_PRODUCT_CYCLADES_CYCLOMY_2 },
	{ PCI_VENDOR_CYCLADES, PCI_PRODUCT_CYCLADES_CYCLOM4Y_1 },
	{ PCI_VENDOR_CYCLADES, PCI_PRODUCT_CYCLADES_CYCLOM4Y_2 },
	{ PCI_VENDOR_CYCLADES, PCI_PRODUCT_CYCLADES_CYCLOM8Y_1 },
	{ PCI_VENDOR_CYCLADES, PCI_PRODUCT_CYCLADES_CYCLOM8Y_2 },
};

int
cy_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, cy_pci_devices,
	    nitems(cy_pci_devices)));
}

void
cy_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct cy_pci_softc *psc = (struct cy_pci_softc *)self;
	struct cy_softc *sc = (struct cy_softc *)self;
	struct pci_attach_args *pa = aux;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;
	pcireg_t memtype;
	int plx_ver;

	sc->sc_bustype = CY_BUSTYPE_PCI;

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_CYCLADES_CYCLOMY_1:
	case PCI_PRODUCT_CYCLADES_CYCLOM4Y_1:
	case PCI_PRODUCT_CYCLADES_CYCLOM8Y_1:
		memtype = PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT_1M;
		break;
	case PCI_PRODUCT_CYCLADES_CYCLOMY_2:
	case PCI_PRODUCT_CYCLADES_CYCLOM4Y_2:
	case PCI_PRODUCT_CYCLADES_CYCLOM8Y_2:
		memtype = PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT;
		break;
	}

	if (pci_mapreg_map(pa, 0x14, PCI_MAPREG_TYPE_IO, 0,
	    &psc->sc_iot, &psc->sc_ioh, NULL, NULL, 0) != 0) {
		printf(": unable to map PLX registers\n");
		return;
	}

	if (pci_mapreg_map(pa, 0x18, memtype, 0, &sc->sc_memt,
	    &sc->sc_memh, NULL, NULL, 0) != 0) {
                printf(": couldn't map device registers\n");
                return;
        }

	if ((sc->sc_nr_cd1400s = cy_probe_common(sc->sc_memt, sc->sc_memh,
	    CY_BUSTYPE_PCI)) == 0) {
		printf(": PCI Cyclom card with no CD1400s\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": couldn't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_TTY, cy_intr,
	    sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	cy_attach(parent, self);

	/* Get PLX version */
	plx_ver = bus_space_read_1(sc->sc_memt, sc->sc_memh, CY_PLX_VER) & 0x0f;

	/* Enable PCI card interrupts */
	switch (plx_ver) {
	case CY_PLX_9050:
		bus_space_write_2(psc->sc_iot, psc->sc_ioh, CY_PCI_INTENA_9050,
		    CY_PLX_9050_ICS_IENABLE | CY_PLX_9050_ICS_LOCAL_IENABLE |
		    CY_PLX_9050_ICS_LOCAL_IPOLARITY);
		break;

	case CY_PLX_9060:
	case CY_PLX_9080:
	default:
		bus_space_write_2(psc->sc_iot, psc->sc_ioh, CY_PCI_INTENA,
		    bus_space_read_2(psc->sc_iot, psc->sc_ioh,
		    CY_PCI_INTENA) | CY_PLX_9060_ICS_IENABLE | 
		    CY_PLX_9060_ICS_LOCAL_IENABLE);
	}
}
