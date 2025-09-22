/*	$OpenBSD: if_rtw_pci.c,v 1.22 2024/05/24 06:02:56 jsg Exp $	*/
/*	$NetBSD: if_rtw_pci.c,v 1.1 2004/09/26 02:33:36 dyoung Exp $	*/

/*-
 * Copyright (c) 1998, 1999, 2000, 2002 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center; Charles M. Hannum; and David Young.
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
 * PCI bus front-end for the Realtek RTL8180L 802.11 MAC/BBP chip.
 *
 * Derived from the ADMtek ADM8211 PCI bus front-end.
 *
 * Derived from the ``Tulip'' PCI bus front-end.
 */

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
#include <machine/intr.h>

#include <dev/ic/rtwreg.h>
#include <dev/ic/rtwvar.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcidevs.h>

int rtw_pci_enable(struct rtw_softc *);
void rtw_pci_disable(struct rtw_softc *);
int rtw_pci_detach(struct device *, int);

/*
 * PCI configuration space registers used by the RTL8180L.
 */
#define	RTW_PCI_IOBA		0x10	/* i/o mapped base */
#define	RTW_PCI_MMBA		0x14	/* memory mapped base */

struct rtw_pci_softc {
	struct rtw_softc	psc_rtw;	/* real RTL8180L softc */

	pci_intr_handle_t	psc_ih;		/* interrupt handle */
	void			*psc_intrcookie;

	pci_chipset_tag_t	psc_pc;		/* our PCI chipset */
	pcitag_t		psc_pcitag;	/* our PCI tag */
	bus_size_t		psc_mapsize;
};

int	rtw_pci_match(struct device *, void *, void *);
void	rtw_pci_attach(struct device *, struct device *, void *);

const struct cfattach rtw_pci_ca = {
	sizeof (struct rtw_pci_softc), rtw_pci_match, rtw_pci_attach,
	    rtw_pci_detach, rtw_activate
};

const struct pci_matchid rtw_pci_products[] = {
	{ PCI_VENDOR_REALTEK,	PCI_PRODUCT_REALTEK_RT8180 },
#ifdef RTW_DEBUG
	{ PCI_VENDOR_REALTEK,	PCI_PRODUCT_REALTEK_RT8185 },
	{ PCI_VENDOR_BELKIN2,	PCI_PRODUCT_BELKIN2_F5D7010 },
#endif
	{ PCI_VENDOR_BELKIN2,	PCI_PRODUCT_BELKIN2_F5D6001 },
	{ PCI_VENDOR_BELKIN2,	PCI_PRODUCT_BELKIN2_F5D6020V3 },
	{ PCI_VENDOR_DLINK,	PCI_PRODUCT_DLINK_DWL610 },
};

int
rtw_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, rtw_pci_products,
	    nitems(rtw_pci_products)));
}

int
rtw_pci_enable(struct rtw_softc *sc)
{
	struct rtw_pci_softc *psc = (void *)sc;

	/* Establish the interrupt. */
	psc->psc_intrcookie = pci_intr_establish(psc->psc_pc, psc->psc_ih,
	    IPL_NET, rtw_intr, sc, sc->sc_dev.dv_xname);
	if (psc->psc_intrcookie == NULL) {
		printf("%s: unable to establish interrupt\n",
		    sc->sc_dev.dv_xname);
		return (1);
	}

	return (0);
}

void
rtw_pci_disable(struct rtw_softc *sc)
{
	struct rtw_pci_softc *psc = (void *)sc;

	/* Unhook the interrupt handler. */
	pci_intr_disestablish(psc->psc_pc, psc->psc_intrcookie);
	psc->psc_intrcookie = NULL;
}

void
rtw_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct rtw_pci_softc *psc = (void *) self;
	struct rtw_softc *sc = &psc->psc_rtw;
	struct rtw_regs *regs = &sc->sc_regs;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	const char *intrstr = NULL;
	bus_space_tag_t iot, memt;
	bus_space_handle_t ioh, memh;
	bus_size_t iosize, memsize;
	int ioh_valid, memh_valid;

	psc->psc_pc = pa->pa_pc;
	psc->psc_pcitag = pa->pa_tag;

	/*
	 * No power management hooks.
	 * XXX Maybe we should add some!
	 */
	sc->sc_flags |= RTW_F_ENABLED;

	/*
	 * Get revision info, and set some chip-specific variables.
	 */
	sc->sc_rev = PCI_REVISION(pa->pa_class);

	pci_set_powerstate(pc, pa->pa_tag, PCI_PMCSR_STATE_D0);

	/*
	 * Map the device.
	 */
	ioh_valid = (pci_mapreg_map(pa, RTW_PCI_IOBA,
	    PCI_MAPREG_TYPE_IO, 0,
	    &iot, &ioh, NULL, &iosize, 0) == 0);
	memh_valid = (pci_mapreg_map(pa, RTW_PCI_MMBA,
	    PCI_MAPREG_TYPE_MEM|PCI_MAPREG_MEM_TYPE_32BIT, 0,
	    &memt, &memh, NULL, &memsize, 0) == 0);

	if (memh_valid) {
		regs->r_bt = memt;
		regs->r_bh = memh;
		psc->psc_mapsize = memsize;
	} else if (ioh_valid) {
		regs->r_bt = iot;
		regs->r_bh = ioh;
		psc->psc_mapsize = iosize;
	} else {
		printf(": unable to map device registers\n");
		return;
	}

	sc->sc_dmat = pa->pa_dmat;

	/*
	 * Map and establish our interrupt.
	 */
	if (pci_intr_map(pa, &psc->psc_ih)) {
		printf(": unable to map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, psc->psc_ih); 
	psc->psc_intrcookie = pci_intr_establish(pc, psc->psc_ih, IPL_NET,
	    rtw_intr, sc, sc->sc_dev.dv_xname);
	if (psc->psc_intrcookie == NULL) {
		printf(": unable to establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}

	printf(": %s\n", intrstr);

	sc->sc_enable = rtw_pci_enable;
	sc->sc_disable = rtw_pci_disable;

	/*
	 * Finish off the attach.
	 */
	rtw_attach(sc);
}

int
rtw_pci_detach(struct device *self, int flags)
{
	struct rtw_pci_softc *psc = (void *)self;
	struct rtw_softc *sc = &psc->psc_rtw;
	struct rtw_regs *regs = &sc->sc_regs;
	int rv;

	rv = rtw_detach(sc);
	if (rv)
		return (rv);
	if (psc->psc_intrcookie != NULL)
		pci_intr_disestablish(psc->psc_pc, psc->psc_intrcookie);
	bus_space_unmap(regs->r_bt, regs->r_bh, psc->psc_mapsize);

	return (0);
}
