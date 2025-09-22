/*	$OpenBSD: if_bwi_cardbus.c,v 1.17 2024/05/24 06:26:47 jsg Exp $ */

/*
 * Copyright (c) 2007 Marcus Glocker <mglocker@openbsd.org>
 * Copyright (c) 2006 Claudio Jeker <claudio@openbsd.org>
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
 * Cardbus front-end for the Broadcom AirForce
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/ic/bwivar.h>

struct bwi_cardbus_softc {
	struct bwi_softc	 csc_bwi;

	/* cardbus specific goo */
	cardbus_devfunc_t	 csc_ct;
	pcitag_t		 csc_tag;
	void			*csc_ih;

	bus_size_t		 csc_mapsize;
	pcireg_t		 csc_bar_val;
	int			 csc_intrline;
	pci_chipset_tag_t	 csc_pc;
};

int		bwi_cardbus_match(struct device *, void *, void*);
void		bwi_cardbus_attach(struct device *, struct device *, void *);
int		bwi_cardbus_detach(struct device *, int);
void		bwi_cardbus_setup(struct bwi_cardbus_softc *);
int		bwi_cardbus_enable(struct bwi_softc *);
void		bwi_cardbus_disable(struct bwi_softc *);
void		bwi_cardbus_conf_write(void *, uint32_t, uint32_t);
uint32_t	bwi_cardbus_conf_read(void *, uint32_t);

const struct cfattach bwi_cardbus_ca = {
	sizeof (struct bwi_cardbus_softc), bwi_cardbus_match,
	bwi_cardbus_attach, bwi_cardbus_detach
};

static const struct pci_matchid bwi_cardbus_devices[] = {
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4303 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4306_2 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4307 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4309 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4318 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM4319 },
	{ PCI_VENDOR_BROADCOM, PCI_PRODUCT_BROADCOM_BCM43XG }
};

int
bwi_cardbus_match(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid(aux, bwi_cardbus_devices,
	    sizeof (bwi_cardbus_devices) / sizeof (bwi_cardbus_devices[0])));
}

void
bwi_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct bwi_cardbus_softc *csc = (struct bwi_cardbus_softc *)self;
	struct cardbus_attach_args *ca = aux;
	struct bwi_softc *sc = &csc->csc_bwi;
	cardbus_devfunc_t ct = ca->ca_ct;
	pcireg_t reg;
	bus_addr_t base;
	int error;

	sc->sc_dmat = ca->ca_dmat;
	csc->csc_ct = ct;
	csc->csc_tag = ca->ca_tag;
	csc->csc_intrline = ca->ca_intrline;
	csc->csc_pc = ca->ca_pc;

	/* power management hooks */
	sc->sc_enable = bwi_cardbus_enable;
	sc->sc_disable = bwi_cardbus_disable;
	//sc->sc_power = bwi_cardbus_power;

	/* map control/status registers */
	error = Cardbus_mapreg_map(ct, CARDBUS_BASE0_REG,
	    PCI_MAPREG_TYPE_MEM, 0, &sc->sc_mem_bt,
	    &sc->sc_mem_bh, &base, &csc->csc_mapsize);
	if (error != 0) {
		printf(": can't map mem space\n");
		return;
	}
	csc->csc_bar_val = base | PCI_MAPREG_TYPE_MEM;

	/* set up the PCI configuration registers */
	bwi_cardbus_setup(csc);

	printf(": irq %d", csc->csc_intrline);

	/* we need to access Cardbus config space from the driver */
	sc->sc_conf_read = bwi_cardbus_conf_read;
	sc->sc_conf_write = bwi_cardbus_conf_write;

	reg = (sc->sc_conf_read)(sc, PCI_SUBSYS_ID_REG);

	sc->sc_pci_revid = PCI_REVISION(ca->ca_class);
	sc->sc_pci_did = PCI_PRODUCT(ca->ca_id);
	sc->sc_pci_subvid = PCI_VENDOR(reg);
	sc->sc_pci_subdid = PCI_PRODUCT(reg);

	error = bwi_attach(sc);
	if (error != 0)
		bwi_cardbus_detach(&sc->sc_dev, 0);

	Cardbus_function_disable(ct);
}

int
bwi_cardbus_detach(struct device *self, int flags)
{
	struct bwi_cardbus_softc *csc = (struct bwi_cardbus_softc *)self;
	struct bwi_softc *sc = &csc->csc_bwi;
	cardbus_devfunc_t ct = csc->csc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;
	int error;

	error = bwi_detach(sc);
	if (error != 0)
		return (error);

	/* unhook the interrupt handler */
	if (csc->csc_ih != NULL) {
		cardbus_intr_disestablish(cc, cf, csc->csc_ih);
		csc->csc_ih = NULL;
	}

	/* release bus space and close window */
	Cardbus_mapreg_unmap(ct, CARDBUS_BASE0_REG, sc->sc_mem_bt,
	    sc->sc_mem_bh, csc->csc_mapsize);

	return (0);
}

void
bwi_cardbus_setup(struct bwi_cardbus_softc *csc)
{
	cardbus_devfunc_t ct = csc->csc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = csc->csc_pc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t reg;

	/* program the BAR */
	pci_conf_write(pc, csc->csc_tag, CARDBUS_BASE0_REG,
	    csc->csc_bar_val);

	/* make sure the right access type is on the cardbus bridge */
	(*cf->cardbus_ctrl)(cc, CARDBUS_MEM_ENABLE);
	(*cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* enable the appropriate bits in the PCI CSR */
	reg = pci_conf_read(pc, csc->csc_tag,
	    PCI_COMMAND_STATUS_REG);
	reg |= PCI_COMMAND_MASTER_ENABLE | PCI_COMMAND_MEM_ENABLE;
	pci_conf_write(pc, csc->csc_tag, PCI_COMMAND_STATUS_REG,
	    reg);
}

int
bwi_cardbus_enable(struct bwi_softc *sc)
{
	struct bwi_cardbus_softc *csc = (struct bwi_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->csc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* power on the socket */
	Cardbus_function_enable(ct);

	/* setup the PCI configuration registers */
	bwi_cardbus_setup(csc);

	/* map and establish the interrupt handler */
	csc->csc_ih = cardbus_intr_establish(cc, cf, csc->csc_intrline, IPL_NET,
	    bwi_intr, sc, sc->sc_dev.dv_xname);
	if (csc->csc_ih == NULL) {
		printf("%s: could not establish interrupt at %d\n",
		    sc->sc_dev.dv_xname, csc->csc_intrline);
		Cardbus_function_disable(ct);
		return (1);
	}

	return (0);
}

void
bwi_cardbus_disable(struct bwi_softc *sc)
{
	struct bwi_cardbus_softc *csc = (struct bwi_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->csc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* unhook the interrupt handler */
	cardbus_intr_disestablish(cc, cf, csc->csc_ih);
	csc->csc_ih = NULL;

	/* power down the socket */
	Cardbus_function_disable(ct);
}

void
bwi_cardbus_conf_write(void *self, uint32_t reg, uint32_t val)
{
	struct bwi_cardbus_softc *csc = (struct bwi_cardbus_softc *)self;
	pci_chipset_tag_t pc = csc->csc_pc;

	pci_conf_write(pc, csc->csc_tag, reg, val);
}

uint32_t
bwi_cardbus_conf_read(void *self, uint32_t reg)
{
	struct bwi_cardbus_softc *csc = (struct bwi_cardbus_softc *)self;
	pci_chipset_tag_t pc = csc->csc_pc;

	return (pci_conf_read(pc, csc->csc_tag, reg));
}
