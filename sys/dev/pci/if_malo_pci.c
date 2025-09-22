/*	$OpenBSD: if_malo_pci.c,v 1.12 2024/05/24 06:02:53 jsg Exp $ */

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
 * PCI front-end for the Marvell Libertas
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

#include <dev/ic/malo.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/* Base Address Register */
#define MALO_PCI_BAR1	0x10
#define MALO_PCI_BAR2	0x14

int	malo_pci_match(struct device *, void *, void *);
void	malo_pci_attach(struct device *, struct device *, void *);
int	malo_pci_detach(struct device *, int);
int	malo_pci_activate(struct device *, int);
void	malo_pci_wakeup(struct malo_softc *);

struct malo_pci_softc {
	struct malo_softc	sc_malo;

	pci_chipset_tag_t        sc_pc;
	void 			*sc_ih;

	bus_size_t		 sc_mapsize1;
	bus_size_t		 sc_mapsize2;
};

const struct cfattach malo_pci_ca = {
	sizeof(struct malo_pci_softc), malo_pci_match, malo_pci_attach,
	malo_pci_detach, malo_pci_activate
};

const struct pci_matchid malo_pci_devices[] = {
	{ PCI_VENDOR_MARVELL, PCI_PRODUCT_MARVELL_88W8310 },
	{ PCI_VENDOR_MARVELL, PCI_PRODUCT_MARVELL_88W8335_1 },
	{ PCI_VENDOR_MARVELL, PCI_PRODUCT_MARVELL_88W8335_2 }
};

int
malo_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, malo_pci_devices,
	    sizeof(malo_pci_devices) / sizeof(malo_pci_devices[0])));
}

void
malo_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct malo_pci_softc *psc = (struct malo_pci_softc *)self;
	struct pci_attach_args *pa = aux;
	struct malo_softc *sc = &psc->sc_malo;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;
	int error;

	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pa->pa_pc;

	/* map control / status registers */
	error = pci_mapreg_map(pa, MALO_PCI_BAR1,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->sc_mem1_bt, &sc->sc_mem1_bh, NULL, &psc->sc_mapsize1, 0);
	if (error != 0) {
		printf(": can't map 1st mem space\n");
		return;
	}

	/* map control / status registers */
	error = pci_mapreg_map(pa, MALO_PCI_BAR2,
	    PCI_MAPREG_TYPE_MEM | PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &sc->sc_mem2_bt, &sc->sc_mem2_bh, NULL, &psc->sc_mapsize2, 0);
	if (error != 0) {
		printf(": can't map 2nd mem space\n");
		return;
	}

	/* map interrupt */
	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	/* establish interrupt */
	intrstr = pci_intr_string(psc->sc_pc, ih);
	psc->sc_ih = pci_intr_establish(psc->sc_pc, ih, IPL_NET, malo_intr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	malo_attach(sc);
}

int
malo_pci_detach(struct device *self, int flags)
{
	struct malo_pci_softc *psc = (struct malo_pci_softc *)self;
	struct malo_softc *sc = &psc->sc_malo;

	malo_detach(sc);
	pci_intr_disestablish(psc->sc_pc, psc->sc_ih);

	return (0);
}

int
malo_pci_activate(struct device *self, int act)
{
	struct malo_pci_softc *psc = (struct malo_pci_softc *)self;
	struct malo_softc *sc = &psc->sc_malo;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			malo_stop(sc);
		break;
	case DVACT_WAKEUP:
		malo_pci_wakeup(sc);
		break;
	}
	return (0);
}

void
malo_pci_wakeup(struct malo_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_UP)
		malo_init(ifp);
}
