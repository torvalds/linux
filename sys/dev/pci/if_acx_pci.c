/*	$OpenBSD: if_acx_pci.c,v 1.11 2024/05/24 06:02:53 jsg Exp $  */

/*-
 * Copyright (c) 2006 Theo de Raadt <deraadt@openbsd.org>
 * Copyright (c) 2005, 2006
 *	Damien Bergamini <damien.bergamini@free.fr>
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

/*
 * PCI front-end for the ACX100/111
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/acxvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

struct acx_pci_softc {
	struct acx_softc	sc_acx;

	/* PCI specific goo */
	struct acx_opns		*sc_opns;
	pci_chipset_tag_t	sc_pc;
	void			*sc_ih;
	bus_size_t		sc_mapsize1;
	bus_size_t		sc_mapsize2;
	int			sc_intrline;

	/* hack for ACX100A */
	bus_space_tag_t		sc_io_bt;
	bus_space_handle_t	sc_io_bh;
	bus_size_t		sc_iomapsize;
};

/* Base Address Register */
#define ACX_PCI_BAR0	0x10
#define ACX_PCI_BAR1	0x14
#define ACX_PCI_BAR2	0x18

int	acx_pci_match(struct device *, void *, void *);
void	acx_pci_attach(struct device *, struct device *, void *);
int	acx_pci_detach(struct device *, int);

const struct cfattach acx_pci_ca = {
	sizeof (struct acx_pci_softc), acx_pci_match, acx_pci_attach,
	acx_pci_detach
};

const struct pci_matchid acx_pci_devices[] = {
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX100A	},
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX100B	},
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX111	}
};

int
acx_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, acx_pci_devices,
	    sizeof (acx_pci_devices) / sizeof (acx_pci_devices[0])));
}

void
acx_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct acx_pci_softc *psc = (struct acx_pci_softc *)self;
	struct acx_softc *sc = &psc->sc_acx;
	struct pci_attach_args *pa = aux;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;
	int error, b1 = ACX_PCI_BAR0, b2 = ACX_PCI_BAR1;

	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pa->pa_pc;

	/* map control/status registers */
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_TI_ACX100A) {
		error = pci_mapreg_map(pa, ACX_PCI_BAR0,
		    PCI_MAPREG_TYPE_IO, 0, &psc->sc_io_bt,
		    &psc->sc_io_bh, NULL, &psc->sc_iomapsize, 0);
		if (error != 0) {
			printf(": can't map i/o space\n");
			return;
		}
		b1 = ACX_PCI_BAR1;
		b2 = ACX_PCI_BAR2;
	}

	error = pci_mapreg_map(pa, b1, PCI_MAPREG_TYPE_MEM |
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &sc->sc_mem1_bt,
	    &sc->sc_mem1_bh, NULL, &psc->sc_mapsize1, 0);
	if (error != 0) {
		printf(": can't map mem1 space\n");
		return;
	}

	error = pci_mapreg_map(pa, b2, PCI_MAPREG_TYPE_MEM |
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &sc->sc_mem2_bt,
	    &sc->sc_mem2_bh, NULL, &psc->sc_mapsize2, 0);
	if (error != 0) {
		printf(": can't map mem2 space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(psc->sc_pc, ih);
	psc->sc_ih = pci_intr_establish(psc->sc_pc, ih, IPL_NET,
	    acx_intr, sc, sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_TI_ACX111)
		acx111_set_param(sc);
	else
		acx100_set_param(sc);

	acx_attach(sc);
}

int
acx_pci_detach(struct device *self, int flags)
{
	struct acx_pci_softc *psc = (struct acx_pci_softc *)self;
	struct acx_softc *sc = &psc->sc_acx;

	acx_detach(sc);
	pci_intr_disestablish(psc->sc_pc, psc->sc_ih);

	return 0;
}
