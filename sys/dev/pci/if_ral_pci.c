/*	$OpenBSD: if_ral_pci.c,v 1.29 2024/05/24 06:02:56 jsg Exp $  */

/*-
 * Copyright (c) 2005-2010 Damien Bergamini <damien.bergamini@free.fr>
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
 * PCI front-end for the Ralink RT2560/RT2561/RT2860/RT3090 driver.
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

#include <dev/ic/rt2560var.h>
#include <dev/ic/rt2661var.h>
#include <dev/ic/rt2860var.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

static struct ral_opns {
	int	(*attach)(void *, int);
	int	(*detach)(void *);
	void	(*suspend)(void *);
	void	(*wakeup)(void *);
	int	(*intr)(void *);

}  ral_rt2560_opns = {
	rt2560_attach,
	rt2560_detach,
	rt2560_suspend,
	rt2560_wakeup,
	rt2560_intr

}, ral_rt2661_opns = {
	rt2661_attach,
	rt2661_detach,
	rt2661_suspend,
	rt2661_wakeup,
	rt2661_intr

}, ral_rt2860_opns = {
	rt2860_attach,
	rt2860_detach,
	rt2860_suspend,
	rt2860_wakeup,
	rt2860_intr
};

struct ral_pci_softc {
	union {
		struct rt2560_softc	sc_rt2560;
		struct rt2661_softc	sc_rt2661;
		struct rt2860_softc	sc_rt2860;
	} u;
#define sc_sc	u.sc_rt2560

	/* PCI specific goo */
	struct ral_opns		*sc_opns;
	pci_chipset_tag_t	sc_pc;
	void			*sc_ih;
	bus_size_t		sc_mapsize;
};

/* Base Address Register */
#define RAL_PCI_BAR0	0x10

int	ral_pci_match(struct device *, void *, void *);
void	ral_pci_attach(struct device *, struct device *, void *);
int	ral_pci_detach(struct device *, int);
int	ral_pci_activate(struct device *, int);
void	ral_pci_wakeup(struct ral_pci_softc *);

const struct cfattach ral_pci_ca = {
	sizeof (struct ral_pci_softc), ral_pci_match, ral_pci_attach,
	ral_pci_detach, ral_pci_activate
};

const struct pci_matchid ral_pci_devices[] = {
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2560 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2561 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2561S },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2661 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2860 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2890 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2760 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT2790 },
	{ PCI_VENDOR_AWT,    PCI_PRODUCT_AWT_RT2890 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_1 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_2 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_3 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_4 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_5 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_6 },
	{ PCI_VENDOR_EDIMAX, PCI_PRODUCT_EDIMAX_RT2860_7 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3060 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3062 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3090 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3091 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3092 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3290 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3562 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3592 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT3593 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT5360 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT5390 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT5392 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT5390_1 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT5390_2 },
	{ PCI_VENDOR_RALINK, PCI_PRODUCT_RALINK_RT5390_3 }
};

int
ral_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, ral_pci_devices,
	    nitems(ral_pci_devices)));
}

void
ral_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct ral_pci_softc *psc = (struct ral_pci_softc *)self;
	struct rt2560_softc *sc = &psc->sc_sc;
	struct pci_attach_args *pa = aux;
	const char *intrstr;
	pci_intr_handle_t ih;
	pcireg_t memtype;
	int error;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_RALINK) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_RALINK_RT2560:
			psc->sc_opns = &ral_rt2560_opns;
			break;
		case PCI_PRODUCT_RALINK_RT2561:
		case PCI_PRODUCT_RALINK_RT2561S:
		case PCI_PRODUCT_RALINK_RT2661:
			psc->sc_opns = &ral_rt2661_opns;
			break;
		default:
			psc->sc_opns = &ral_rt2860_opns;
			break;
		}
	} else {
		/* all other vendors are RT2860 only */
		psc->sc_opns = &ral_rt2860_opns;
	}
	sc->sc_dmat = pa->pa_dmat;
	psc->sc_pc = pa->pa_pc;

	/* map control/status registers */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, RAL_PCI_BAR0);
	error = pci_mapreg_map(pa, RAL_PCI_BAR0, memtype, 0, &sc->sc_st,
	    &sc->sc_sh, NULL, &psc->sc_mapsize, 0);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(psc->sc_pc, ih);
	psc->sc_ih = pci_intr_establish(psc->sc_pc, ih, IPL_NET,
	    psc->sc_opns->intr, sc, sc->sc_dev.dv_xname);
	if (psc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	(*psc->sc_opns->attach)(sc, PCI_PRODUCT(pa->pa_id));
}

int
ral_pci_detach(struct device *self, int flags)
{
	struct ral_pci_softc *psc = (struct ral_pci_softc *)self;
	struct rt2560_softc *sc = &psc->sc_sc;
	int error;

	if (psc->sc_ih != NULL) {
		pci_intr_disestablish(psc->sc_pc, psc->sc_ih);

		error = (*psc->sc_opns->detach)(sc);
		if (error != 0)
			return error;
	}

	if (psc->sc_mapsize > 0)
		bus_space_unmap(sc->sc_st, sc->sc_sh, psc->sc_mapsize);

	return 0;
}

int
ral_pci_activate(struct device *self, int act)
{
	struct ral_pci_softc *psc = (struct ral_pci_softc *)self;
	struct rt2560_softc *sc = &psc->sc_sc;

	switch (act) {
	case DVACT_SUSPEND:
		(*psc->sc_opns->suspend)(sc);
		break;
	case DVACT_WAKEUP:
		ral_pci_wakeup(psc);
		break;
	}
	return 0;
}

void
ral_pci_wakeup(struct ral_pci_softc *psc)
{
	struct rt2560_softc *sc = &psc->sc_sc;
	int s;

	s = splnet();
	(*psc->sc_opns->wakeup)(sc);
	splx(s);
}
