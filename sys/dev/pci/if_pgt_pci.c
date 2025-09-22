/*	$OpenBSD: if_pgt_pci.c,v 1.20 2024/05/24 06:02:56 jsg Exp $  */

/*
 * Copyright (c) 2006 Marcus Glocker <mglocker@openbsd.org>
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
 * PCI front-end for the PrismGT
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/pgtreg.h>
#include <dev/ic/pgtvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/* Base Address Register */
#define PGT_PCI_BAR0	0x10

int	pgt_pci_match(struct device *, void *, void *);
void	pgt_pci_attach(struct device *, struct device *, void *);
int	pgt_pci_detach(struct device *, int);

struct pgt_pci_softc {
	struct pgt_softc	sc_pgt;

	pci_chipset_tag_t       sc_pc;
	void 			*sc_ih;
	bus_size_t		sc_mapsize;
};

const struct cfattach pgt_pci_ca = {
	sizeof(struct pgt_pci_softc), pgt_pci_match, pgt_pci_attach,
	pgt_pci_detach, pgt_activate
};

const struct pci_matchid pgt_pci_devices[] = {
	{ PCI_VENDOR_INTERSIL, PCI_PRODUCT_INTERSIL_ISL3877 },
	{ PCI_VENDOR_INTERSIL, PCI_PRODUCT_INTERSIL_ISL3890 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CRWE154G72 }
};

int
pgt_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, pgt_pci_devices,
	    sizeof(pgt_pci_devices) / sizeof(pgt_pci_devices[0])));
}

void
pgt_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pgt_pci_softc *psc = (struct pgt_pci_softc *)self;
	struct pgt_softc *sc = &psc->sc_pgt;
	struct pci_attach_args *pa = aux;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;
	int error;

	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pa->pa_pc;

	/* remember chipset */
	if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTERSIL_ISL3877)
		sc->sc_flags |= SC_ISL3877;

	/* map control / status registers */
	error = pci_mapreg_map(pa, PGT_PCI_BAR0,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->sc_iotag, &sc->sc_iohandle, NULL, &psc->sc_mapsize, 0);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}

	/* map interrupt */
	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	/* disable all interrupts */
	bus_space_write_4(sc->sc_iotag, sc->sc_iohandle, PGT_REG_INT_EN, 0);
	(void)bus_space_read_4(sc->sc_iotag, sc->sc_iohandle, PGT_REG_INT_EN);
	DELAY(PGT_WRITEIO_DELAY);

	/* establish interrupt */
	intrstr = pci_intr_string(psc->sc_pc, ih);
	psc->sc_ih = pci_intr_establish(psc->sc_pc, ih, IPL_NET, pgt_intr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	config_mountroot(self, pgt_attach);
}

int
pgt_pci_detach(struct device *self, int flags)
{
	struct pgt_pci_softc *psc = (struct pgt_pci_softc *)self;
	struct pgt_softc *sc = &psc->sc_pgt;

	if (psc->sc_ih != NULL) {
		pgt_detach(sc);
		pci_intr_disestablish(psc->sc_pc, psc->sc_ih);
	}
	if (psc->sc_mapsize > 0)
		bus_space_unmap(sc->sc_iotag, sc->sc_iohandle,
		    psc->sc_mapsize);

	return (0);
}
