/*	$OpenBSD: if_malo_cardbus.c,v 1.14 2024/05/24 06:26:47 jsg Exp $ */

/*
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

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/ic/malo.h>

struct malo_cardbus_softc {
	struct malo_softc	sc_malo;

	/* cardbus specific goo */
	cardbus_devfunc_t	sc_ct;
	pcitag_t		sc_tag;
	void			*sc_ih;

	bus_size_t		sc_mapsize1;
	bus_size_t		sc_mapsize2;
	pcireg_t		sc_bar1_val;
	pcireg_t		sc_bar2_val;
	int			sc_intrline;
	pci_chipset_tag_t	sc_pc;
};

int	malo_cardbus_match(struct device *parent, void *match, void *aux);
void	malo_cardbus_attach(struct device *parent, struct device *self,
	    void *aux);
int	malo_cardbus_detach(struct device *self, int flags);
void	malo_cardbus_setup(struct malo_cardbus_softc *csc);
int	malo_cardbus_enable(struct malo_softc *sc);
void	malo_cardbus_disable(struct malo_softc *sc);

const struct cfattach malo_cardbus_ca = {
	sizeof (struct malo_cardbus_softc), malo_cardbus_match,
	malo_cardbus_attach, malo_cardbus_detach
};

static const struct pci_matchid malo_cardbus_devices[] = {
	{ PCI_VENDOR_MARVELL, PCI_PRODUCT_MARVELL_88W8310 },
	{ PCI_VENDOR_MARVELL, PCI_PRODUCT_MARVELL_88W8335_1 },
	{ PCI_VENDOR_MARVELL, PCI_PRODUCT_MARVELL_88W8335_2 }
};

int
malo_cardbus_match(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid(aux, malo_cardbus_devices,
	    sizeof (malo_cardbus_devices) / sizeof (malo_cardbus_devices[0])));
}

void
malo_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct malo_cardbus_softc *csc = (struct malo_cardbus_softc *)self;
	struct cardbus_attach_args *ca = aux;
	struct malo_softc *sc = &csc->sc_malo;
	cardbus_devfunc_t ct = ca->ca_ct;
	bus_addr_t base;
	int error;

	sc->sc_dmat = ca->ca_dmat;
	csc->sc_ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_intrline = ca->ca_intrline;
	csc->sc_pc = ca->ca_pc;

	/* power management hooks */
	sc->sc_enable = malo_cardbus_enable;
	sc->sc_disable = malo_cardbus_disable;
#if 0
	sc->sc_power = malo_cardbus_power;
#endif

	/* map control/status registers */
	error = Cardbus_mapreg_map(ct, CARDBUS_BASE0_REG,
	    PCI_MAPREG_TYPE_MEM, 0, &sc->sc_mem1_bt,
	    &sc->sc_mem1_bh, &base, &csc->sc_mapsize1);
	if (error != 0) {
		printf(": can't map mem1 space\n");
		return;
	}
	csc->sc_bar1_val = base | PCI_MAPREG_TYPE_MEM;

	/* map control/status registers */
	error = Cardbus_mapreg_map(ct, CARDBUS_BASE1_REG,
	    PCI_MAPREG_TYPE_MEM, 0, &sc->sc_mem2_bt,
	    &sc->sc_mem2_bh, &base, &csc->sc_mapsize2);
	if (error != 0) {
		printf(": can't map mem2 space\n");
		Cardbus_mapreg_unmap(ct, CARDBUS_BASE0_REG, sc->sc_mem1_bt,
		    sc->sc_mem1_bh, csc->sc_mapsize1);
		return;
	}
	csc->sc_bar2_val = base | PCI_MAPREG_TYPE_MEM;

	/* set up the PCI configuration registers */
	malo_cardbus_setup(csc);

	printf(": irq %d", csc->sc_intrline);

	error = malo_attach(sc);
	if (error != 0)
		malo_cardbus_detach(&sc->sc_dev, 0);

	Cardbus_function_disable(ct);
}

int
malo_cardbus_detach(struct device *self, int flags)
{
	struct malo_cardbus_softc *csc = (struct malo_cardbus_softc *)self;
	struct malo_softc *sc = &csc->sc_malo;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;
	int error;

	error = malo_detach(sc);
	if (error != 0)
		return (error);

	/* unhook the interrupt handler */
	if (csc->sc_ih != NULL) {
		cardbus_intr_disestablish(cc, cf, csc->sc_ih);
		csc->sc_ih = NULL;
	}

	/* release bus space and close window */
	Cardbus_mapreg_unmap(ct, CARDBUS_BASE0_REG, sc->sc_mem1_bt,
	    sc->sc_mem1_bh, csc->sc_mapsize1);
	Cardbus_mapreg_unmap(ct, CARDBUS_BASE1_REG, sc->sc_mem2_bt,
	    sc->sc_mem2_bh, csc->sc_mapsize2);

	return (0);
}

void
malo_cardbus_setup(struct malo_cardbus_softc *csc)
{
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = csc->sc_pc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t reg;

	/* program the BAR */
	pci_conf_write(pc, csc->sc_tag, CARDBUS_BASE0_REG,
	    csc->sc_bar1_val);
	pci_conf_write(pc, csc->sc_tag, CARDBUS_BASE1_REG,
	    csc->sc_bar2_val);

	/* make sure the right access type is on the cardbus bridge */
	(*cf->cardbus_ctrl)(cc, CARDBUS_MEM_ENABLE);
	(*cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* enable the appropriate bits in the PCI CSR */
	reg = pci_conf_read(pc, csc->sc_tag,
	    PCI_COMMAND_STATUS_REG);
	reg |= PCI_COMMAND_MASTER_ENABLE | PCI_COMMAND_MEM_ENABLE;
	pci_conf_write(pc, csc->sc_tag, PCI_COMMAND_STATUS_REG,
	    reg);
}

int
malo_cardbus_enable(struct malo_softc *sc)
{
	struct malo_cardbus_softc *csc = (struct malo_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* power on the socket */
	Cardbus_function_enable(ct);

	/* setup the PCI configuration registers */
	malo_cardbus_setup(csc);

	/* map and establish the interrupt handler */
	csc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline, IPL_NET,
	    malo_intr, sc, sc->sc_dev.dv_xname);
	if (csc->sc_ih == NULL) {
		printf("%s: could not establish interrupt at %d\n",
		    sc->sc_dev.dv_xname, csc->sc_intrline);
		Cardbus_function_disable(ct);
		return (1);
	}

	return (0);
}

void
malo_cardbus_disable(struct malo_softc *sc)
{
	struct malo_cardbus_softc *csc = (struct malo_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* unhook the interrupt handler */
	cardbus_intr_disestablish(cc, cf, csc->sc_ih);
	csc->sc_ih = NULL;

	/* power down the socket */
	Cardbus_function_disable(ct);
}
