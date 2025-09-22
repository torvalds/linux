/* $OpenBSD: if_dwqe_pci.c,v 1.3 2023/11/11 16:50:25 stsp Exp $ */

/*
 * Copyright (c) 2023 Stefan Sperling <stsp@openbsd.org>
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
 * Driver for the Intel Elkhart Lake ethernet controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/miivar.h>

#include <dev/ic/dwqereg.h>
#include <dev/ic/dwqevar.h>

static const struct pci_matchid dwqe_pci_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_PSE0_RGMII_1G	},
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_PSE0_SGMII_1G },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_PSE0_SGMII_2G },
#if 0
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_PSE1_RGMII_1G },
#endif
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_PSE1_SGMII_1G },
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_EHL_PSE1_SGMII_2G },
};

struct dwqe_pci_softc {
	struct dwqe_softc	sc_sc;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;
	bus_size_t		sc_mapsize;
};

int
dwqe_pci_match(struct device *parent, void *cfdata, void *aux)
{
	struct pci_attach_args *pa = aux;
	return pci_matchbyid(pa, dwqe_pci_devices, nitems(dwqe_pci_devices));
}

void
dwqe_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct pci_attach_args *pa = aux;
	struct dwqe_pci_softc *psc = (void *)self;
	struct dwqe_softc *sc = &psc->sc_sc;
	pci_intr_handle_t ih;
	pcireg_t memtype;
	int err;
	const char *intrstr;

	psc->sc_pct = pa->pa_pc;
	psc->sc_pcitag = pa->pa_tag;

	sc->sc_dmat = pa->pa_dmat;

	memtype = pci_mapreg_type(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	err = pci_mapreg_map(pa, PCI_MAPREG_START, memtype, 0,
	    &sc->sc_iot, &sc->sc_ioh, NULL, &psc->sc_mapsize, 0);
	if (err) {
		printf("%s: can't map mem space\n", DEVNAME(sc));
		return;
	}

	if (pci_intr_map_msi(pa, &ih) && pci_intr_map(pa, &ih)) {
		printf("%s: can't map interrupt\n", DEVNAME(sc));
		return;
	}

	intrstr = pci_intr_string(psc->sc_pct, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_NET | IPL_MPSAFE,
	    dwqe_intr, psc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	switch (PCI_PRODUCT(pa->pa_id)) {
	case PCI_PRODUCT_INTEL_EHL_PSE0_RGMII_1G:
		sc->sc_phy_mode = DWQE_PHY_MODE_RGMII_ID;
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_250_300;
		sc->sc_clkrate = 200000000;
		break;
	case PCI_PRODUCT_INTEL_EHL_PSE0_SGMII_1G:
	case PCI_PRODUCT_INTEL_EHL_PSE0_SGMII_2G:
	case PCI_PRODUCT_INTEL_EHL_PSE1_SGMII_1G:
	case PCI_PRODUCT_INTEL_EHL_PSE1_SGMII_2G:
		sc->sc_phy_mode = DWQE_PHY_MODE_SGMII;
		sc->sc_clk = GMAC_MAC_MDIO_ADDR_CR_250_300;
		sc->sc_clkrate = 200000000;
		break;
	default:
		sc->sc_phy_mode = DWQE_PHY_MODE_UNKNOWN;
		break;
	}

	sc->sc_phyloc = MII_PHY_ANY;
	sc->sc_8xpbl = 1;
	sc->sc_txpbl = 32;
	sc->sc_rxpbl = 32;
	sc->sc_txfifo_size = 32768;
	sc->sc_rxfifo_size = 32768;

	sc->sc_axi_config = 1;
	sc->sc_wr_osr_lmt = 1;
	sc->sc_rd_osr_lmt = 1;
	sc->sc_blen[0] = 4;
	sc->sc_blen[1] = 8;
	sc->sc_blen[2] = 16;

	dwqe_lladdr_read(sc, sc->sc_lladdr);

	dwqe_reset(sc);
	dwqe_attach(sc);
}

const struct cfattach dwqe_pci_ca = {
	sizeof(struct dwqe_softc), dwqe_pci_match, dwqe_pci_attach
};
