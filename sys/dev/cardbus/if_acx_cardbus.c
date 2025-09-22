/*	$OpenBSD: if_acx_cardbus.c,v 1.24 2024/05/24 06:26:47 jsg Exp $  */

/*
 * Copyright (c) 2006 Claudio Jeker <claudio@openbsd.org>
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
 * CardBus front-end for the Texas Instruments ACX driver
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

#include <dev/ic/acxvar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

struct acx_cardbus_softc {
	struct acx_softc	sc_acx;

	/* cardbus specific goo */
	cardbus_devfunc_t	sc_ct;
	pcitag_t		sc_tag;
	void			*sc_ih;
	bus_size_t		sc_mapsize1;
	bus_size_t		sc_mapsize2;
	pcireg_t		sc_iobar_val; /* acx100 only */
	pcireg_t		sc_bar1_val;
	pcireg_t		sc_bar2_val;
	int			sc_intrline;

	/* hack for ACX100A */
	bus_space_tag_t		sc_io_bt;
	bus_space_handle_t	sc_io_bh;
	bus_size_t		sc_iomapsize;

	int			sc_acx_attached;
	pci_chipset_tag_t	sc_pc;
};

int	acx_cardbus_match(struct device *, void *, void *);
void	acx_cardbus_attach(struct device *, struct device *, void *);
int	acx_cardbus_detach(struct device *, int);

const struct cfattach acx_cardbus_ca = {
	sizeof (struct acx_cardbus_softc), acx_cardbus_match,
	acx_cardbus_attach, acx_cardbus_detach
};

static const struct pci_matchid acx_cardbus_devices[] = {
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX100A },
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX100B },
	{ PCI_VENDOR_TI, PCI_PRODUCT_TI_ACX111 },
};

int	acx_cardbus_enable(struct acx_softc *);
void	acx_cardbus_disable(struct acx_softc *);
void	acx_cardbus_power(struct acx_softc *, int);
void	acx_cardbus_setup(struct acx_cardbus_softc *);

int
acx_cardbus_match(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid((struct cardbus_attach_args *)aux,
	    acx_cardbus_devices,
	    sizeof (acx_cardbus_devices) / sizeof (acx_cardbus_devices[0])));
}

void
acx_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct acx_cardbus_softc *csc = (struct acx_cardbus_softc *)self;
	struct acx_softc *sc = &csc->sc_acx;
	struct cardbus_attach_args *ca = aux;
	cardbus_devfunc_t ct = ca->ca_ct;
	bus_addr_t base;
	int error, b1 = CARDBUS_BASE0_REG, b2 = CARDBUS_BASE1_REG;

	sc->sc_dmat = ca->ca_dmat;
	csc->sc_ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_intrline = ca->ca_intrline;
	csc->sc_pc = ca->ca_pc;

	/* power management hooks */
	sc->sc_enable = acx_cardbus_enable;
	sc->sc_disable = acx_cardbus_disable;
	sc->sc_power = acx_cardbus_power;

	if (PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_TI_ACX100A) {
		/* first map I/O space as seen in the dragonfly code */
		error = Cardbus_mapreg_map(ct, CARDBUS_BASE0_REG,
		    PCI_MAPREG_TYPE_IO, 0, &csc->sc_io_bt, &csc->sc_io_bh,
		    &base, &csc->sc_iomapsize);
		if (error != 0) {
			printf(": can't map i/o space\n");
			return;
		}
		csc->sc_iobar_val = base | PCI_MAPREG_TYPE_IO;
		b1 = CARDBUS_BASE1_REG;
		b2 = CARDBUS_BASE2_REG;
	}

	/* map control/status registers */
	error = Cardbus_mapreg_map(ct, b1, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_mem1_bt, &sc->sc_mem1_bh, &base, &csc->sc_mapsize1);
	if (error != 0) {
		printf(": can't map mem1 space\n");
		return;
	}

	csc->sc_bar1_val = base | PCI_MAPREG_TYPE_MEM;

	/* map the other memory region */
	error = Cardbus_mapreg_map(ct, b2, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_mem2_bt, &sc->sc_mem2_bh, &base, &csc->sc_mapsize2);
	if (error != 0) {
		printf(": can't map mem2 space\n");
		return;
	}

	csc->sc_bar2_val = base | PCI_MAPREG_TYPE_MEM;

	/* set up the PCI configuration registers */
	acx_cardbus_setup(csc);

	printf(": irq %d\n", csc->sc_intrline);

	if (PCI_PRODUCT(ca->ca_id) == PCI_PRODUCT_TI_ACX111)
		acx111_set_param(sc);
	else
		acx100_set_param(sc);

	error = acx_attach(sc);
	csc->sc_acx_attached = error == 0;

	Cardbus_function_disable(ct);
}

int
acx_cardbus_detach(struct device *self, int flags)
{
	struct acx_cardbus_softc *csc = (struct acx_cardbus_softc *)self;
	struct acx_softc *sc = &csc->sc_acx;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;
	int error, b1 = CARDBUS_BASE0_REG, b2 = CARDBUS_BASE1_REG;

	if (csc->sc_acx_attached) {
		error = acx_detach(sc);
		if (error != 0)
			return (error);
	}

	/* unhook the interrupt handler */
	if (csc->sc_ih != NULL) {
		cardbus_intr_disestablish(cc, cf, csc->sc_ih);
		csc->sc_ih = NULL;
	}

	/* release bus space and close window */
	if (csc->sc_iomapsize) {
		b1 = CARDBUS_BASE1_REG;
		b2 = CARDBUS_BASE2_REG;
	}
	Cardbus_mapreg_unmap(ct, b1, sc->sc_mem1_bt,
	    sc->sc_mem1_bh, csc->sc_mapsize1);
	Cardbus_mapreg_unmap(ct, b2, sc->sc_mem2_bt,
	    sc->sc_mem2_bh, csc->sc_mapsize2);
	if (csc->sc_iomapsize)
		Cardbus_mapreg_unmap(ct, CARDBUS_BASE0_REG, csc->sc_io_bt,
		    csc->sc_io_bh, csc->sc_iomapsize);

	return (0);
}

int
acx_cardbus_enable(struct acx_softc *sc)
{
	struct acx_cardbus_softc *csc;
	int error;

	csc = (struct acx_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* power on the socket */
	error = Cardbus_function_enable(ct);
	if (error)
		return error;

	/* setup the PCI configuration registers */
	acx_cardbus_setup(csc);

	/* map and establish the interrupt handler */
	csc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline, IPL_NET,
	    acx_intr, sc, sc->sc_dev.dv_xname);
	if (csc->sc_ih == NULL) {
		printf("%s: could not establish interrupt at %d\n",
		    sc->sc_dev.dv_xname, csc->sc_intrline);
		Cardbus_function_disable(ct);
		return (1);
	}

	return (0);
}

void
acx_cardbus_disable(struct acx_softc *sc)
{
	struct acx_cardbus_softc *csc = (struct acx_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* unhook the interrupt handler */
	cardbus_intr_disestablish(cc, cf, csc->sc_ih);
	csc->sc_ih = NULL;

	/* power down the socket */
	Cardbus_function_disable(ct);
}

void
acx_cardbus_power(struct acx_softc *sc, int why)
{
	struct acx_cardbus_softc *csc = (struct acx_cardbus_softc *)sc;

	if (why == DVACT_RESUME) {
		/* kick the PCI configuration registers */
		acx_cardbus_setup(csc);
	}
}

void
acx_cardbus_setup(struct acx_cardbus_softc *csc)
{
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = csc->sc_pc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t reg;
	int b1 = CARDBUS_BASE0_REG, b2 = CARDBUS_BASE1_REG;

	if (csc->sc_iobar_val) {
		pci_conf_write(pc, csc->sc_tag, CARDBUS_BASE0_REG,
		    csc->sc_iobar_val);
		b1 = CARDBUS_BASE1_REG;
		b2 = CARDBUS_BASE2_REG;
		/* (*cf->cardbus_ctrl)(cc, CARDBUS_IO_ENABLE); */
	}

	/* program the BAR */
	pci_conf_write(pc, csc->sc_tag, b1, csc->sc_bar1_val);
	pci_conf_write(pc, csc->sc_tag, b2, csc->sc_bar2_val);

	/* make sure the right access type is on the cardbus bridge */
	(*cf->cardbus_ctrl)(cc, CARDBUS_MEM_ENABLE);
	(*cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* enable the appropriate bits in the PCI CSR */
	reg = pci_conf_read(pc, csc->sc_tag,
	    PCI_COMMAND_STATUS_REG);
	reg |= PCI_COMMAND_MASTER_ENABLE | PCI_COMMAND_MEM_ENABLE;
#if 0
	if (csc->sc_iobar_val)
		reg |= PCI_COMMAND_IO_ENABLE;
#endif
	pci_conf_write(pc, csc->sc_tag, PCI_COMMAND_STATUS_REG,
	    reg);
}
