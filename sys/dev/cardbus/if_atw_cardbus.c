/*	$OpenBSD: if_atw_cardbus.c,v 1.26 2024/05/24 06:26:47 jsg Exp $	*/
/*	$NetBSD: if_atw_cardbus.c,v 1.9 2004/07/23 07:07:55 dyoung Exp $	*/

/*-
 * Copyright (c) 1999, 2000, 2003 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center.  This code was adapted for the ADMtek ADM8211
 * by David Young.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * CardBus bus front-end for the ADMtek ADM8211 802.11 MAC/BBP driver.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h> 
#include <sys/device.h>
 
#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <net80211/ieee80211_radiotap.h>
#include <net80211/ieee80211_var.h>

#include <machine/bus.h>

#include <dev/ic/atwreg.h>
#include <dev/ic/atwvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

/*
 * PCI configuration space registers used by the ADM8211.
 */
#define	ATW_PCI_IOBA		0x10	/* i/o mapped base */
#define	ATW_PCI_MMBA		0x14	/* memory mapped base */

struct atw_cardbus_softc {
	struct atw_softc sc_atw;	/* real ADM8211 softc */

	/* CardBus-specific goo. */
	void	*sc_ih;			/* interrupt handle */
	cardbus_devfunc_t sc_ct;	/* our CardBus devfuncs */
	pcitag_t sc_tag;		/* our CardBus tag */
	int	sc_csr;			/* CSR bits */
	bus_size_t sc_mapsize;		/* the size of mapped bus space
					   region */

	int	sc_cben;		/* CardBus enables */
	int	sc_bar_reg;		/* which BAR to use */
	pcireg_t sc_bar_val;		/* value of the BAR */

	int	sc_intrline;		/* interrupt line */
	pci_chipset_tag_t sc_pc;
};

int	atw_cardbus_match(struct device *, void *, void *);
void	atw_cardbus_attach(struct device *, struct device *, void *);
int	atw_cardbus_detach(struct device *, int);

const struct cfattach atw_cardbus_ca = {
	sizeof(struct atw_cardbus_softc), atw_cardbus_match, atw_cardbus_attach,
	    atw_cardbus_detach
};

void	atw_cardbus_setup(struct atw_cardbus_softc *);

int	atw_cardbus_enable(struct atw_softc *);
void	atw_cardbus_disable(struct atw_softc *);
void	atw_cardbus_power(struct atw_softc *, int);

const struct pci_matchid atw_cardbus_devices[] = {
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_ADM8211 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CRSHPW796 },
};

int
atw_cardbus_match(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid((struct cardbus_attach_args *)aux,
	    atw_cardbus_devices, nitems(atw_cardbus_devices)));
}

void
atw_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct atw_cardbus_softc *csc = (void *)self;
	struct atw_softc *sc = &csc->sc_atw;
	struct cardbus_attach_args *ca = aux;
	cardbus_devfunc_t ct = ca->ca_ct;
	bus_addr_t adr;

	sc->sc_dmat = ca->ca_dmat;
	csc->sc_ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_pc = ca->ca_pc;

	/*
	 * Power management hooks.
	 */
	sc->sc_enable = atw_cardbus_enable;
	sc->sc_disable = atw_cardbus_disable;
	sc->sc_power = atw_cardbus_power;

	/* Get revision info. */
	sc->sc_rev = PCI_REVISION(ca->ca_class);

#if 0
	printf(": signature %08x\n%s",
	    pci_conf_read(ca->ca_pc, csc->sc_tag, 0x80),
	    sc->sc_dev.dv_xname);
#endif

	/*
	 * Map the device.
	 */
	csc->sc_csr = PCI_COMMAND_MASTER_ENABLE;
	if (Cardbus_mapreg_map(ct, ATW_PCI_MMBA,
	    PCI_MAPREG_TYPE_MEM, 0, &sc->sc_st, &sc->sc_sh, &adr,
	    &csc->sc_mapsize) == 0) {
#if 0
		printf(": atw_cardbus_attach mapped %d bytes mem space\n%s",
		    csc->sc_mapsize, sc->sc_dev.dv_xname);
#endif
		csc->sc_cben = CARDBUS_MEM_ENABLE;
		csc->sc_csr |= PCI_COMMAND_MEM_ENABLE;
		csc->sc_bar_reg = ATW_PCI_MMBA;
		csc->sc_bar_val = adr | PCI_MAPREG_TYPE_MEM;
	} else if (Cardbus_mapreg_map(ct, ATW_PCI_IOBA,
	    PCI_MAPREG_TYPE_IO, 0, &sc->sc_st, &sc->sc_sh, &adr,
	    &csc->sc_mapsize) == 0) {
#if 0
		printf(": atw_cardbus_attach mapped %d bytes I/O space\n%s",
		    csc->sc_mapsize, sc->sc_dev.dv_xname);
#endif
		csc->sc_cben = CARDBUS_IO_ENABLE;
		csc->sc_csr |= PCI_COMMAND_IO_ENABLE;
		csc->sc_bar_reg = ATW_PCI_IOBA;
		csc->sc_bar_val = adr | PCI_MAPREG_TYPE_IO;
	} else {
		printf(": unable to map device registers\n");
		return;
	}

	/*
	 * Bring the chip out of powersave mode and initialize the
	 * configuration registers.
	 */
	atw_cardbus_setup(csc);

	/* Remember which interrupt line. */
	csc->sc_intrline = ca->ca_intrline;

	printf(": revision %d.%d: irq %d\n",
	    (sc->sc_rev >> 4) & 0xf, sc->sc_rev & 0xf, csc->sc_intrline);
#if 0
	/*
	 * The CardBus cards will make it to store-and-forward mode as
	 * soon as you put them under any kind of load, so just start
	 * out there.
	 */
	sc->sc_txthresh = 3; /* TBD name constant */
#endif

	/*
	 * Finish off the attach.
	 */
	atw_attach(sc);

	ATW_WRITE(sc, ATW_FER, ATW_FER_INTR);

	/*
	 * Power down the socket.
	 */
	Cardbus_function_disable(csc->sc_ct);
}

int
atw_cardbus_detach(struct device *self, int flags)
{
	struct atw_cardbus_softc *csc = (void *)self;
	struct atw_softc *sc = &csc->sc_atw;
	struct cardbus_devfunc *ct = csc->sc_ct;
	int rv;

#if defined(DIAGNOSTIC)
	if (ct == NULL)
		panic("%s: data structure lacks", sc->sc_dev.dv_xname);
#endif

	rv = atw_detach(sc);
	if (rv)
		return (rv);

	/*
	 * Unhook the interrupt handler.
	 */
	if (csc->sc_ih != NULL)
		cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, csc->sc_ih);

	/*
	 * Release bus space and close window.
	 */
	if (csc->sc_bar_reg != 0)
		Cardbus_mapreg_unmap(ct, csc->sc_bar_reg,
		    sc->sc_st, sc->sc_sh, csc->sc_mapsize);

	return (0);
}

int
atw_cardbus_enable(struct atw_softc *sc)
{
	struct atw_cardbus_softc *csc = (void *) sc;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/*
	 * Power on the socket.
	 */
	Cardbus_function_enable(ct);

	/*
	 * Set up the PCI configuration registers.
	 */
	atw_cardbus_setup(csc);

	/*
	 * Map and establish the interrupt.
	 */
	csc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline, IPL_NET,
	    atw_intr, sc, sc->sc_dev.dv_xname);
	if (csc->sc_ih == NULL) {
		printf("%s: unable to establish interrupt at %d\n",
		    sc->sc_dev.dv_xname, csc->sc_intrline);
		Cardbus_function_disable(csc->sc_ct);
		return (1);
	}

	return (0);
}

void
atw_cardbus_disable(struct atw_softc *sc)
{
	struct atw_cardbus_softc *csc = (void *) sc;
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	cardbus_function_tag_t cf = ct->ct_cf;

	/* Unhook the interrupt handler. */
	cardbus_intr_disestablish(cc, cf, csc->sc_ih);
	csc->sc_ih = NULL;

	/* Power down the socket. */
	Cardbus_function_disable(ct);
}

void
atw_cardbus_power(struct atw_softc *sc, int why)
{
	if (why == DVACT_RESUME)
		atw_enable(sc);
}

void
atw_cardbus_setup(struct atw_cardbus_softc *csc)
{
#ifdef notyet
	struct atw_softc *sc = &csc->sc_atw;
#endif
	cardbus_devfunc_t ct = csc->sc_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = csc->sc_pc;
	pcireg_t reg;

#ifdef notyet
	(void)cardbus_setpowerstate(sc->sc_dev.dv_xname, ct, csc->sc_tag,
	    PCI_PWR_D0);
#endif

	/* Program the BAR. */
	pci_conf_write(pc, csc->sc_tag, csc->sc_bar_reg,
	    csc->sc_bar_val);

	/* Make sure the right access type is on the CardBus bridge. */
	(*ct->ct_cf->cardbus_ctrl)(cc, csc->sc_cben);
	(*ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* Enable the appropriate bits in the PCI CSR. */
	reg = pci_conf_read(pc, csc->sc_tag,
	    PCI_COMMAND_STATUS_REG);
	reg &= ~(PCI_COMMAND_IO_ENABLE|PCI_COMMAND_MEM_ENABLE);
	reg |= csc->sc_csr;
	pci_conf_write(pc, csc->sc_tag, PCI_COMMAND_STATUS_REG,
	    reg);

	/*
	 * Make sure the latency timer is set to some reasonable
	 * value.
	 */
	reg = pci_conf_read(pc, csc->sc_tag, PCI_BHLC_REG);
	if (PCI_LATTIMER(reg) < 0x20) {
		reg &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
		reg |= (0x20 << PCI_LATTIMER_SHIFT);
		pci_conf_write(pc, csc->sc_tag, PCI_BHLC_REG, reg);
	}
}
