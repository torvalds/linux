/*	$OpenBSD: if_bwi_pci.c,v 1.19 2024/05/24 06:02:53 jsg Exp $ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
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
 * PCI front-end for the Broadcom AirForce
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
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/ic/bwivar.h>
#include <dev/ic/bwireg.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/* Base Address Register */
#define BWI_PCI_BAR0	0x10

int		bwi_pci_match(struct device *, void *, void *);
void		bwi_pci_attach(struct device *, struct device *, void *);
int		bwi_pci_detach(struct device *, int);
void		bwi_pci_conf_write(void *, uint32_t, uint32_t);
uint32_t	bwi_pci_conf_read(void *, uint32_t);
int		bwi_pci_activate(struct device *, int);
void		bwi_pci_wakeup(struct bwi_softc *);

struct bwi_pci_softc {
	struct bwi_softc	 psc_bwi;

	pci_chipset_tag_t        psc_pc;
	pcitag_t		 psc_pcitag;
	void 			*psc_ih;

	bus_size_t		 psc_mapsize;
};

const struct cfattach bwi_pci_ca = {
	sizeof(struct bwi_pci_softc), bwi_pci_match, bwi_pci_attach,
	bwi_pci_detach, bwi_pci_activate
};

const struct pci_matchid bwi_pci_devices[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4303 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306_2 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4307 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4309 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4311 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4312 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4318 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4319 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM43XG },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4331 },
};

int
bwi_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	/*
	 * The second revision of the BCM4311/BCM4312
	 * chips require v4 firmware.
	 */
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BROADCOM &&
            (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROADCOM_BCM4311 ||
	     PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROADCOM_BCM4312) &&
	     PCI_REVISION(pa->pa_class) == 0x02)
		return (0);

	return (pci_matchbyid((struct pci_attach_args *)aux, bwi_pci_devices,
	    sizeof(bwi_pci_devices) / sizeof(bwi_pci_devices[0])));
}

void
bwi_reset_bcm4331(struct bwi_softc *sc)
{
	int i;

	/*
	 * The BCM4331 is not actually supported by this driver, but buggy EFI
	 * revisions in 2011-2012 Macs leave this chip enabled by default,
	 * causing it to emit spurious interrupts when the shared interrupt
	 * line is enabled.
	 */
	for (i = 0; CSR_READ_4(sc, BWI_RESET_STATUS) && i < 30; i++)
		delay(10);

	CSR_WRITE_4(sc, BWI_RESET_CTRL, BWI_RESET_CTRL_RESET);
	CSR_READ_4(sc, BWI_RESET_CTRL);
	delay(1);
	CSR_WRITE_4(sc, BWI_RESET_CTRL, 0);
	CSR_READ_4(sc, BWI_RESET_CTRL);
	delay(10);
}

void
bwi_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct bwi_pci_softc *psc = (struct bwi_pci_softc *)self;
	struct pci_attach_args *pa = aux;
	struct bwi_softc *sc = &psc->psc_bwi;
	const char *intrstr = NULL;
	pci_intr_handle_t ih;
	pcireg_t memtype, reg;

	sc->sc_dmat = pa->pa_dmat;
	psc->psc_pc = pa->pa_pc;
	psc->psc_pcitag = pa->pa_tag;

	/* map control / status registers */
	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, BWI_PCI_BAR0);
	if (pci_mapreg_map(pa, BWI_PCI_BAR0, memtype, 0, &sc->sc_mem_bt,
	    &sc->sc_mem_bh, NULL, &psc->psc_mapsize, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	/* we need to access PCI config space from the driver */
	sc->sc_conf_write = bwi_pci_conf_write;
	sc->sc_conf_read = bwi_pci_conf_read;

	reg = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

	sc->sc_pci_revid = PCI_REVISION(pa->pa_class);
	sc->sc_pci_did = PCI_PRODUCT(pa->pa_id);
	sc->sc_pci_subvid = PCI_VENDOR(reg);
	sc->sc_pci_subdid = PCI_PRODUCT(reg);

	if (sc->sc_pci_did == PCI_PRODUCT_BROADCOM_BCM4331) {
		printf(": disabling\n");
		bwi_reset_bcm4331(sc);
		bus_space_unmap(sc->sc_mem_bt, sc->sc_mem_bh, psc->psc_mapsize);
		return;
	}

	/* map interrupt */
	if (pci_intr_map(pa, &ih) != 0) {
		printf(": can't map interrupt\n");
		return;
	}

	/* establish interrupt */
	intrstr = pci_intr_string(psc->psc_pc, ih);
	psc->psc_ih = pci_intr_establish(psc->psc_pc, ih, IPL_NET, bwi_intr, sc,
	    sc->sc_dev.dv_xname);
	if (psc->psc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	bwi_attach(sc);
}

int
bwi_pci_detach(struct device *self, int flags)
{
	struct bwi_pci_softc *psc = (struct bwi_pci_softc *)self;
	struct bwi_softc *sc = &psc->psc_bwi;

	bwi_detach(sc);
	pci_intr_disestablish(psc->psc_pc, psc->psc_ih);

	return (0);
}

int
bwi_pci_activate(struct device *self, int act)
{
	struct bwi_pci_softc *psc = (struct bwi_pci_softc *)self;
	struct bwi_softc *sc = &psc->psc_bwi;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	switch (act) {
	case DVACT_SUSPEND:
		if (ifp->if_flags & IFF_RUNNING)
			bwi_stop(sc, 1);
		break;
	case DVACT_WAKEUP:
		bwi_pci_wakeup(sc);
		break;
	}

	return (0);
}

void
bwi_pci_wakeup(struct bwi_softc *sc)
{
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	if (ifp->if_flags & IFF_UP)
		bwi_init(ifp);
}

void
bwi_pci_conf_write(void *self, uint32_t reg, uint32_t val)
{
	struct bwi_pci_softc *psc = (struct bwi_pci_softc *)self;

	pci_conf_write(psc->psc_pc, psc->psc_pcitag, reg, val);
}

uint32_t
bwi_pci_conf_read(void *self, uint32_t reg)
{
	struct bwi_pci_softc *psc = (struct bwi_pci_softc *)self;

	return (pci_conf_read(psc->psc_pc, psc->psc_pcitag, reg));
}
