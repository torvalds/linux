/*	$OpenBSD: if_rl_cardbus.c,v 1.33 2024/05/24 06:26:47 jsg Exp $ */
/*	$NetBSD: if_rl_cardbus.c,v 1.3.8.3 2001/11/14 19:14:02 nathanw Exp $	*/

/*
 * Copyright (c) 2000 Masanori Kanaoka
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * if_rl_cardbus.c:
 *	Cardbus specific routines for Realtek 8139 ethernet adapter.
 *	Tested for 
 *		- elecom-Laneed	LD-10/100CBA (Accton MPX5030)
 *		- MELCO		LPC3-TX-CB   (Realtek 8139)
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_media.h>

#include <netinet/in.h>
#include <netinet/if_ether.h>

#include <machine/bus.h>

#include <dev/mii/miivar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

#include <dev/cardbus/cardbusvar.h>

/*
 * Default to using PIO access for this driver. On SMP systems,
 * there appear to be problems with memory mapped mode: it looks like
 * doing too many memory mapped access back to back in rapid succession
 * can hang the bus. I'm inclined to blame this on crummy design/construction
 * on the part of Realtek. Memory mapped mode does appear to work on
 * uniprocessor systems though.
 */
#define RL_USEIOSPACE 

#include <dev/ic/rtl81x9reg.h>

/*
 * Various supported device vendors/types and their names.
 */
const struct pci_matchid rl_cardbus_devices[] = {
	{ PCI_VENDOR_ABOCOM, PCI_PRODUCT_ABOCOM_FE2000VX },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_5030 },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_2CB_TXD },
	{ PCI_VENDOR_COREGA, PCI_PRODUCT_COREGA_CB_TXD },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_DFE690TXD },
	{ PCI_VENDOR_PLANEX, PCI_PRODUCT_PLANEX_FNW_3603_TX },
	{ PCI_VENDOR_PLANEX, PCI_PRODUCT_PLANEX_FNW_3800_TX },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8138 },
	{ PCI_VENDOR_REALTEK, PCI_PRODUCT_REALTEK_RT8139 },
};

struct rl_cardbus_softc {
	struct rl_softc sc_rl;	/* real rtk softc */ 

	/* CardBus-specific goo. */
	cardbus_devfunc_t sc_ct;
	pci_chipset_tag_t sc_pc;
	pcitag_t sc_tag;
	int sc_csr;
	int sc_cben;
	int sc_bar_reg;
	pcireg_t sc_bar_val;
	bus_size_t sc_mapsize;
	int sc_intrline;
};

static int rl_cardbus_match(struct device *, void *, void *);
static void rl_cardbus_attach(struct device *, struct device *, void *);
static int rl_cardbus_detach(struct device *, int);
void rl_cardbus_setup(struct rl_cardbus_softc *);

const struct cfattach rl_cardbus_ca = {
	sizeof(struct rl_cardbus_softc), rl_cardbus_match, rl_cardbus_attach,
	    rl_cardbus_detach
};

int
rl_cardbus_match(struct device *parent, void *match, void *aux)
{
	return (cardbus_matchbyid((struct cardbus_attach_args *)aux,
	    rl_cardbus_devices, nitems(rl_cardbus_devices)));
}


void
rl_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct rl_cardbus_softc		*csc =
	    (struct rl_cardbus_softc *)self;
	struct rl_softc			*sc = &csc->sc_rl;
	struct cardbus_attach_args	*ca = aux;
	struct cardbus_softc		*psc =
	    (struct cardbus_softc *)sc->sc_dev.dv_parent;
	cardbus_chipset_tag_t		cc = psc->sc_cc;
	cardbus_function_tag_t		cf = psc->sc_cf;                            
	cardbus_devfunc_t		ct = ca->ca_ct;
	bus_addr_t			adr;

	sc->sc_dmat = ca->ca_dmat;
	csc->sc_ct = ct;
	csc->sc_tag = ca->ca_tag;
	csc->sc_intrline = ca->ca_intrline;
	csc->sc_pc = ca->ca_pc;

	/*
	 * Map control/status registers.
	 */
	csc->sc_csr = PCI_COMMAND_MASTER_ENABLE;
#ifdef RL_USEIOSPACE
	if (Cardbus_mapreg_map(ct, RL_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->rl_btag, &sc->rl_bhandle, &adr, &csc->sc_mapsize) == 0) {
		csc->sc_cben = CARDBUS_IO_ENABLE;
		csc->sc_csr |= PCI_COMMAND_IO_ENABLE;
		csc->sc_bar_reg = RL_PCI_LOIO;
		csc->sc_bar_val = adr | PCI_MAPREG_TYPE_IO;
	}
#else
	if (Cardbus_mapreg_map(ct, RL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->rl_btag, &sc->rl_bhandle, &adr, &csc->sc_mapsize) == 0) {
		csc->sc_cben = CARDBUS_MEM_ENABLE;
		csc->sc_csr |= PCI_COMMAND_MEM_ENABLE;
		csc->sc_bar_reg = RL_PCI_LOMEM;
		csc->sc_bar_val = adr | PCI_MAPREG_TYPE_MEM;
	}
#endif
	else {
		printf("%s: unable to map deviceregisters\n",
			 sc->sc_dev.dv_xname);
		return;
	}

	Cardbus_function_enable(ct);

	rl_cardbus_setup(csc);

	/*
	 * Map and establish the interrupt.
	 */
	sc->sc_ih = cardbus_intr_establish(cc, cf, csc->sc_intrline, IPL_NET,
	    rl_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt\n");
		Cardbus_function_disable(csc->sc_ct);
		return;
	}
	printf(": irq %d", csc->sc_intrline);

	sc->rl_type = RL_8139;

	rl_attach(sc);
}

int 
rl_cardbus_detach(struct device *self, int flags)
{
	struct rl_cardbus_softc	*csc = (void *) self;
	struct rl_softc		*sc = &csc->sc_rl;
	struct cardbus_devfunc	*ct = csc->sc_ct;
	int			rv;

#ifdef DIAGNOSTIC
	if (ct == NULL)
		panic("%s: data structure lacks", sc->sc_dev.dv_xname);
#endif
	rv = rl_detach(sc);
	if (rv)
		return (rv);
	/*
	 * Unhook the interrupt handler.
	 */
	if (sc->sc_ih != NULL)
		cardbus_intr_disestablish(ct->ct_cc, ct->ct_cf, sc->sc_ih);
	
	/*
	 * Release bus space and close window.
	 */
	if (csc->sc_bar_reg != 0)
		Cardbus_mapreg_unmap(ct, csc->sc_bar_reg,
			sc->rl_btag, sc->rl_bhandle, csc->sc_mapsize);

	return (0);
}

void 
rl_cardbus_setup(struct rl_cardbus_softc *csc)
{
	struct rl_softc		*sc = &csc->sc_rl;
	cardbus_devfunc_t	ct = csc->sc_ct;
	cardbus_chipset_tag_t	cc = ct->ct_cc;
	pci_chipset_tag_t	pc = csc->sc_pc;
	pcireg_t		reg, command;
	int			pmreg;

	/*
	 * Handle power management nonsense.
	 */
	if (pci_get_capability(pc, csc->sc_tag,
	    PCI_CAP_PWRMGMT, &pmreg, 0)) {
		command = pci_conf_read(pc, csc->sc_tag, pmreg + 4);
		if (command & RL_PSTATE_MASK) {
			pcireg_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_conf_read(pc, csc->sc_tag,
			    RL_PCI_LOIO);
			membase = pci_conf_read(pc, csc->sc_tag,
			    RL_PCI_LOMEM);
			irq = pci_conf_read(pc, csc->sc_tag,
			    PCI_PRODUCT_DELTA_8139);

			/* Reset the power state. */
			printf("%s: chip is in D%d power mode "
			    "-- setting to D0\n", sc->sc_dev.dv_xname,
			    command & RL_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_conf_write(pc, csc->sc_tag,
			    pmreg + 4, command);

			/* Restore PCI config data. */
			pci_conf_write(pc, csc->sc_tag,
			    RL_PCI_LOIO, iobase);
			pci_conf_write(pc, csc->sc_tag,
			    RL_PCI_LOMEM, membase);
			pci_conf_write(pc, csc->sc_tag,
			    PCI_PRODUCT_DELTA_8139, irq);
		}
	}

	/* Make sure the right access type is on the CardBus bridge. */
	(*ct->ct_cf->cardbus_ctrl)(cc, csc->sc_cben);
	(*ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* Program the BAR */
	pci_conf_write(pc, csc->sc_tag,
		csc->sc_bar_reg, csc->sc_bar_val);

	/* Enable the appropriate bits in the CARDBUS CSR. */
	reg = pci_conf_read(pc, csc->sc_tag, 
	    PCI_COMMAND_STATUS_REG);
	reg &= ~(PCI_COMMAND_IO_ENABLE|PCI_COMMAND_MEM_ENABLE);
	reg |= csc->sc_csr;
	pci_conf_write(pc, csc->sc_tag, 
	    PCI_COMMAND_STATUS_REG, reg);

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

