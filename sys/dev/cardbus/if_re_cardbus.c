/*	$OpenBSD: if_re_cardbus.c,v 1.32 2024/05/24 06:26:47 jsg Exp $	*/

/*
 * Copyright (c) 2005 Peter Valchev <pvalchev@openbsd.org>
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
 * Cardbus front-end for the Realtek 8169
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/ic/rtl81x9reg.h>
#include <dev/ic/revar.h>

struct re_cardbus_softc {
	/* General */
	struct rl_softc sc_rl;

	/* Cardbus-specific data */
	cardbus_devfunc_t ct;
	pcitag_t sc_tag;
	pci_chipset_tag_t sc_pc;
	int sc_csr;
	int sc_cben;
	int sc_bar_reg;
	pcireg_t sc_bar_val;
	int sc_intrline;

	bus_size_t sc_mapsize;
};

int	re_cardbus_probe(struct device *, void *, void *);
void	re_cardbus_attach(struct device *, struct device *, void *);
int	re_cardbus_detach(struct device *, int);
void	re_cardbus_setup(struct rl_softc *);

/*
 * Cardbus autoconfig definitions
 */
const struct cfattach re_cardbus_ca = {
	sizeof(struct re_cardbus_softc),
	re_cardbus_probe,
	re_cardbus_attach,
	re_cardbus_detach
};

const struct pci_matchid re_cardbus_devices[] = {
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8169 },
};

/*
 * Probe for a Realtek 8169/8110 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
re_cardbus_probe(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid((struct cardbus_attach_args *)aux,
	    re_cardbus_devices, nitems(re_cardbus_devices)));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
re_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct re_cardbus_softc	*csc = (struct re_cardbus_softc *)self;
	struct rl_softc		*sc = &csc->sc_rl;
	struct cardbus_attach_args *ca = aux;
	struct cardbus_softc *psc =
	    (struct cardbus_softc *)sc->sc_dev.dv_parent;
	cardbus_chipset_tag_t cc = psc->sc_cc;
	cardbus_function_tag_t cf = psc->sc_cf;
	cardbus_devfunc_t ct = ca->ca_ct;
	bus_addr_t adr;
	char intrstr[16];

	sc->sc_dmat = ca->ca_dmat;
	csc->ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_pc = ca->ca_pc;
	csc->sc_intrline = ca->ca_intrline;

	/*
	 * Map control/status registers.
	 */
	if (Cardbus_mapreg_map(ct, RL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->rl_btag, &sc->rl_bhandle, &adr, &csc->sc_mapsize) == 0) {
		csc->sc_cben = CARDBUS_MEM_ENABLE;
		csc->sc_csr |= PCI_COMMAND_MEM_ENABLE;
		csc->sc_bar_reg = RL_PCI_LOMEM;
		csc->sc_bar_val = adr | PCI_MAPREG_TYPE_MEM;
	} else {
		printf(": can't map mem space\n");
		return;
	}

	/* Enable power */
	Cardbus_function_enable(ct);

	/* Get chip out of powersave mode (if applicable), initialize
	 * config registers */
	re_cardbus_setup(sc);

	/* Allocate interrupt */
	sc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline,
	    IPL_NET, re_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt at %d",
		    ca->ca_intrline);
		Cardbus_function_disable(csc->ct);
		return;
	}
	snprintf(intrstr, sizeof(intrstr), "irq %d", ca->ca_intrline);

	sc->sc_product = PCI_PRODUCT(ca->ca_id);

	/* Call bus-independent (common) attach routine */
	if (re_attach(sc, intrstr)) {
		cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, sc->sc_ih);
		Cardbus_mapreg_unmap(ct, csc->sc_bar_reg, sc->rl_btag,
		    sc->rl_bhandle, csc->sc_mapsize);
	}
}

/*
 * Get chip out of power-saving mode, init registers
 */
void
re_cardbus_setup(struct rl_softc *sc)
{
	struct re_cardbus_softc *csc = (struct re_cardbus_softc *)sc;
	cardbus_devfunc_t ct = csc->ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = csc->sc_pc;
	pcireg_t reg, command;
	int pmreg;

	/* Handle power management nonsense */
	if (pci_get_capability(pc, csc->sc_tag,
	    PCI_CAP_PWRMGMT, &pmreg, 0)) {
		command = pci_conf_read(pc, csc->sc_tag,
		    pmreg + PCI_PMCSR);

		if (command & RL_PSTATE_MASK) {
			pcireg_t iobase, membase, irq;

			/* Save important PCI config data */
			iobase = pci_conf_read(pc, csc->sc_tag, RL_PCI_LOIO);
			membase = pci_conf_read(pc, csc->sc_tag, RL_PCI_LOMEM);
			irq = pci_conf_read(pc, csc->sc_tag, RL_PCI_INTLINE);

			/* Reset the power state */
			printf("%s: chip is in D%d power mode "
			    "-- setting to D0\n", sc->sc_dev.dv_xname,
			    command & RL_PSTATE_MASK);
			command &= RL_PSTATE_MASK;
			pci_conf_write(pc, csc->sc_tag, pmreg + PCI_PMCSR,
			    command);

			/* Restore PCI config data */
			pci_conf_write(pc, csc->sc_tag, RL_PCI_LOIO, iobase);
			pci_conf_write(pc, csc->sc_tag, RL_PCI_LOMEM, membase);
			pci_conf_write(pc, csc->sc_tag, RL_PCI_INTLINE, irq);
		}
	}

	/* Make sure the right access type is on the Cardbus bridge */
	(*ct->ct_cf->cardbus_ctrl)(cc, csc->sc_cben);
	(*ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* Program the BAR */
	pci_conf_write(pc, csc->sc_tag, csc->sc_bar_reg, csc->sc_bar_val);

	/* Enable proper bits in CARDBUS CSR */
	reg = pci_conf_read(pc, csc->sc_tag, PCI_COMMAND_STATUS_REG);
	reg &= ~(PCI_COMMAND_IO_ENABLE|PCI_COMMAND_MEM_ENABLE);
	reg |= csc->sc_csr;
	pci_conf_write(pc, csc->sc_tag, PCI_COMMAND_STATUS_REG, reg);

	/* Make sure the latency timer is set to some reasonable value */
	reg = pci_conf_read(pc, csc->sc_tag, PCI_BHLC_REG);
	if (PCI_LATTIMER(reg) < 0x20) {
		reg &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
		reg |= (0x20 << PCI_LATTIMER_SHIFT);
		pci_conf_write(pc, csc->sc_tag, PCI_BHLC_REG, reg);
	}
}

/*
 * Cardbus detach function: deallocate all resources
 */
int
re_cardbus_detach(struct device *self, int flags)
{
	struct re_cardbus_softc *csc = (void *)self;
	struct rl_softc *sc = &csc->sc_rl;
	struct cardbus_devfunc *ct = csc->ct;

	re_detach(sc);

	/* Disable interrupts */
	if (sc->sc_ih != NULL)
		cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, sc->sc_ih);

	/* Free cardbus resources */
	Cardbus_mapreg_unmap(ct, csc->sc_bar_reg, sc->rl_btag, sc->rl_bhandle,
	    csc->sc_mapsize);

	return (0);
}
